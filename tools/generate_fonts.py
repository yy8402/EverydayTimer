#!/usr/bin/env python3
"""Regenerate checked-in bitmap fonts using Pillow. Only needed to change typefaces."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
ROOT=Path(__file__).resolve().parents[1]
base=Path('/usr/share/fonts/truetype/dejavu')
specs=[('small',10,'DejaVuSans.ttf'),('status',11,'DejaVuSans.ttf'),('best',17,'DejaVuSansMono-Bold.ttf'),('title',28,'DejaVuSans-Bold.ttf'),('unit',30,'DejaVuSans.ttf'),('digits',88,'DejaVuSansMono-Bold.ttf'),('medium_digits',64,'DejaVuSansMono-Bold.ttf'),('compact',48,'DejaVuSansMono-Bold.ttf'),('small_digits',32,'DejaVuSansMono-Bold.ttf'),('tiny_digits',24,'DejaVuSansMono-Bold.ttf')]
def packed(im):
 out=bytearray();bits=list(im.getdata())
 for pos in range(0,len(bits),8):
  b=0
  for i,p in enumerate(bits[pos:pos+8]):
   if p:b|=1<<(7-i)
  out.append(b)
 return out
h=['// Generated bitmap font data. DejaVu font license in third_party/.','#pragma once','#include <cstdint>','struct Glyph{uint32_t offset;uint8_t w,h,advance;int8_t x,y;};','struct Font{const Glyph* glyphs;const uint8_t* bits;};']
for name,size,path in specs:
 font=ImageFont.truetype(str(base/path),size);data=bytearray();glyphs=[]
 for code in range(32,127):
  c=chr(code);x0,y0,x1,y1=font.getbbox(c,anchor='ls');w=max(1,x1-x0);ht=max(1,y1-y0)
  im=Image.new('1',(w,ht));ImageDraw.Draw(im).text((-x0,-y0),c,font=font,fill=1,anchor='ls',stroke_width=0)
  glyphs.append((len(data),w,ht,round(font.getlength(c)),x0,y0));data.extend(packed(im))
 h.append('static const uint8_t bits_%s[]={%s};'%(name,','.join(map(str,data))))
 h.append('static const Glyph glyphs_%s[]={%s};'%(name,','.join('{'+','.join(map(str,g))+'}' for g in glyphs)))
 h.append('static const Font font_%s={glyphs_%s,bits_%s};'%(name,name,name))
(ROOT/'main/fonts.h').write_text('\n'.join(h)+'\n')
import sys
if '--ascii-only' in sys.argv:
 print('Generated ASCII bitmap fonts')
 raise SystemExit(0)
font=ImageFont.truetype('/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc',24,index=2)
with (ROOT/'main/cjk.bin').open('wb') as f:
 for code in range(0x4e00,0xa000):
  im=Image.new('1',(24,32));ImageDraw.Draw(im).text((0,26),chr(code),font=font,fill=1,anchor='ls');f.write(packed(im))
print('Generated ASCII fonts and', (ROOT/'main/cjk.bin').stat().st_size,'byte basic CJK font')
