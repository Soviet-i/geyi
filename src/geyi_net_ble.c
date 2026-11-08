/*
 * geyi_net_ble.c —— 联机 BLE 传输实现（REQ-501/508/512）
 *
 * 参考 SDK v2.5.1 例程：
 *   example/ble/peripheral/src/main.c
 *   example/ble/central_and_peripheral/src/main.c
 * 只使用上层 GAP/SIBLES API；GATT 服务为自定义 128-bit：
 *   - RX 特征值：客户端 write（write req/command）到房主；
 *   - TX 特征值：房主 notify（客户端写 CCCD 订阅）到客户端。
 * 消息按 4 字节分片头（total LE16 + offset LE16）传输，接收端按连接重组。
 */
#include "geyi_net_ble.h"

#ifdef GEYI_WITH_RTTHREAD

#include <string.h>
#include "rtthread.h"
#include "bf0_ble_gap.h"
#include "bf0_sibles.h"
#include "bf0_sibles_internal.h"
#include "bf0_sibles_advertising.h"
#include "ble_connection_manager.h"

#define BLE_LOG(fmt, ...) rt_kprintf("[geyi:ble] " fmt "\n", ##__VA_ARGS__)

#define GY_BLE_RXQ_CAP  16
#define GY_BLE_ADVQ_CAP 8
#define GY_BLE_FRAG_HDR 4

/* 16-byte UUIDs (ASCII, easy to recognize in logs). */
#define GY_SVC_UUID {'G','Y','N','E','T','-','R','O','O','M','-','S','V','C','0','1'}
#define GY_RX_UUID  {'G','Y','N','E','T','-','R','X','-','C','H','A','R','0','0','1'}
#define GY_TX_UUID  {'G','Y','N','E','T','-','T','X','-','C','H','A','R','0','0','1'}
#define SERIAL_UUID_16(x) {((uint8_t)(x & 0xff)), ((uint8_t)((x >> 8) & 0xff))}
static const uint8_t g_svc_uuid[ATT_UUID_128_LEN] = GY_SVC_UUID;
static const uint8_t g_rx_uuid[ATT_UUID_128_LEN] = GY_RX_UUID;
static const uint8_t g_tx_uuid[ATT_UUID_128_LEN] = GY_TX_UUID;

enum {
    GY_BLE_SVC = 0,
    GY_BLE_RX_CHAR,
    GY_BLE_RX_VAL,
    GY_BLE_TX_CHAR,
    GY_BLE_TX_VAL,
    GY_BLE_TX_CCCD,
    GY_BLE_ATT_NB
};

BLE_GATT_SERVICE_DEFINE_128(g_geyi_att_db)
{
    BLE_GATT_SERVICE_DECLARE(GY_BLE_SVC, SERIAL_UUID_16_PRI_SERVICE, BLE_GATT_PERM_READ_ENABLE),
    BLE_GATT_CHAR_DECLARE(GY_BLE_RX_CHAR, SERIAL_UUID_16_CHARACTERISTIC, BLE_GATT_PERM_READ_ENABLE),
    BLE_GATT_CHAR_VALUE_DECLARE(GY_BLE_RX_VAL, GY_RX_UUID,
        BLE_GATT_PERM_WRITE_REQ_ENABLE | BLE_GATT_PERM_WRITE_COMMAND_ENABLE,
        BLE_GATT_VALUE_PERM_UUID_128 | BLE_GATT_VALUE_PERM_RI_ENABLE, 512),
    BLE_GATT_CHAR_DECLARE(GY_BLE_TX_CHAR, SERIAL_UUID_16_CHARACTERISTIC, BLE_GATT_PERM_READ_ENABLE),
    BLE_GATT_CHAR_VALUE_DECLARE(GY_BLE_TX_VAL, GY_TX_UUID,
        BLE_GATT_PERM_READ_ENABLE | BLE_GATT_PERM_NOTIFY_ENABLE,
        BLE_GATT_VALUE_PERM_UUID_128 | BLE_GATT_VALUE_PERM_RI_ENABLE, 512),
    BLE_GATT_DESCRIPTOR_DECLARE(GY_BLE_TX_CCCD, SERIAL_UUID_16_CLIENT_CHAR_CFG,
        BLE_GATT_PERM_READ_ENABLE | BLE_GATT_PERM_WRITE_REQ_ENABLE,
        BLE_GATT_VALUE_PERM_RI_ENABLE, 2),
};

typedef struct {
    uint8_t  used;
    uint8_t  conn_idx;
    uint8_t  role;       /* 0 = central(client), 1 = peripheral(host) */
    uint8_t  notify;
    uint16_t mtu;
    uint8_t  addr[6];
    uint8_t  addr_type;
} GyBleConn;

typedef struct {
    uint8_t  active;
    uint16_t total;
    uint16_t got;
    uint8_t  buf[GY_NET_MAX_PACKET];
} GyBleFrag;

static GyBleConn g_conn[GY_NET_BLE_MAX_CONN];
static GyBleFrag g_frag[GY_NET_BLE_MAX_CONN];

static GyNetBleRx g_rxq[GY_BLE_RXQ_CAP];
static uint8_t g_rx_head, g_rx_tail, g_rx_count;
static GyNetBleAdv g_advq[GY_BLE_ADVQ_CAP];
static uint8_t g_adv_head, g_adv_tail, g_adv_count;
static struct rt_mutex g_q_lock;
static int g_q_lock_ready;

static uint8_t g_power_on;
static uint8_t g_mode;              /* 0 = none, 1 = host, 2 = client */
static uint8_t g_client_conn = 0xff;
static sibles_hdl g_srv;
static uint16_t g_rmt_svc = 0;
static uint16_t g_rx_val_hdl, g_tx_val_hdl, g_tx_cccd_hdl;
static uint8_t g_adv_active;
static uint8_t g_scan_active;
static uint8_t g_pending_host, g_pending_scan;
static uint8_t g_adv_payload[16];
static uint8_t g_adv_payload_len;
static uint8_t g_adv_connectable = 1;

SIBLES_ADVERTISING_CONTEXT_DECLAR(g_geyi_adv_ctx);
static uint8_t geyi_adv_event(uint8_t event, void *context, void *data);

static void q_init(void)
{
    if (!g_q_lock_ready) {
        if (rt_mutex_init(&g_q_lock, "gy_net_q", RT_IPC_FLAG_PRIO) == RT_EOK)
            g_q_lock_ready = 1;
    }
}
static void q_lock(void) { if (g_q_lock_ready) rt_mutex_take(&g_q_lock, RT_WAITING_FOREVER); }
static void q_unlock(void) { if (g_q_lock_ready) rt_mutex_release(&g_q_lock); }

static GyBleConn *conn_find(uint8_t conn_idx)
{
    int i;
    for (i = 0; i < GY_NET_BLE_MAX_CONN; ++i)
        if (g_conn[i].used && g_conn[i].conn_idx == conn_idx) return &g_conn[i];
    return RT_NULL;
}
static GyBleConn *conn_alloc(uint8_t conn_idx)
{
    int i;
    for (i = 0; i < GY_NET_BLE_MAX_CONN; ++i) {
        if (!g_conn[i].used) {
            memset(&g_conn[i], 0, sizeof(g_conn[i]));
            g_conn[i].used = 1;
            g_conn[i].conn_idx = conn_idx;
            g_conn[i].mtu = 23;
            return &g_conn[i];
        }
    }
    return RT_NULL;
}
static void conn_free(uint8_t conn_idx)
{
    int i;
    for (i = 0; i < GY_NET_BLE_MAX_CONN; ++i)
        if (g_conn[i].used && g_conn[i].conn_idx == conn_idx) {
            memset(&g_conn[i], 0, sizeof(g_conn[i]));
            memset(&g_frag[i], 0, sizeof(g_frag[i]));
        }
    if (g_client_conn == conn_idx) {
        g_client_conn = 0xff;
        g_rmt_svc = 0;
    }
}

static void push_rx(uint8_t conn, GyNetMsg *msg)
{
    q_lock();
    if (g_rx_count < GY_BLE_RXQ_CAP) {
        g_rxq[g_rx_tail].conn = conn;
        g_rxq[g_rx_tail].msg = *msg;
        g_rx_tail = (uint8_t)((g_rx_tail + 1) % GY_BLE_RXQ_CAP);
        g_rx_count++;
    }
    q_unlock();
}
static void push_adv(const ble_gap_ext_adv_report_ind_t *ind, const GyNetAdvRoom *adv)
{
    q_lock();
    if (g_adv_count < GY_BLE_ADVQ_CAP) {
        GyNetBleAdv *slot = &g_advq[g_adv_tail];
        memset(slot, 0, sizeof(*slot));
        slot->adv = *adv;
        memcpy(slot->addr, ind->addr.addr.addr, 6);
        slot->addr_type = ind->addr.addr_type;
        slot->rssi = ind->rssi;
        g_adv_tail = (uint8_t)((g_adv_tail + 1) % GY_BLE_ADVQ_CAP);
        g_adv_count++;
    }
    q_unlock();
}

int gy_net_ble_recv(GyNetBleRx *out)
{
    int ok = 0;
    if (!out) return 0;
    q_lock();
    if (g_rx_count) {
        *out = g_rxq[g_rx_head];
        g_rx_head = (uint8_t)((g_rx_head + 1) % GY_BLE_RXQ_CAP);
        g_rx_count--;
        ok = 1;
    }
    q_unlock();
    return ok;
}
int gy_net_ble_adv_recv(GyNetBleAdv *out)
{
    int ok = 0;
    if (!out) return 0;
    q_lock();
    if (g_adv_count) {
        *out = g_advq[g_adv_head];
        g_adv_head = (uint8_t)((g_adv_head + 1) % GY_BLE_ADVQ_CAP);
        g_adv_count--;
        ok = 1;
    }
    q_unlock();
    return ok;
}

static void frag_input(uint8_t conn_idx, const uint8_t *data, uint16_t len)
{
    int i;
    GyBleFrag *f = RT_NULL;
    GyNetMsg msg;
    uint16_t total, offset;
    if (!data || len < GY_BLE_FRAG_HDR) return;
    for (i = 0; i < GY_NET_BLE_MAX_CONN; ++i)
        if (g_conn[i].used && g_conn[i].conn_idx == conn_idx) { f = &g_frag[i]; break; }
    if (!f) return;
    total = (uint16_t)(data[0] | ((uint16_t)data[1] << 8));
    offset = (uint16_t)(data[2] | ((uint16_t)data[3] << 8));
    if (!total || total > GY_NET_MAX_PACKET) return;
    if (offset == 0) {
        f->active = 1;
        f->total = total;
        f->got = 0;
    }
    if (!f->active || offset != f->got) { f->active = 0; return; }
    len = (uint16_t)(len - GY_BLE_FRAG_HDR);
    if (f->got + len > f->total) len = (uint16_t)(f->total - f->got);
    memcpy(f->buf + f->got, data + GY_BLE_FRAG_HDR, len);
    f->got = (uint16_t)(f->got + len);
    if (f->got == f->total) {
        if (gy_net_proto_decode(&msg, f->buf, f->total) == GY_NET_OK)
            push_rx(conn_idx, &msg);
        f->active = 0;
    }
}

static int send_bytes(uint8_t conn_idx, const uint8_t *data, uint16_t len)
{
    GyBleConn *c = conn_find(conn_idx);
    uint16_t mtu, chunk, offset = 0;
    uint8_t frag[GY_NET_MAX_PACKET + GY_BLE_FRAG_HDR];
    if (!c || !len) return 0;
    mtu = c->mtu > 10 ? c->mtu : 23;
    chunk = (uint16_t)(mtu - 7);
    if (chunk < 16) chunk = 16;
    if (chunk > sizeof(frag) - GY_BLE_FRAG_HDR) chunk = sizeof(frag) - GY_BLE_FRAG_HDR;
    while (offset < len) {
        uint16_t n = (uint16_t)(len - offset);
        int retry, ok = 0;
        if (n > chunk) n = chunk;
        frag[0] = (uint8_t)(len & 0xff);
        frag[1] = (uint8_t)(len >> 8);
        frag[2] = (uint8_t)(offset & 0xff);
        frag[3] = (uint8_t)(offset >> 8);
        memcpy(frag + GY_BLE_FRAG_HDR, data + offset, n);
        for (retry = 0; retry < 4 && !ok; ++retry) {
            if (g_mode == 1) {
                sibles_value_t v;
                v.hdl = g_srv;
                v.idx = GY_BLE_TX_VAL;
                v.len = (uint16_t)(n + GY_BLE_FRAG_HDR);
                v.value = frag;
                ok = (sibles_write_value(conn_idx, &v) == (int)(n + GY_BLE_FRAG_HDR));
            } else {
                sibles_write_remote_value_t v;
                v.handle = g_rx_val_hdl;
                v.write_type = SIBLES_WRITE_WITHOUT_RSP;
                v.len = (uint16_t)(n + GY_BLE_FRAG_HDR);
                v.value = frag;
                ok = (sibles_write_remote_value(g_rmt_svc, conn_idx, &v) == SIBLES_WRITE_NO_ERR);
            }
            if (!ok) rt_thread_mdelay(5);
        }
        if (!ok) return 0;
        offset = (uint16_t)(offset + n);
    }
    return 1;
}

int gy_net_ble_send(uint8_t conn, const GyNetMsg *msg)
{
    uint8_t buf[GY_NET_MAX_PACKET];
    int n, i, ok = 0;
    if (!msg) return 0;
    n = gy_net_proto_encode(msg, buf, (int)sizeof(buf));
    if (n <= 0) return 0;
    if (g_mode == 1) {
        if (conn != GY_NET_BLE_BROADCAST_CONN) return send_bytes(conn, buf, (uint16_t)n);
        for (i = 0; i < GY_NET_BLE_MAX_CONN; ++i)
            if (g_conn[i].used && g_conn[i].notify) ok |= send_bytes(g_conn[i].conn_idx, buf, (uint16_t)n);
        return ok;
    }
    if (g_client_conn == 0xff || !g_rmt_svc) return 0;
    return send_bytes(g_client_conn, buf, (uint16_t)n);
}

static void build_adv_payload(uint32_t room_id, uint8_t players, uint8_t phase,
                              uint8_t spectators, uint8_t max_spectators)
{
    g_adv_payload[0] = 'G';
    g_adv_payload[1] = 'Y';
    g_adv_payload[2] = GY_NET_VERSION;
    g_adv_payload[3] = (uint8_t)(room_id & 0xff);
    g_adv_payload[4] = (uint8_t)((room_id >> 8) & 0xff);
    g_adv_payload[5] = (uint8_t)((room_id >> 16) & 0xff);
    g_adv_payload[6] = (uint8_t)((room_id >> 24) & 0xff);
    g_adv_payload[7] = players;
    g_adv_payload[8] = phase;
    g_adv_payload[9] = spectators;
    g_adv_payload[10] = max_spectators;
    g_adv_payload_len = 11;
}

static void adv_fill_para(sibles_advertising_para_t *para, int connectable)
{
    char name[20];
    uint16_t company = SIG_SIFLI_COMPANY_ID;
    para->own_addr_type = GAPM_STATIC_ADDR;
    para->config.adv_mode = connectable ? SIBLES_ADV_CONNECT_MODE : SIBLES_ADV_BROADCAST_MODE;
    para->config.max_tx_pwr = 0x7F;
    para->config.is_auto_restart = connectable ? 1 : 0;
    if (connectable) {
        para->config.mode_config.conn_config.duration = 0;
        para->config.mode_config.conn_config.interval = 0x30;
    } else {
        para->config.mode_config.broadcast_config.duration = 0;
        para->config.mode_config.broadcast_config.interval = 0x30;
        para->config.mode_config.broadcast_config.scannable_enable = 1;
    }
    rt_snprintf(name, sizeof(name), "GEYI-%08x", (unsigned)(g_adv_payload[3] |
        ((uint32_t)g_adv_payload[4] << 8) | ((uint32_t)g_adv_payload[5] << 16) |
        ((uint32_t)g_adv_payload[6] << 24)));
    para->rsp_data.completed_name = rt_malloc(sizeof(sibles_adv_type_name_t) + strlen(name));
    if (para->rsp_data.completed_name) {
        para->rsp_data.completed_name->name_len = (uint8_t)strlen(name);
        memcpy(para->rsp_data.completed_name->name, name, strlen(name));
    }
    para->adv_data.manufacturer_data = rt_malloc(sizeof(sibles_adv_type_manufacturer_data_t) + g_adv_payload_len);
    if (para->adv_data.manufacturer_data) {
        para->adv_data.manufacturer_data->company_id = company;
        para->adv_data.manufacturer_data->data_len = g_adv_payload_len;
        memcpy(para->adv_data.manufacturer_data->additional_data, g_adv_payload, g_adv_payload_len);
    }
    para->evt_handler = geyi_adv_event;
}

static void adv_free_para(sibles_advertising_para_t *para)
{
    if (para->rsp_data.completed_name) rt_free(para->rsp_data.completed_name);
    if (para->adv_data.manufacturer_data) rt_free(para->adv_data.manufacturer_data);
    para->rsp_data.completed_name = RT_NULL;
    para->adv_data.manufacturer_data = RT_NULL;
}

static uint8_t geyi_adv_event(uint8_t event, void *context, void *data)
{
    (void)context; (void)data;
    if (event == SIBLES_ADV_EVT_ADV_STARTED) BLE_LOG("adv started");
    else if (event == SIBLES_ADV_EVT_ADV_STOPPED) BLE_LOG("adv stopped");
    return 0;
}

static int adv_start(int connectable)
{
    sibles_advertising_para_t para;
    uint8_t ret;
    if (!g_power_on) { g_pending_host = 1; g_adv_connectable = (uint8_t)connectable; return 0; }
    if (g_adv_active) {
        sibles_advertising_stop(g_geyi_adv_ctx);
        sibles_advertising_delete(g_geyi_adv_ctx);
        g_adv_active = 0;
    }
    memset(&para, 0, sizeof(para));
    adv_fill_para(&para, connectable);
    ret = sibles_advertising_init(g_geyi_adv_ctx, &para);
    adv_free_para(&para);
    if (ret != SIBLES_ADV_NO_ERR) { BLE_LOG("adv init err %d", ret); return -1; }
    ret = sibles_advertising_start(g_geyi_adv_ctx);
    if (ret != SIBLES_ADV_NO_ERR) { BLE_LOG("adv start err %d", ret); return -1; }
    g_adv_active = 1;
    g_adv_connectable = (uint8_t)connectable;
    return 0;
}

int gy_net_ble_host_start(uint32_t room_id, uint8_t players, uint8_t phase,
                          uint8_t spectators, uint8_t max_spectators)
{
    g_mode = 1;
    build_adv_payload(room_id, players, phase, spectators, max_spectators);
    return adv_start(1);
}

int gy_net_ble_host_update(uint8_t players, uint8_t phase, uint8_t spectators,
                           uint8_t max_spectators, int connectable)
{
    uint32_t room_id = (uint32_t)g_adv_payload[3] |
        ((uint32_t)g_adv_payload[4] << 8) | ((uint32_t)g_adv_payload[5] << 16) |
        ((uint32_t)g_adv_payload[6] << 24);
    int mode_changed = (!!connectable != !!g_adv_connectable);
    build_adv_payload(room_id, players, phase, spectators, max_spectators);
    if (mode_changed) return adv_start(connectable);
    if (!g_adv_active) return adv_start(connectable);
    {
        sibles_advertising_para_t para;
        uint8_t ret;
        memset(&para, 0, sizeof(para));
        adv_fill_para(&para, connectable);
        ret = sibles_advertising_update_adv_and_scan_rsp_data(g_geyi_adv_ctx,
                                                              &para.adv_data, &para.rsp_data);
        adv_free_para(&para);
        if (ret != SIBLES_ADV_NO_ERR) { BLE_LOG("adv update err %d", ret); return -1; }
    }
    return 0;
}

void gy_net_ble_host_stop(void)
{
    if (g_adv_active) {
        sibles_advertising_stop(g_geyi_adv_ctx);
        sibles_advertising_delete(g_geyi_adv_ctx);
        g_adv_active = 0;
    }
    g_pending_host = 0;
}

int gy_net_ble_scan_start(void)
{
    ble_gap_scan_start_t p;
    if (!g_power_on) { g_pending_scan = 1; return 0; }
    memset(&p, 0, sizeof(p));
    p.own_addr_type = GAPM_STATIC_ADDR;
    p.type = GAPM_SCAN_TYPE_OBSERVER;
    p.dup_filt_pol = 0;
    p.scan_param_1m.scan_intv = 0x30;
    p.scan_param_1m.scan_wd = 0x30;
    p.duration = 0;
    p.period = 0;
    if (ble_gap_scan_start(&p) != HL_ERR_NO_ERROR) return -1;
    g_scan_active = 1;
    return 0;
}
void gy_net_ble_scan_stop(void)
{
    if (g_scan_active) { ble_gap_scan_stop(); g_scan_active = 0; }
    g_pending_scan = 0;
}

int gy_net_ble_connect(const uint8_t addr[6], uint8_t addr_type)
{
    ble_gap_connection_create_param_t p;
    if (!addr) return -1;
    gy_net_ble_scan_stop();
    memset(&p, 0, sizeof(p));
    p.own_addr_type = GAPM_STATIC_ADDR;
    p.conn_to = 500;
    p.type = GAPM_INIT_TYPE_DIRECT_CONN_EST;
    p.conn_param_1m.scan_intv = 0x30;
    p.conn_param_1m.scan_wd = 0x30;
    p.conn_param_1m.conn_intv_min = 0x60;
    p.conn_param_1m.conn_intv_max = 0x80;
    p.conn_param_1m.conn_latency = 0;
    p.conn_param_1m.supervision_to = 500;
    p.conn_param_1m.ce_len_min = 0;
    p.conn_param_1m.ce_len_max = 100;
    p.peer_addr.addr_type = addr_type;
    memcpy(p.peer_addr.addr.addr, addr, 6);
    g_mode = 2;
    if (ble_gap_create_connection(&p) != HL_ERR_NO_ERROR) return -1;
    return 0;
}

void gy_net_ble_disconnect(uint8_t conn)
{
    ble_gap_disconnect_t d;
    d.conn_idx = conn;
    d.reason = 0x13; /* remote user terminated */
    ble_gap_disconnect(&d);
}

int gy_net_ble_connected(void)
{
    int i;
    for (i = 0; i < GY_NET_BLE_MAX_CONN; ++i)
        if (g_conn[i].used) return 1;
    return 0;
}
int gy_net_ble_ready(void)
{
    return (g_client_conn != 0xff && g_rmt_svc && g_rx_val_hdl && g_tx_val_hdl) ? 1 : 0;
}
uint8_t gy_net_ble_conn_count(void)
{
    int i, n = 0;
    for (i = 0; i < GY_NET_BLE_MAX_CONN; ++i) if (g_conn[i].used) n++;
    return (uint8_t)n;
}
uint8_t gy_net_ble_conn_at(uint8_t index)
{
    int i, n = 0;
    for (i = 0; i < GY_NET_BLE_MAX_CONN; ++i)
        if (g_conn[i].used) {
            if (n == index) return g_conn[i].conn_idx;
            n++;
        }
    return 0xff;
}
uint8_t gy_net_ble_client_conn(void) { return g_client_conn; }
uint16_t gy_net_ble_mtu(uint8_t conn)
{
    GyBleConn *c = conn_find(conn);
    return c ? c->mtu : 23;
}

static uint8_t *ble_get_cb(uint8_t conn_idx, uint8_t idx, uint16_t *len)
{
    (void)conn_idx;
    if (len) *len = 0;
    if (idx == GY_BLE_TX_VAL && len) *len = 0;
    return RT_NULL;
}
static uint8_t ble_set_cb(uint8_t conn_idx, sibles_set_cbk_t *para)
{
    GyBleConn *c;
    if (!para) return 1;
    c = conn_find(conn_idx);
    if (!c) return 1;
    if (para->idx == GY_BLE_RX_VAL) {
        frag_input(conn_idx, para->value, para->len);
    } else if (para->idx == GY_BLE_TX_CCCD) {
        c->notify = (uint8_t)(para->len >= 1 && (para->value[0] & 1));
        BLE_LOG("cccd conn=%d notify=%d", conn_idx, c->notify);
    }
    return 0;
}

static void ble_service_init(void)
{
    BLE_GATT_SERVICE_INIT_128(svc, g_geyi_att_db, GY_BLE_ATT_NB,
        BLE_GATT_SERVICE_PERM_NOAUTH | BLE_GATT_SERVICE_PERM_UUID_128 |
        BLE_GATT_SERVICE_PERM_MULTI_LINK, (uint8_t *)g_svc_uuid);
    g_srv = sibles_register_svc_128(&svc);
    if (g_srv) sibles_register_cbk(g_srv, ble_get_cb, ble_set_cb);
    else BLE_LOG("service register failed");
}

static void parse_adv(const ble_gap_ext_adv_report_ind_t *ind)
{
    const uint8_t *p = ind->data;
    uint16_t left = ind->length;
    while (left >= 2) {
        uint8_t len = p[0];
        uint8_t type = p[1];
        if (len == 0 || len + 1 > left) break;
        if (type == 0xFF && len >= 1 + 2 + 11) {
            const uint8_t *d = p + 2;
            uint16_t company = (uint16_t)(d[0] | ((uint16_t)d[1] << 8));
            if (company == SIG_SIFLI_COMPANY_ID && d[2] == 'G' && d[3] == 'Y' &&
                d[4] == GY_NET_VERSION) {
                GyNetAdvRoom adv;
                adv.room_id = (uint32_t)d[5] | ((uint32_t)d[6] << 8) |
                              ((uint32_t)d[7] << 16) | ((uint32_t)d[8] << 24);
                adv.players = d[9];
                adv.phase = d[10];
                adv.spectators = d[11];
                adv.max_spectators = d[12];
                push_adv(ind, &adv);
                return;
            }
        }
        p += len + 1;
        left = (uint16_t)(left - len - 1);
    }
}

static int geyi_gattc_event(uint16_t event_id, uint8_t *data, uint16_t len)
{
    (void)len;
    if (event_id == SIBLES_REMOTE_EVENT_IND) {
        sibles_remote_event_ind_t *ind = (sibles_remote_event_ind_t *)data;
        frag_input(ind->conn_idx, ind->value, ind->length);
    } else if (event_id == SIBLES_REGISTER_REMOTE_SVC_RSP) {
        sibles_register_remote_svc_rsp_t *rsp = (sibles_register_remote_svc_rsp_t *)data;
        BLE_LOG("remote svc registered conn=%d status=%d", rsp->conn_idx, rsp->status);
    }
    return 0;
}

static int geyi_ble_event(uint16_t event_id, uint8_t *data, uint16_t len, uint32_t context)
{
    (void)len; (void)context;
    switch (event_id) {
    case BLE_POWER_ON_IND:
        g_power_on = 1;
        ble_service_init();
        if (g_pending_host) { g_pending_host = 0; adv_start(g_adv_connectable); }
        if (g_pending_scan) { g_pending_scan = 0; gy_net_ble_scan_start(); }
        BLE_LOG("power on");
        break;
    case BLE_GAP_CONNECTED_IND: {
        ble_gap_connect_ind_t *ind = (ble_gap_connect_ind_t *)data;
        GyBleConn *c = conn_alloc(ind->conn_idx);
        if (c) {
            c->role = ind->role;
            c->addr_type = ind->peer_addr_type;
            memcpy(c->addr, ind->peer_addr.addr, 6);
            if (g_mode == 2) g_client_conn = ind->conn_idx;
            sibles_exchange_mtu(ind->conn_idx);
            BLE_LOG("connected idx=%d role=%d", ind->conn_idx, ind->role);
            if (g_mode == 2)
                sibles_search_service(ind->conn_idx, ATT_UUID_128_LEN, (uint8_t *)g_svc_uuid);
        }
        break;
    }
    case BLE_GAP_DISCONNECTED_IND: {
        ble_gap_disconnected_ind_t *ind = (ble_gap_disconnected_ind_t *)data;
        conn_free(ind->conn_idx);
        BLE_LOG("disconnected idx=%d reason=%d", ind->conn_idx, ind->reason);
        break;
    }
    case SIBLES_MTU_EXCHANGE_IND: {
        sibles_mtu_exchange_ind_t *ind = (sibles_mtu_exchange_ind_t *)data;
        GyBleConn *c = conn_find(ind->conn_idx);
        if (c) c->mtu = ind->mtu;
        BLE_LOG("mtu idx=%d mtu=%d", ind->conn_idx, ind->mtu);
        break;
    }
    case SIBLES_SEARCH_SVC_RSP: {
        sibles_svc_search_rsp_t *rsp = (sibles_svc_search_rsp_t *)data;
        uint8_t i;
        uint8_t *p;
        BLE_LOG("search svc result=%d conn=%d", rsp->result, rsp->conn_idx);
        if (rsp->result != HL_ERR_NO_ERROR || !rsp->svc) break;
        p = rsp->svc->att_db;
        for (i = 0; i < rsp->svc->char_count; ++i) {
            sibles_svc_search_char_t *ch = (sibles_svc_search_char_t *)p;
            if (ch->uuid_len == ATT_UUID_128_LEN) {
                if (memcmp(ch->uuid, g_rx_uuid, ATT_UUID_128_LEN) == 0)
                    g_rx_val_hdl = ch->pointer_hdl;
                else if (memcmp(ch->uuid, g_tx_uuid, ATT_UUID_128_LEN) == 0) {
                    g_tx_val_hdl = ch->pointer_hdl;
                    g_tx_cccd_hdl = sibles_descriptor_handle_find(ch, ATT_DESC_CLIENT_CHAR_CFG);
                }
            }
            p += sizeof(sibles_svc_search_char_t) +
                 ch->desc_count * sizeof(struct sibles_disc_char_desc_ind);
        }
        g_rmt_svc = sibles_register_remote_svc(rsp->conn_idx, rsp->svc->hdl_start,
                                               rsp->svc->hdl_end, geyi_gattc_event);
        if (g_rmt_svc && g_tx_cccd_hdl) {
            sibles_write_remote_value_t v;
            uint8_t cccd[2] = {1, 0};
            v.handle = g_tx_cccd_hdl;
            v.write_type = SIBLES_WRITE;
            v.len = 2;
            v.value = cccd;
            sibles_write_remote_value(g_rmt_svc, rsp->conn_idx, &v);
        }
        BLE_LOG("chars rx=%d tx=%d cccd=%d rmt=%d", g_rx_val_hdl, g_tx_val_hdl,
                g_tx_cccd_hdl, g_rmt_svc);
        break;
    }
    case BLE_GAP_EXT_ADV_REPORT_IND:
        parse_adv((ble_gap_ext_adv_report_ind_t *)data);
        break;
    case BLE_GAP_SCAN_START_CNF:
        BLE_LOG("scan started");
        break;
    case BLE_GAP_SCAN_STOPPED_IND:
        g_scan_active = 0;
        BLE_LOG("scan stopped");
        break;
    default:
        break;
    }
    return 0;
}
BLE_EVENT_REGISTER(geyi_ble_event, NULL);

void gy_net_ble_init(void)
{
    static int inited;
    if (inited) return;
    inited = 1;
    q_init();
    memset(g_conn, 0, sizeof(g_conn));
    memset(g_frag, 0, sizeof(g_frag));
    g_client_conn = 0xff;
    sifli_ble_enable();
}

int gy_net_ble_selftest(void)
{
    GyNetMsg m;
    uint8_t buf[16];
    int n;
    gy_net_msg_init(&m, GY_NET_PING, 1);
    n = gy_net_proto_encode(&m, buf, sizeof(buf));
    return n == GY_NET_HEADER_SIZE ? 0 : -1;
}

#endif /* GEYI_WITH_RTTHREAD */
