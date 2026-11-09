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
#include "bf0_ble_common.h"
#include "bf0_sibles.h"
#include "bf0_sibles_internal.h"
#include "bf0_sibles_advertising.h"
#include "ble_connection_manager.h"

#define BLE_LOG(fmt, ...) rt_kprintf("[%u][geyi:ble] " fmt "\n", (unsigned)rt_tick_get(), ##__VA_ARGS__)

#define GY_BLE_RXQ_CAP  16
#define GY_BLE_ADVQ_CAP 8
#define GY_BLE_TXQ_CAP  8
#define GY_BLE_FRAG_HDR 4

/* 16-byte UUIDs. BLE/GATT stores 128-bit UUIDs LSB-first, so these arrays
 * are the byte-reversed form of the readable names:
 *   GYNET-ROOM-SVC01 / GYNET-RX-CHAR001 / GYNET-TX-CHAR001
 * Using the readable order here makes the on-air UUID reversed and prevents
 * the client's UUID search from matching the server's service. */
#define GY_SVC_UUID {'1','0','C','V','S','-','M','O','O','R','-','T','E','N','Y','G'}
#define GY_RX_UUID  {'1','0','0','R','A','H','C','-','X','R','-','T','E','N','Y','G'}
#define GY_TX_UUID  {'1','0','0','R','A','H','C','-','X','T','-','T','E','N','Y','G'}
#define SERIAL_UUID_16(x) {((uint8_t)(x & 0xff)), ((uint8_t)((x >> 8) & 0xff))}
static const uint8_t g_svc_uuid[ATT_UUID_128_LEN] = GY_SVC_UUID;
static const uint8_t g_rx_uuid[ATT_UUID_128_LEN] = GY_RX_UUID;
static const uint8_t g_tx_uuid[ATT_UUID_128_LEN] = GY_TX_UUID;
/* 搜索接口使用可读（MSB-first）顺序；GATT 数据库/空口使用 LSB-first。 */
static const uint8_t g_svc_uuid_be[ATT_UUID_128_LEN] =
    {'G','Y','N','E','T','-','R','O','O','M','-','S','V','C','0','1'};
static const uint8_t g_rx_uuid_be[ATT_UUID_128_LEN] =
    {'G','Y','N','E','T','-','R','X','-','C','H','A','R','0','0','1'};
static const uint8_t g_tx_uuid_be[ATT_UUID_128_LEN] =
    {'G','Y','N','E','T','-','T','X','-','C','H','A','R','0','0','1'};

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
typedef struct {
    uint8_t  conn;
    GyNetMsg msg;
} GyBleTx;
static GyBleTx g_txq[GY_BLE_TXQ_CAP];
static uint8_t g_tx_head, g_tx_tail, g_tx_count;
static struct rt_semaphore g_tx_sem;
static rt_thread_t g_tx_thread;
static int g_tx_ready;
static struct rt_mutex g_q_lock;
static int g_q_lock_ready;
static struct rt_mutex g_conn_lock;
static int g_conn_lock_ready;

static uint8_t g_power_on;
static uint8_t g_service_ready;
static uint8_t g_pending_search_conn = 0xff;
static uint8_t g_mode;              /* 0 = none, 1 = host, 2 = client */
static uint8_t g_client_conn = 0xff;
static sibles_hdl g_srv;
static uint16_t g_rmt_svc = 0;
static uint8_t g_remote_svc_ok;
static uint16_t g_rx_val_hdl, g_tx_val_hdl, g_tx_cccd_hdl;
static uint8_t g_cccd_ok;
static uint8_t g_adv_active;
static uint8_t g_adv_started;   /* ADV_STARTED 事件已到，才允许 update */
static uint8_t g_adv_inited;    /* init 成功，协议栈可自动重启广播 */
static uint8_t g_adv_stopping;
static uint8_t g_scan_active;
static uint8_t g_pending_host, g_pending_scan;
static uint32_t g_scan_retry_at;
static uint8_t g_adv_payload[16];
static uint8_t g_adv_payload_len;
static uint8_t g_adv_connectable = 1;
static uint8_t g_adv_dirty;      /* 广播内容变化，等待状态机允许时写回 */
static uint32_t g_adv_retry_at;
static uint32_t g_adv_stopped_at;
static uint32_t g_adv_start_at;

SIBLES_ADVERTISING_CONTEXT_DECLAR(g_geyi_adv_ctx);
static uint8_t geyi_adv_event(uint8_t event, void *context, void *data);

static void q_init(void)
{
    if (!g_q_lock_ready) {
        if (rt_mutex_init(&g_q_lock, "gy_net_q", RT_IPC_FLAG_PRIO) == RT_EOK)
            g_q_lock_ready = 1;
    }
    if (!g_conn_lock_ready) {
        if (rt_mutex_init(&g_conn_lock, "gy_net_c", RT_IPC_FLAG_PRIO) == RT_EOK)
            g_conn_lock_ready = 1;
    }
}
static void q_lock(void) { if (g_q_lock_ready) rt_mutex_take(&g_q_lock, RT_WAITING_FOREVER); }
static void q_unlock(void) { if (g_q_lock_ready) rt_mutex_release(&g_q_lock); }
static void c_lock(void) { if (g_conn_lock_ready) rt_mutex_take(&g_conn_lock, RT_WAITING_FOREVER); }
static void c_unlock(void) { if (g_conn_lock_ready) rt_mutex_release(&g_conn_lock); }

static GyBleConn *conn_find(uint8_t conn_idx)
{
    int i;
    GyBleConn *found = RT_NULL;
    c_lock();
    for (i = 0; i < GY_NET_BLE_MAX_CONN; ++i)
        if (g_conn[i].used && g_conn[i].conn_idx == conn_idx) { found = &g_conn[i]; break; }
    c_unlock();
    return found;
}
static GyBleConn *conn_alloc(uint8_t conn_idx)
{
    int i;
    GyBleConn *found = RT_NULL;
    c_lock();
    for (i = 0; i < GY_NET_BLE_MAX_CONN; ++i) {
        if (!g_conn[i].used) {
            memset(&g_conn[i], 0, sizeof(g_conn[i]));
            g_conn[i].used = 1;
            g_conn[i].conn_idx = conn_idx;
            g_conn[i].mtu = 23;
            found = &g_conn[i];
            break;
        }
    }
    c_unlock();
    return found;
}
static void conn_free(uint8_t conn_idx)
{
    int i;
    c_lock();
    for (i = 0; i < GY_NET_BLE_MAX_CONN; ++i)
        if (g_conn[i].used && g_conn[i].conn_idx == conn_idx) {
            memset(&g_conn[i], 0, sizeof(g_conn[i]));
            memset(&g_frag[i], 0, sizeof(g_frag[i]));
        }
    if (g_client_conn == conn_idx) {
        g_client_conn = 0xff;
        g_rmt_svc = 0;
        g_remote_svc_ok = 0;
        g_cccd_ok = 0;
    }
    c_unlock();
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
    c_lock();
    for (i = 0; i < GY_NET_BLE_MAX_CONN; ++i)
        if (g_conn[i].used && g_conn[i].conn_idx == conn_idx) { f = &g_frag[i]; break; }
    if (!f) { c_unlock(); return; }
    total = (uint16_t)(data[0] | ((uint16_t)data[1] << 8));
    offset = (uint16_t)(data[2] | ((uint16_t)data[3] << 8));
    if (!total || total > GY_NET_MAX_PACKET) { c_unlock(); return; }
    if (offset == 0) {
        f->active = 1;
        f->total = total;
        f->got = 0;
    }
    if (!f->active || offset != f->got) { f->active = 0; c_unlock(); return; }
    len = (uint16_t)(len - GY_BLE_FRAG_HDR);
    if (f->got + len > f->total) len = (uint16_t)(f->total - f->got);
    memcpy(f->buf + f->got, data + GY_BLE_FRAG_HDR, len);
    f->got = (uint16_t)(f->got + len);
    if (f->got == f->total) {
        int ok = gy_net_proto_decode(&msg, f->buf, f->total);
        f->active = 0;
        c_unlock();
        if (ok == GY_NET_OK) push_rx(conn_idx, &msg);
        return;
    }
    c_unlock();
}

static int send_bytes(uint8_t conn_idx, const uint8_t *data, uint16_t len)
{
    uint16_t mtu = 23, chunk, offset = 0;
    uint8_t frag[GY_NET_MAX_PACKET + GY_BLE_FRAG_HDR];
    int i, found = 0;
    if (!len) return 0;
    c_lock();
    for (i = 0; i < GY_NET_BLE_MAX_CONN; ++i)
        if (g_conn[i].used && g_conn[i].conn_idx == conn_idx) {
            mtu = g_conn[i].mtu > 10 ? g_conn[i].mtu : 23;
            found = 1;
            break;
        }
    c_unlock();
    if (!found) return 0;
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
                int wr;
                v.handle = g_rx_val_hdl;
                v.write_type = SIBLES_WRITE_WITHOUT_RSP;
                v.len = (uint16_t)(n + GY_BLE_FRAG_HDR);
                v.value = frag;
                wr = sibles_write_remote_value(g_rmt_svc, conn_idx, &v);
                ok = (wr == SIBLES_WRITE_NO_ERR);
                if (!ok) BLE_LOG("remote write fail conn=%d hdl=%d ret=%d",
                                 conn_idx, g_rx_val_hdl, wr);
            }
            if (!ok) rt_thread_mdelay(5);
        }
        if (!ok) return 0;
        offset = (uint16_t)(offset + n);
    }
    return 1;
}

static void tx_thread_entry(void *parameter)
{
    GyBleTx item;
    (void)parameter;
    for (;;) {
        uint8_t buf[GY_NET_MAX_PACKET];
        int n, i, ok = 0;
        rt_sem_take(&g_tx_sem, RT_WAITING_FOREVER);
        q_lock();
        if (!g_tx_count) { q_unlock(); continue; }
        item = g_txq[g_tx_head];
        g_tx_head = (uint8_t)((g_tx_head + 1) % GY_BLE_TXQ_CAP);
        g_tx_count--;
        q_unlock();
        n = gy_net_proto_encode(&item.msg, buf, (int)sizeof(buf));
        if (n <= 0) continue;
        if (g_mode == 1) {
            uint8_t conns[GY_NET_BLE_MAX_CONN];
            int count = 0;
            if (item.conn != GY_NET_BLE_BROADCAST_CONN) {
                send_bytes(item.conn, buf, (uint16_t)n);
                continue;
            }
            c_lock();
            for (i = 0; i < GY_NET_BLE_MAX_CONN; ++i)
                if (g_conn[i].used && g_conn[i].notify)
                    conns[count++] = g_conn[i].conn_idx;
            c_unlock();
            for (i = 0; i < count; ++i) ok |= send_bytes(conns[i], buf, (uint16_t)n);
            (void)ok;
        } else if (g_client_conn != 0xff && g_remote_svc_ok) {
            send_bytes(g_client_conn, buf, (uint16_t)n);
        }
    }
}

int gy_net_ble_send(uint8_t conn, const GyNetMsg *msg)
{
    if (!msg || !g_tx_ready) return 0;
    q_lock();
    if (g_tx_count >= GY_BLE_TXQ_CAP) { q_unlock(); return 0; }
    g_txq[g_tx_tail].conn = conn;
    g_txq[g_tx_tail].msg = *msg;
    g_tx_tail = (uint8_t)((g_tx_tail + 1) % GY_BLE_TXQ_CAP);
    g_tx_count++;
    q_unlock();
    rt_sem_release(&g_tx_sem);
    return 1;
}

void gy_net_ble_flush(uint32_t timeout_ms)
{
    uint32_t waited = 0;
    if (!g_tx_ready) return;
    for (;;) {
        int pending;
        q_lock();
        pending = g_tx_count;
        q_unlock();
        if (!pending || waited >= timeout_ms) return;
        rt_thread_mdelay(5);
        waited += 5;
    }
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
    /* 与 SDK v2.5.1 example/ble/peripheral 的 ble_app_advertising_start
     * 保持一致：LEGACY CONNECT 模式、名称放 rsp_data、自动重启广播。 */
    para->config.adv_mode = connectable ? SIBLES_ADV_CONNECT_MODE : SIBLES_ADV_BROADCAST_MODE;
    para->config.max_tx_pwr = 0x7F;
    para->config.is_auto_restart = 1;
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
    {
        ble_gap_dev_name_t *dn = rt_malloc(sizeof(ble_gap_dev_name_t) + strlen(name));
        if (dn) {
            dn->len = (uint8_t)strlen(name);
            memcpy(dn->name, name, strlen(name));
            ble_gap_set_dev_name(dn);
            rt_free(dn);
        }
    }
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
    if (para->adv_data.completed_name) rt_free(para->adv_data.completed_name);
    if (para->rsp_data.completed_name) rt_free(para->rsp_data.completed_name);
    if (para->adv_data.manufacturer_data) rt_free(para->adv_data.manufacturer_data);
    para->adv_data.completed_name = RT_NULL;
    para->rsp_data.completed_name = RT_NULL;
    para->adv_data.manufacturer_data = RT_NULL;
}

static uint8_t geyi_adv_event(uint8_t event, void *context, void *data)
{
    (void)context;
    if (event == SIBLES_ADV_EVT_ADV_STARTED) {
        sibles_adv_evt_startted_t *evt = (sibles_adv_evt_startted_t *)data;
        uint8_t status = evt ? evt->status : 0;
        g_adv_active = (status == 0) ? 1 : 0;
        g_adv_started = g_adv_active;
        if (g_adv_active) { g_adv_stopped_at = 0; g_adv_stopping = 0; }
        BLE_LOG("adv started status=%d", status);
        if (g_adv_active && g_mode == 0) {
            /* host_stop 与在途 start 竞争：已经关房就立刻再停掉。 */
            sibles_advertising_stop(g_geyi_adv_ctx);
            sibles_advertising_delete(g_geyi_adv_ctx);
            g_adv_active = 0;
            g_adv_started = 0;
            g_adv_inited = 0;
        }
    } else if (event == SIBLES_ADV_EVT_ADV_STOPPED) {
        if (g_adv_stopping) {
            /* 旧实例的延迟 STOPPED 事件，不能把新实例标成已停。 */
            BLE_LOG("adv stopped (stale ignored)");
            return 0;
        }
        g_adv_active = 0;
        g_adv_started = 0;
        g_adv_stopped_at = rt_tick_get();
        BLE_LOG("adv stopped");
        /* config.is_auto_restart=1 已由协议栈负责重启；这里再 start 会
         * 叠加 advertising 实例，最终 Create advertising failed。 */
    }
    return 0;
}

static int adv_start(int connectable)
{
    sibles_advertising_para_t para;
    uint8_t ret;
    if (!g_power_on) {
        g_pending_host = 1;
        g_adv_connectable = (uint8_t)connectable;
        BLE_LOG("adv pending power=0");
        return 0;
    }
    BLE_LOG("adv_start connectable=%d", connectable);
    g_adv_stopping = 1;
    g_adv_stopped_at = 0;
    if (g_adv_active) {
        sibles_advertising_stop(g_geyi_adv_ctx);
        g_adv_active = 0;
    }
    if (g_adv_inited) {
        sibles_advertising_delete(g_geyi_adv_ctx);
        g_adv_inited = 0;
        rt_thread_mdelay(20);   /* 等协议栈释放 adv 实例 */
    }
    g_adv_started = 0;
    memset(&para, 0, sizeof(para));
    adv_fill_para(&para, connectable);
    ret = sibles_advertising_init(g_geyi_adv_ctx, &para);
    adv_free_para(&para);
    BLE_LOG("adv init ret=%d", ret);
    if (ret != SIBLES_ADV_NO_ERR) { g_adv_stopping = 0; BLE_LOG("adv init err %d", ret); return -1; }
    g_adv_inited = 1;
    ret = sibles_advertising_start(g_geyi_adv_ctx);
    BLE_LOG("adv start ret=%d", ret);
    if (ret != SIBLES_ADV_NO_ERR) {
        g_adv_stopping = 0;
        sibles_advertising_delete(g_geyi_adv_ctx);
        g_adv_inited = 0;   /* 下次 host_update 重新 init/start */
        BLE_LOG("adv start err %d", ret);
        return -1;
    }
    g_adv_active = 1;
    g_adv_connectable = (uint8_t)connectable;
    g_adv_dirty = 0;   /* init 已使用当前 g_adv_payload */
    g_adv_start_at = rt_tick_get();
    /* 保持 stopping=1 直到 ADV_STARTED，忽略上一实例的延迟 STOPPED。 */
    return 0;
}

int gy_net_ble_host_start(uint32_t room_id, uint8_t players, uint8_t phase,
                          uint8_t spectators, uint8_t max_spectators)
{
    int ret;
    g_mode = 1;
    build_adv_payload(room_id, players, phase, spectators, max_spectators);
    BLE_LOG("host_start room=%08x players=%d power=%d",
            (unsigned)room_id, players, g_power_on);
    ret = adv_start(1);
    BLE_LOG("host_start ret=%d", ret);
    return ret;
}

static int adv_apply_payload(int connectable)
{
    sibles_advertising_para_t para;
    uint8_t ret;
    memset(&para, 0, sizeof(para));
    adv_fill_para(&para, connectable);
    ret = sibles_advertising_update_adv_and_scan_rsp_data(g_geyi_adv_ctx,
                                                          &para.adv_data, &para.rsp_data);
    adv_free_para(&para);
    if (ret != SIBLES_ADV_NO_ERR) {
        BLE_LOG("adv update defer ret=%d", ret);
        return -1;
    }
    g_adv_dirty = 0;
    return 0;
}

int gy_net_ble_host_update(uint8_t players, uint8_t phase, uint8_t spectators,
                           uint8_t max_spectators, int connectable)
{
    uint32_t room_id = (uint32_t)g_adv_payload[3] |
        ((uint32_t)g_adv_payload[4] << 8) | ((uint32_t)g_adv_payload[5] << 16) |
        ((uint32_t)g_adv_payload[6] << 24);
    int mode_changed = (!!connectable != !!g_adv_connectable);
    build_adv_payload(room_id, players, phase, spectators, max_spectators);
    g_adv_dirty = 1;
    if (mode_changed) return adv_start(connectable);
    /* 广播可能因连接/断线处于自动重启窗口，此时不要重建实例；
     * 新人数留在 g_adv_payload，ADV_STARTED 后由 poll 写回。 */
    if (!g_adv_active) {
        if (g_adv_inited) return 0;
        return adv_start(connectable);
    }
    /* 协议栈在 STARTING/STOPPING/IDLE 时拒绝 update；不要在这里重建
     * advertising（会叠出多个实例并报 Create advertising failed），
     * 交给 gy_net_ble_poll 在 ADV_STARTED 后重试。 */
    if (!g_adv_started) return 0;
    return adv_apply_payload(connectable);
}

void gy_net_ble_host_stop(void)
{
    g_mode = 0;
    g_pending_host = 0;
    g_adv_stopping = 1;
    /* 不依赖 g_adv_active：update 失败/事件乱序时该标志可能与协议栈
     * 实际状态不一致，漏掉 stop 会让房间继续广播，其它板永远看到幽灵房。 */
    sibles_advertising_stop(g_geyi_adv_ctx);
    sibles_advertising_delete(g_geyi_adv_ctx);
    g_adv_active = 0;
    g_adv_started = 0;
    g_adv_inited = 0;
    g_adv_dirty = 0;
    g_adv_stopping = 0;
}

int gy_net_ble_scan_start(void)
{
    ble_gap_scan_start_t p;
    int ret;
    if (g_scan_active || g_pending_scan) return 0;
    if (!g_power_on) { g_pending_scan = 1; BLE_LOG("scan pending power=0"); return 0; }
    memset(&p, 0, sizeof(p));
    p.own_addr_type = GAPM_STATIC_ADDR;
    p.type = GAPM_SCAN_TYPE_OBSERVER;
    p.dup_filt_pol = 0;
    p.scan_param_1m.scan_intv = 0x30;
    p.scan_param_1m.scan_wd = 0x30;
    p.duration = 0;
    p.period = 0;
    ret = ble_gap_scan_start(&p);
    BLE_LOG("scan_start ret=%d", ret);
    if (ret != HL_ERR_NO_ERROR) {
        /* 扫描停止是异步的；刚离开房间时可能返回 busy。1 s 后由 poll 重试。 */
        g_pending_scan = 1;
        g_scan_retry_at = rt_tick_get() + rt_tick_from_millisecond(1000);
        return -1;
    }
    g_pending_scan = 0;
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
    /* 7.5–10ms 连接间隔：GATT 服务发现要多次往返，120–160ms 会让
     * 加入房间卡 3s。BLE 允许的最小间隔，代价是功耗略高（插电板可接受）。 */
    p.conn_param_1m.conn_intv_min = 0x06;
    p.conn_param_1m.conn_intv_max = 0x08;
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
    int i, found = 0;
    c_lock();
    for (i = 0; i < GY_NET_BLE_MAX_CONN; ++i)
        if (g_conn[i].used) { found = 1; break; }
    c_unlock();
    return found;
}
int gy_net_ble_ready(void)
{
    return (g_client_conn != 0xff && g_remote_svc_ok && g_rx_val_hdl &&
            g_tx_val_hdl && g_tx_cccd_hdl && g_cccd_ok) ? 1 : 0;
}
int gy_net_ble_write_ready(void)
{
    return (g_client_conn != 0xff && g_remote_svc_ok && g_rx_val_hdl) ? 1 : 0;
}
uint8_t gy_net_ble_conn_count(void)
{
    int i, n = 0;
    c_lock();
    for (i = 0; i < GY_NET_BLE_MAX_CONN; ++i) if (g_conn[i].used) n++;
    c_unlock();
    return (uint8_t)n;
}
uint8_t gy_net_ble_conn_at(uint8_t index)
{
    int i, n = 0;
    uint8_t ret = 0xff;
    c_lock();
    for (i = 0; i < GY_NET_BLE_MAX_CONN; ++i)
        if (g_conn[i].used) {
            if (n == index) { ret = g_conn[i].conn_idx; break; }
            n++;
        }
    c_unlock();
    return ret;
}
uint8_t gy_net_ble_client_conn(void)
{
    uint8_t ret;
    c_lock();
    ret = g_client_conn;
    c_unlock();
    return ret;
}
uint16_t gy_net_ble_mtu(uint8_t conn)
{
    GyBleConn *c = conn_find(conn);
    uint16_t ret = 23;
    if (c) { c_lock(); ret = c->mtu; c_unlock(); }
    return ret;
}

int gy_net_ble_local_id(void)
{
    static int cached;
    bd_addr_t addr;
    int id = 0;
    if (cached > 0) return cached;
    /* 有些批次的 NVDS/公共地址是同一个默认值，两块板会显示相同 #ID。
     * 优先用芯片 UID 派生地址（SDK example/ble/* 同样做法），保证唯一；
     * UID 不可读时才退回公共地址。成功后缓存，失败不缓存以便稍后重试。 */
    if (bt_mac_addr_generate_via_uid_v2(&addr) == 0)
        id = ((int)addr.addr[4] << 8) | addr.addr[5];
    else if (bt_mac_addr_generate_via_uid(&addr) == 0)
        id = ((int)addr.addr[4] << 8) | addr.addr[5];
    else if (ble_get_public_address(&addr) == HL_ERR_NO_ERROR)
        id = ((int)addr.addr[4] << 8) | addr.addr[5];
    if (id > 0) cached = id;
    return id;
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
        if (rsp->status == 0 && g_tx_cccd_hdl) {
            sibles_write_remote_value_t v;
            uint8_t cccd[2] = {1, 0};
            int wr;
            v.handle = g_tx_cccd_hdl;
            v.write_type = SIBLES_WRITE;
            v.len = 2;
            v.value = cccd;
            wr = sibles_write_remote_value(g_rmt_svc, rsp->conn_idx, &v);
            BLE_LOG("cccd write conn=%d hdl=%d ret=%d", rsp->conn_idx,
                    g_tx_cccd_hdl, wr);
            if (wr == SIBLES_WRITE_NO_ERR)
                g_cccd_ok = 1;
        }
    }
    return 0;
}

static int geyi_ble_event(uint16_t event_id, uint8_t *data, uint16_t len, uint32_t context)
{
    (void)len; (void)context;
    switch (event_id) {
    case BLE_POWER_ON_IND:
        g_power_on = 1;
        /* 不能在 BLE 事件回调里注册 GATT：sibles_register_svc_128() 会等
         * 同一个事件线程处理注册响应，直接在这里调用会自锁，后续 GAP
         * 事件永远不来。注册推迟到 gy_net_ble_poll()（主线程）。 */
        if (g_pending_host) { g_pending_host = 0; adv_start(g_adv_connectable); }
        if (g_pending_scan) { g_pending_scan = 0; gy_net_ble_scan_start(); }
        BLE_LOG("power on");
        break;
    case BLE_GAP_CONNECTED_IND: {
        ble_gap_connect_ind_t *ind = (ble_gap_connect_ind_t *)data;
        GyBleConn *c;
        if (g_mode == 0) {
            /* 房主已离开/关房，但在停止广播前仍可能收到连接请求；
             * 立即断开，避免客户端连上一个不处理 GATT 的僵尸设备。 */
            BLE_LOG("reject conn idx=%d mode=idle", ind->conn_idx);
            {
                ble_gap_disconnect_t d;
                d.conn_idx = ind->conn_idx;
                d.reason = 0x13;
                ble_gap_disconnect(&d);
            }
            break;
        }
        c = conn_alloc(ind->conn_idx);
        if (c) {
            c->role = ind->role;
            c->addr_type = ind->peer_addr_type;
            memcpy(c->addr, ind->peer_addr.addr, 6);
            if (g_mode == 2) g_client_conn = ind->conn_idx;
            sibles_exchange_mtu(ind->conn_idx);
            BLE_LOG("connected idx=%d role=%d", ind->conn_idx, ind->role);
            if (g_mode == 2)
                g_pending_search_conn = ind->conn_idx;
            /* 不手动 start：adv config 已设 is_auto_restart，协议栈会在
             * 连接/断线后自行恢复广播；手动 start 会叠加实例。 */
        }
        break;
    }
    case BLE_GAP_DISCONNECTED_IND: {
        ble_gap_disconnected_ind_t *ind = (ble_gap_disconnected_ind_t *)data;
        conn_free(ind->conn_idx);
        BLE_LOG("disconnected idx=%d reason=%d", ind->conn_idx, ind->reason);
        /* 同上：交给 is_auto_restart。 */
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
        BLE_LOG("search svc result=%d conn=%d chars=%d uuid_len=%d",
                rsp->result, rsp->conn_idx, rsp->svc ? rsp->svc->char_count : -1,
                rsp->svc ? rsp->svc->uuid_len : -1);
        if (rsp->svc && rsp->svc->char_count) {
            int di;
            BLE_LOG("svc uuid %02x%02x%02x%02x ... hdl %d-%d",
                    rsp->svc->uuid[0], rsp->svc->uuid[1], rsp->svc->uuid[2],
                    rsp->svc->uuid[3], rsp->svc->hdl_start, rsp->svc->hdl_end);
            (void)di;
        }
        if (rsp->result != HL_ERR_NO_ERROR || !rsp->svc) break;
        p = rsp->svc->att_db;
        for (i = 0; i < rsp->svc->char_count; ++i) {
            sibles_svc_search_char_t *ch = (sibles_svc_search_char_t *)p;
            if (ch->uuid_len == ATT_UUID_128_LEN) {
                if (memcmp(ch->uuid, g_rx_uuid, ATT_UUID_128_LEN) == 0 ||
                    memcmp(ch->uuid, g_rx_uuid_be, ATT_UUID_128_LEN) == 0)
                    g_rx_val_hdl = ch->pointer_hdl;
                else if (memcmp(ch->uuid, g_tx_uuid, ATT_UUID_128_LEN) == 0 ||
                         memcmp(ch->uuid, g_tx_uuid_be, ATT_UUID_128_LEN) == 0) {
                    g_tx_val_hdl = ch->pointer_hdl;
                    g_tx_cccd_hdl = sibles_descriptor_handle_find(ch, ATT_DESC_CLIENT_CHAR_CFG);
                }
            }
            p += sizeof(sibles_svc_search_char_t) +
                 ch->desc_count * sizeof(struct sibles_disc_char_desc_ind);
        }
        g_cccd_ok = 0;
        g_remote_svc_ok = 0;
        g_rmt_svc = sibles_register_remote_svc(rsp->conn_idx, rsp->svc->hdl_start,
                                               rsp->svc->hdl_end, geyi_gattc_event);
        g_remote_svc_ok = 1;   /* 返回 0 是合法索引，不能用非零判断 */
        BLE_LOG("chars rx=%d tx=%d cccd=%d rmt=%d hdl=%d-%d", g_rx_val_hdl,
                g_tx_val_hdl, g_tx_cccd_hdl, g_rmt_svc, rsp->svc->hdl_start,
                rsp->svc->hdl_end);
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

void gy_net_ble_poll(void)
{
    if (!g_power_on) return;
    if (!g_service_ready) {
        g_service_ready = 1;
        BLE_LOG("service init");
        ble_service_init();
        BLE_LOG("service init done srv=%d", (int)(uintptr_t)g_srv);
    }
    if (g_pending_search_conn != 0xff) {
        uint8_t conn = g_pending_search_conn;
        g_pending_search_conn = 0xff;
        BLE_LOG("search start conn=%d", conn);
        sibles_search_service(conn, ATT_UUID_128_LEN, (uint8_t *)g_svc_uuid);
    }
    if (g_pending_scan && (int32_t)(rt_tick_get() - g_scan_retry_at) >= 0) {
        g_pending_scan = 0;
        gy_net_ble_scan_start();
    }
    if (g_adv_dirty && g_mode == 1 && g_adv_active && g_adv_started &&
        (int32_t)(rt_tick_get() - g_adv_retry_at) >= 0) {
        g_adv_retry_at = rt_tick_get() + rt_tick_from_millisecond(200);
        adv_apply_payload(g_adv_connectable);
    }
    /* is_auto_restart 正常会自行恢复；若 1.5s 后仍停着，主动重建一次。 */
    if (g_mode == 1 && g_adv_inited && !g_adv_active && g_adv_stopped_at &&
        (int32_t)(rt_tick_get() - g_adv_stopped_at) >= rt_tick_from_millisecond(1500)) {
        g_adv_stopped_at = 0;
        adv_start(g_adv_connectable);
    }
    /* start 命令没人回 ADV_STARTED（事件线程异常）：1.5s 后重建。 */
    if (g_mode == 1 && g_adv_inited && g_adv_active && !g_adv_started &&
        (int32_t)(rt_tick_get() - g_adv_start_at) >= rt_tick_from_millisecond(1500)) {
        adv_start(g_adv_connectable);
    }
}

void gy_net_ble_init(void)
{
    static int inited;
    if (inited) return;
    inited = 1;
    q_init();
    memset(g_conn, 0, sizeof(g_conn));
    memset(g_frag, 0, sizeof(g_frag));
    g_client_conn = 0xff;
    if (rt_sem_init(&g_tx_sem, "gy_net_tx", 0, RT_IPC_FLAG_FIFO) == RT_EOK) {
        g_tx_thread = rt_thread_create("gy_net_tx", tx_thread_entry, RT_NULL, 4096, 22, 10);
        if (g_tx_thread) {
            rt_thread_startup(g_tx_thread);
            g_tx_ready = 1;
        }
    }
    sifli_ble_enable();
    BLE_LOG("local id=%04X", (unsigned)gy_net_ble_local_id());
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
