"""Generate reproducible Geyi UI CN16 from the SDK's OFL-licensed Source Han font.

Only CJK glyphs used by UI literals are embedded. ASCII/icons use SDK fallback.
Run with --check to verify the generated C resource, not to modify it.
"""
from pathlib import Path
import hashlib
import os
import re
import sys
from PIL import Image, ImageDraw, ImageFont

sys.stdout.reconfigure(encoding='utf-8')
root=Path(__file__).resolve().parents[1]
sdk=Path(os.environ.get('SIFLI_SDK',r'D:\sifli\SiFli-SDK'))
source=sdk/'external/lvgl_v9/scripts/built_in_font/SourceHanSansSC-Normal.otf'
ui=(root/'src/geyi_ui.c').read_text(encoding='utf-8')
text=''.join(re.findall(r'"([^"\n]*)"',ui))
characters=sorted({c for c in text if '\u4e00'<=c<='\u9fff'})
font=ImageFont.truetype(str(source),16)
notdef=font.getmask(chr(0x10ffff))
missing=[c for c in characters if font.getmask(c).size==notdef.size and bytes(font.getmask(c))==bytes(notdef)]
if missing:raise SystemExit('Source font missing: '+''.join(missing))
bitmap=[]
descriptors=['    {0,0,0,0,0,0}, /* reserved */']
for ch in characters:
    left,top,right,bottom=font.getbbox(ch,anchor='ls')
    w,h=right-left,bottom-top
    image=Image.new('L',(w,h),0)
    ImageDraw.Draw(image).text((-left,-top),ch,font=font,fill=255,anchor='ls')
    pixels=[(value*15+127)//255 for value in image.tobytes()]
    if len(pixels)%2:pixels.append(0)
    start=len(bitmap)
    bitmap.extend((pixels[i]<<4)|pixels[i+1] for i in range(0,len(pixels),2))
    descriptors.append(f'    {{{start},{round(font.getlength(ch)*16)},{w},{h},{left},{-bottom}}}, /* U+{ord(ch):04X} */')
first=ord(characters[0])
lines=[
'/* Generated Geyi UI CN16. Font derivative under SIL OFL 1.1.',
' * Copyright 2014-2021 Adobe. See assets/fonts/OFL-SourceHanSansSC.txt.',
' * SDK source: external/lvgl_v9/scripts/built_in_font/SourceHanSansSC-Normal.otf',
' * source_sha256: '+hashlib.sha256(source.read_bytes()).hexdigest(),
' * CJK: '+''.join(characters),
' * Primary derivative name: Geyi UI CN16. */',
'#ifdef GEYI_WITH_LVGL',
'#include "geyi_font_cn16.h"',
'static const uint8_t bitmap[] = {']
for i in range(0,len(bitmap),16):lines.append('    '+','.join(f'0x{x:02x}' for x in bitmap[i:i+16])+',')
lines+=['};','static const lv_font_fmt_txt_glyph_dsc_t glyphs[] = {',*descriptors,'};',
'static const uint16_t unicode_list[] = {',
'    '+','.join(str(ord(c)-first) for c in characters),'};',
'static const lv_font_fmt_txt_cmap_t maps[] = {',
f'    {{.range_start={first},.range_length={ord(characters[-1])-first+1},.glyph_id_start=1,',
f'     .unicode_list=unicode_list,.glyph_id_ofs_list=NULL,.list_length={len(characters)},',
'     .type=LV_FONT_FMT_TXT_CMAP_SPARSE_TINY}', '};',
'static const lv_font_fmt_txt_dsc_t data = {',
'    .glyph_bitmap=bitmap,.glyph_dsc=glyphs,.cmaps=maps,.kern_dsc=NULL,',
'    .kern_scale=0,.cmap_num=1,.bpp=4,.kern_classes=0,.bitmap_format=0,.stride=0','};',
'const lv_font_t geyi_font_cn16 = {',
'    .get_glyph_dsc=lv_font_get_glyph_dsc_fmt_txt,.get_glyph_bitmap=lv_font_get_bitmap_fmt_txt,',
'    .line_height=20,.base_line=5,.subpx=LV_FONT_SUBPX_NONE,',
'    .underline_position=-2,.underline_thickness=1,.dsc=&data,',
'    .fallback=&lv_font_source_han_sans_sc_16_cjk','};','#endif','']
content='\n'.join(lines)
target=root/'src/geyi_font_cn16.c'
if '--check' in sys.argv:
    if not target.exists() or target.read_text(encoding='utf-8')!=content:raise SystemExit('Font resource is stale; regenerate first.')
    print('Generated font consistency PASS:',len(characters),'CJK glyphs,',len(bitmap),'bitmap bytes')
else:
    target.write_text(content,encoding='utf-8')
    print('Generated:',len(characters),'CJK glyphs,',len(bitmap),'bitmap bytes')
