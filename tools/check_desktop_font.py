"""Check every non-ASCII UI character against the generated CN16 subset."""
from pathlib import Path
import re
import sys
sys.stdout.reconfigure(encoding='utf-8')
root=Path(__file__).resolve().parents[1]
font=(root/'src/geyi_font_cn16.c').read_text(encoding='utf-8')
symbols=re.search(r'\* Glyphs: (.*)',font).group(1)
ui=(root/'src/geyi_ui.c').read_text(encoding='utf-8')
text=''.join(re.findall(r'"([^"\n]*)"',ui))
missing=sorted({c for c in text if ord(c)>127}-set(symbols))
traditional=sorted(set(text)&set('確認開輸勝選擇個請結線態變單預覽紅藍'))
print('Glyph coverage:', 'PASS' if not missing else ''.join(missing))
print('Simplified labels:', 'PASS' if not traditional else ''.join(traditional))
raise SystemExit(bool(missing or traditional))
