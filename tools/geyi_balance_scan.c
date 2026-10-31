/*
 * geyi_balance_scan.c —— REQ-411 命令行入口
 *
 * 用法：geyi-balance-scan [--groups N] [--games N] [--out file.csv]
 * 默认 20 组 × 每组 50 局；固定种子，同一二进制重复运行结果一致。
 */
#include "geyi_balance.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    int groups = gy_balance_group_count();
    int games = 50;
    const char *out_path = "balance_scan.csv";
    char *csv;
    FILE *fp;
    int i;
    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--groups") && i + 1 < argc) groups = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--games") && i + 1 < argc) games = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--out") && i + 1 < argc) out_path = argv[++i];
        else {
            fprintf(stderr, "usage: %s [--groups N] [--games N] [--out file.csv]\n", argv[0]);
            return 2;
        }
    }
    csv = (char *)calloc(1, 65536);
    if (!csv) return 2;
    if (gy_balance_scan_csv(groups, games, csv, 65536) != 0) {
        fprintf(stderr, "scan failed (groups=%d games=%d)\n", groups, games);
        free(csv);
        return 1;
    }
    fp = fopen(out_path, "wb");
    if (!fp) {
        fprintf(stderr, "cannot write %s\n", out_path);
        free(csv);
        return 1;
    }
    fputs(csv, fp);
    fclose(fp);
    printf("wrote %s (%d groups x %d games)\n", out_path, groups, games);
    free(csv);
    return 0;
}
