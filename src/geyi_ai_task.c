#include "geyi_ai_task.h"
#include <string.h>

#ifdef GEYI_WITH_RTTHREAD
#include "rtthread.h"

static struct rt_semaphore ai_sem;
static struct rt_mutex ai_lock;
static rt_thread_t ai_thread;
static int initialized; /* 1 = ready, -1 = init failed, 0 = not tried */
static GyGame snapshot;
static GyAiConfig config;
static uint32_t job_revision;
static uint32_t job_generation;
static volatile int cancel_flag;
static volatile int running;
static int job_pending;
static GyAiResult result;

static uint32_t board_now_ms(void)
{
    return (uint32_t)((uint64_t)rt_tick_get() * 1000 / RT_TICK_PER_SECOND);
}

static void ai_thread_entry(void *parameter)
{
    (void)parameter;
    for (;;) {
        GyGame g;
        GyAiConfig c;
        uint32_t rev, gen;
        GyAiAction action;
        GyAiStats stats;
        int ok;
        rt_sem_take(&ai_sem, RT_WAITING_FOREVER);
        rt_mutex_take(&ai_lock, RT_WAITING_FOREVER);
        g = snapshot; c = config; rev = job_revision; gen = job_generation;
        job_pending = 0; running = 1;
        rt_mutex_release(&ai_lock);
        c.cancel = &cancel_flag;
        c.now_ms = board_now_ms;
        ok = gy_ai_choose(&g, &c, &action, &stats);
        if (ok)
            rt_kprintf("[geyi:ai] nodes=%u cutoffs=%u ms=%u depth=%d score=%d kind=%d\n",
                       (unsigned)stats.nodes, (unsigned)stats.cutoffs,
                       (unsigned)stats.elapsed_ms, stats.depth, action.score, action.kind);
        else
            rt_kprintf("[geyi:ai] no action or cancelled\n");
        rt_mutex_take(&ai_lock, RT_WAITING_FOREVER);
        running = 0;
        memset(&result, 0, sizeof(result));
        if (ok) {
            result.action = action;
            result.stats = stats;
        }
        result.revision = rev;
        result.generation = gen;
        result.ready = 1;
        rt_mutex_release(&ai_lock);
    }
}

static int ensure_initialized(void)
{
    if (initialized > 0) return 0;
    if (initialized < 0) return -1;
    if (rt_mutex_init(&ai_lock, "gy_ai_lock", RT_IPC_FLAG_PRIO) != RT_EOK) {
        initialized = -1; return -1;
    }
    if (rt_sem_init(&ai_sem, "gy_ai_sem", 0, RT_IPC_FLAG_PRIO) != RT_EOK) {
        initialized = -1; return -1;
    }
    ai_thread = rt_thread_create("gy_ai", ai_thread_entry, RT_NULL, 16384, 20, 10);
    if (!ai_thread) { initialized = -1; return -1; }
    rt_thread_startup(ai_thread);
    initialized = 1;
    return 0;
}

int gy_ai_task_start(const GyGame *g, const GyAiConfig *cfg,
                     uint32_t revision, uint32_t generation)
{
    if (!g || !cfg || ensure_initialized() != 0) return -1;
    rt_mutex_take(&ai_lock, RT_WAITING_FOREVER);
    if (running || job_pending) {
        if (running) cancel_flag = 1;
        rt_mutex_release(&ai_lock);
        return -1;
    }
    snapshot = *g;
    config = *cfg;
    job_revision = revision;
    job_generation = generation;
    cancel_flag = 0;
    job_pending = 1;
    result.ready = 0;
    rt_mutex_release(&ai_lock);
    rt_sem_release(&ai_sem);
    return 0;
}

int gy_ai_task_poll(GyAiResult *out)
{
    int ready;
    if (!out || initialized <= 0) return 0;
    rt_mutex_take(&ai_lock, RT_WAITING_FOREVER);
    ready = result.ready;
    if (ready) {
        *out = result;
        result.ready = 0;
    }
    rt_mutex_release(&ai_lock);
    return ready;
}

void gy_ai_task_cancel(void)
{
    if (initialized <= 0) return;
    rt_mutex_take(&ai_lock, RT_WAITING_FOREVER);
    cancel_flag = 1;
    rt_mutex_release(&ai_lock);
}

#else /* desktop host fallback: synchronous, poll on the next UI tick */

#include <time.h>

static GyAiResult result;
static int pending;

static uint32_t host_now_ms(void)
{
    return (uint32_t)((uint64_t)clock() * 1000 / CLOCKS_PER_SEC);
}

int gy_ai_task_start(const GyGame *g, const GyAiConfig *cfg,
                     uint32_t revision, uint32_t generation)
{
    GyAiConfig c;
    if (!g || !cfg) return -1;
    c = *cfg;
    c.now_ms = host_now_ms;
    c.cancel = NULL;
    memset(&result, 0, sizeof(result));
    result.revision = revision;
    result.generation = generation;
    if (!gy_ai_choose(g, &c, &result.action, &result.stats))
        memset(&result.action, 0, sizeof(result.action));
    result.ready = 1;
    pending = 1;
    return 0;
}

int gy_ai_task_poll(GyAiResult *out)
{
    if (!out || !pending) return 0;
    *out = result;
    pending = 0;
    return 1;
}

void gy_ai_task_cancel(void)
{
    pending = 0;
}

#endif
