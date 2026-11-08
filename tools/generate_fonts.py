#!/usr/bin/env python3
"""Regenerate checked-in bitmap fonts using Pillow. Only needed to change typefaces."""
import argparse
import zlib
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
def generate_cjk(path):
 font=ImageFont.truetype(str(path),24)
 if font.getname() != ('Noto Sans CJK SC', 'Medium'):
  raise ValueError('Expected NotoSansCJKsc-Medium.otf')
 data=bytearray()
 for code in range(0x4e00,0xa000):
  im=Image.new('1',(24,32))
  ImageDraw.Draw(im).text((0,26),chr(code),font=font,fill=1,anchor='ls')
  data.extend(packed(im))
 output=ROOT/'main/cjk.z'
 output.write_bytes(zlib.compress(data,9))
 print('Generated', output.stat().st_size, 'bytes of compressed CJK glyphs')

parser=argparse.ArgumentParser(description=__doc__)
mode=parser.add_mutually_exclusive_group()
mode.add_argument('--ascii-only',action='store_true')
mode.add_argument('--cjk-only',action='store_true')
parser.add_argument('--cjk-font',type=Path,default=Path('/usr/share/fonts/opentype/noto/NotoSansCJKsc-Medium.otf'))
args=parser.parse_args()
if args.cjk_only:
 generate_cjk(args.cjk_font)
 raise SystemExit(0)

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
print('Generated ASCII bitmap fonts')
if not args.ascii_only:
 generate_cjk(args.cjk_font)
