"""Check every CJK UI character against the SDK font's declared symbols."""
from pathlib import Path
import re
import sys
sys.stdout.reconfigure(encoding='utf-8')
root=Path(__file__).resolve().parents[1]
font=Path(r'D:\sifli\SiFli-SDK\external\lvgl_v9\src\font\lv_font_source_han_sans_sc_16_cjk.c').read_text(encoding='utf-8')
symbols=re.search(r'--symbols (.*?) --font',font).group(1)
ui=(root/'src/geyi_ui.c').read_text(encoding='utf-8')
text=''.join(re.findall(r'"([^"\n]*)"',ui))
missing=sorted({c for c in text if '\u4e00'<=c<='\u9fff'}-set(symbols))
print('CJK coverage:', 'PASS' if not missing else ''.join(missing))
raise SystemExit(bool(missing))
