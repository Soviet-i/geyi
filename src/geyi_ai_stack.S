/* AI 线程栈：放在 PSRAM，必须是 NOBITS（不产生可加载数据）。
 * 若用普通 C 数组 + section 属性，链接器会把 .RW_PSRAM1 标成 PROGBITS/LOAD，
 * objcopy 会从 Flash 0x12020000 填到 PSRAM 0x60000000，生成约 1.3 GB 的
 * main.bin，sftool 烧录会超时。 */
    .section .l2_ret_bss_ai_stack,"aw",%nobits
    .align 3
    .global geyi_ai_stack
    .type geyi_ai_stack, %object
    .size geyi_ai_stack, 16384
geyi_ai_stack:
    .space 16384
