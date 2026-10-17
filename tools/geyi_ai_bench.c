#include "geyi_ai.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint32_t now_ms(void)
{
    return (uint32_t)((uint64_t)clock() * 1000 / CLOCKS_PER_SEC);
}

int main(int argc, char **argv)
{
    int depth = 4, pruning = 1, i;
    uint32_t seed = 20261015;
    GyGame g;
    GyAiConfig c;
    GyAiAction a;
    GyAiStats s;
    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--no-pruning")) pruning = 0;
        else if (!strcmp(argv[i], "--depth") && i + 1 < argc) depth = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--seed") && i + 1 < argc)
            seed = (uint32_t)strtoul(argv[++i], NULL, 10);
        else {
            fprintf(stderr, "usage: geyi-ai-bench [--depth N] [--no-pruning] [--seed N]\n");
            return 2;
        }
    }
    if (!gy_init(&g, gy_default_config(), seed)) return 2;
    c = gy_ai_default_config();
    c.max_depth = depth;
    c.use_pruning = pruning;
    c.use_iterative = 0;
    c.time_limit_ms = 0;
    c.node_limit = 0;
    c.now_ms = now_ms;
    c.cancel = NULL;
    if (!gy_ai_choose(&g, &c, &a, &s)) {
        printf("no action\n");
        return 1;
    }
    printf("pruning=%d depth=%d seed=%u nodes=%llu cutoffs=%llu elapsed_ms=%u score=%d ",
           pruning, depth, seed, (unsigned long long)s.nodes,
           (unsigned long long)s.cutoffs, s.elapsed_ms, a.score);
    if (a.kind == GY_AI_MOVE)
        printf("action=move direction=%d count=%d\n", a.move.direction, a.move.count);
    else
        printf("action=place column=%d\n", a.column);
    return 0;
}
