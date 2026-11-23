#include "geyi_history_store.h"

#ifdef GEYI_WITH_RTTHREAD

#include "geyi_history.h"
#include "rtthread.h"
#include "flashdb.h"
#include <string.h>

#define GY_HISTORY_KV_PART "history"
#define GY_HISTORY_KV_DB   "geyi_hist"
#define GY_HISTORY_KV_KEY  "records"
#define GY_HISTORY_RETRY_MS 1000u

static struct fdb_kvdb history_db;
static struct rt_mutex history_lock;
static GyHistoryStore history_snapshot;
static int history_ready;
static uint32_t history_last_try;

static void history_lock_cb(fdb_db_t db)
{
    (void)db;
    rt_mutex_take(&history_lock, RT_WAITING_FOREVER);
}

static void history_unlock_cb(fdb_db_t db)
{
    (void)db;
    rt_mutex_release(&history_lock);
}

static int history_store_save(void)
{
    struct fdb_blob blob;
    fdb_err_t err;

    if (!history_ready) return 0;
    if (gy_history_store_pack(&history_snapshot) < 0) return 0;

    if (history_snapshot.count == 0) {
        err = fdb_kv_del(&history_db, GY_HISTORY_KV_KEY);
        /* 键不存在也算清空成功。 */
        return err == FDB_NO_ERR || err == FDB_KV_NAME_ERR;
    }

    err = fdb_kv_set_blob(&history_db, GY_HISTORY_KV_KEY,
                          fdb_blob_make(&blob, &history_snapshot,
                                        sizeof(history_snapshot)));
    return err == FDB_NO_ERR;
}

int gy_history_store_init(void)
{
    struct fdb_blob blob;
    size_t len;
    fdb_err_t err;

    if (history_ready) return 1;

    rt_mutex_init(&history_lock, "gy_hist", RT_IPC_FLAG_FIFO);
    memset(&history_db, 0, sizeof(history_db));
    fdb_kvdb_control(&history_db, FDB_KVDB_CTRL_SET_LOCK,
                     (void *)history_lock_cb);
    fdb_kvdb_control(&history_db, FDB_KVDB_CTRL_SET_UNLOCK,
                     (void *)history_unlock_cb);

    err = fdb_kvdb_init(&history_db, GY_HISTORY_KV_DB,
                        GY_HISTORY_KV_PART, NULL, NULL);
    if (err != FDB_NO_ERR) {
        gy_history_reset();
        rt_kprintf("[geyi:history] store init FAIL rc=%d; RAM fallback\n", (int)err);
        return 0;
    }

    history_ready = 1;
    memset(&blob, 0, sizeof(blob));
    len = fdb_kv_get_blob(&history_db, GY_HISTORY_KV_KEY,
                          fdb_blob_make(&blob, &history_snapshot,
                                        sizeof(history_snapshot)));
    if (len == 0) {
        gy_history_reset();          /* 首次启动：空历史 */
        return 1;
    }

    if (len != sizeof(history_snapshot) ||
        blob.saved.len != sizeof(history_snapshot) ||
        gy_history_store_unpack(&history_snapshot) != 0) {
        rt_kprintf("[geyi:history] store record invalid; clearing\n");
        fdb_kv_del(&history_db, GY_HISTORY_KV_KEY);
        gy_history_reset();
        return 1;
    }

    return 1;
}

void gy_history_store_flush(uint32_t now_ms)
{
    if (!history_ready || !gy_history_dirty()) return;
    if (history_last_try &&
        (uint32_t)(now_ms - history_last_try) < GY_HISTORY_RETRY_MS) return;

    if (history_store_save()) {
        gy_history_mark_clean();
        history_last_try = 0;
    } else {
        history_last_try = now_ms ? now_ms : 1u;
        rt_kprintf("[geyi:history] store save FAIL; retry later\n");
    }
}

int gy_history_store_ready(void)
{
    return history_ready;
}

#else /* !GEYI_WITH_RTTHREAD */

int gy_history_store_init(void) { return 1; }
void gy_history_store_flush(uint32_t now_ms) { (void)now_ms; }
int gy_history_store_ready(void) { return 1; }

#endif
