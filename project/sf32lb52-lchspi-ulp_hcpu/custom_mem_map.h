/*
 * SPDX-FileCopyrightText: 2019-2022 SiFli Technologies(Nanjing) Co., Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __CUSTOM_MEM_MAP__
#define __CUSTOM_MEM_MAP__

#ifdef USING_PARTITION_TABLE
    #include "ptab.h"
#endif /* USING_PARTITION_TABLE */

/* 工程级 HCPU FAL 表：保留板级 dfu/ble，追加 game history 分区。
 * 32 KB = 4 个 flash2 擦除块（每块 8 KB），供 FlashDB KV 做 GC/掉电恢复。 */
#define FAL_PART_TABLE \
{ \
    {FAL_PART_MAGIC_WORD,       "dfu",      NOR_FLASH2_DEV_NAME,    KVDB_DFU_REGION_OFFSET,     KVDB_DFU_REGION_SIZE,     0}, \
    {FAL_PART_MAGIC_WORD,       "ble",      NOR_FLASH2_DEV_NAME,    KVDB_BLE_REGION_OFFSET,     KVDB_BLE_REGION_SIZE,     0}, \
    {FAL_PART_MAGIC_WORD,       "history",  NOR_FLASH2_DEV_NAME,    KVDB_HISTORY_REGION_OFFSET, KVDB_HISTORY_REGION_SIZE, 0}, \
}

#endif  /* __MEM_MAP__ */
