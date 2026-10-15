# 界面简体字库

Geyi UI CN16为本项目界面使用的16像素4bpp位图子集，由
`tools/generate_ui_font.py` 从SiFli-SDK v2.5.1随附的
`external/lvgl_v9/scripts/built_in_font/SourceHanSansSC-Normal.otf`生成。
源字体为Adobe思源黑体SC，Copyright 2014–2021 Adobe。
保留的SIL OFL 1.1许可见同目录OFL-SourceHanSansSC.txt。

本项目修改：仅抽取界面所需66个汉字、栅格化并转换为LVGL原生C资源，
派生字体更名为Geyi UI CN16；不分发完整OTF，不以Windows字体替代源字体。
源字体SHA256、完整字符清单均写在src/geyi_font_cn16.c头部。
非子集字符回退到SDK原有16像素字库。生成脚本--check可验证资源一致性。
