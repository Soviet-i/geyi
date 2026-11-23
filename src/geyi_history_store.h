#ifndef GEYI_HISTORY_STORE_H
#define GEYI_HISTORY_STORE_H
#include <stdint.h>

/* 板端历史持久化适配器：只由固件构建编译，桌面/主机不依赖 FlashDB。 */
int gy_history_store_init(void);
void gy_history_store_flush(uint32_t now_ms);
int gy_history_store_ready(void);

#endif
