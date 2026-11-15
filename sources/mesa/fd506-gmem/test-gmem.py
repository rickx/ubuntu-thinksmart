#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compile production FDL/GMEM expressions and layout code against captured inputs."""
import pathlib
import re
import subprocess
import sys
import tempfile

root = pathlib.Path(sys.argv[1])
gmem = (root / 'src/gallium/drivers/freedreno/freedreno_gmem.c').read_text()
fdl = (root / 'src/freedreno/fdl/fd5_layout.c').read_text()
start = gmem.index('static bool\nlayout_gmem(')
end = gmem.index('\nstatic void\ncalc_nbins(', start)
layout_function = gmem[start:end]
assignments = '\n'.join(re.findall(r'key->zsbuf_cpp\[[01]\] =\s*[^;]+;', gmem))
fdl_cpp = '\n'.join(re.findall(r'layout->cpp (?:=|\*=) [^;]+;', fdl))
source = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define MAX_RENDER_TARGETS 8
#define DIV_ROUND_UP(a,b) (((a)+(b)-1)/(b))
static unsigned util_align_npot(unsigned a,unsigned b) { return DIV_ROUND_UP(a,b)*b; }
static unsigned div_align(unsigned a,unsigned b,unsigned c) { return util_align_npot(DIV_ROUND_UP(a,b),c); }
/* Format values encode bytes/sample, sufficient for these uncompressed formats. */
static unsigned util_format_get_blocksize(unsigned format) { return format; }
struct info { unsigned tile_align_w,tile_align_h,tile_max_w,tile_max_h; };
struct fd_screen { struct info *info; unsigned gmemsize_bytes; };
struct gmem_key { unsigned width,height,gmem_page_align; unsigned cbuf_cpp[8],zsbuf_cpp[2]; };
struct fd_gmem_stateobj { struct fd_screen *screen; unsigned bin_w,bin_h,nbins_x,nbins_y,cbuf_base[8],zsbuf_base[2]; };
struct resource { struct { unsigned cpp,format; } layout; struct resource *stencil; };
'''
source += layout_function
source += '\nint main(void) {\n'
source += r'''
struct info info={64,32,1024,4064};
struct fd_screen screen={&info,0x22000};
for (unsigned samples=1;samples<=4;samples*=2) {
 for (unsigned split=0;split<2;split++) {
  struct { unsigned format,nr_samples; } config={4,samples}, *params=&config;
  struct resource depth={0},stencil={0},*rsc=&depth;
  __typeof__(depth.layout) *layout=&depth.layout;
  layout->format=params->format;
'''
source += fdl_cpp
source += r'''
  stencil.layout.format=1;stencil.layout.cpp=samples;
  if (split) depth.stencil=&stencil;
  struct { unsigned samples; } fb={samples},*pfb=&fb;
  struct gmem_key value={.width=128,.height=32,.gmem_page_align=4},*key=&value;
  key->cbuf_cpp[0]=samples; /* A8 multisample framebuffer captured on FD506. */
'''
source += assignments
source += r'''
  if (!split) key->zsbuf_cpp[1]=0;
  assert(key->zsbuf_cpp[0]==4*samples);
  if (split) assert(key->zsbuf_cpp[1]==samples);
  struct fd_gmem_stateobj out={.screen=&screen};
  assert(layout_gmem(key,2,1,&out));
  assert(out.nbins_x==2 && out.nbins_y==1);
  unsigned color_end=out.cbuf_base[0]+out.bin_w*out.bin_h*key->cbuf_cpp[0];
  assert(out.zsbuf_base[0]>=color_end);
  unsigned depth_end=out.zsbuf_base[0]+out.bin_w*out.bin_h*key->zsbuf_cpp[0];
  assert(depth_end<=screen.gmemsize_bytes);
  if (split) {
   assert(out.zsbuf_base[1]>=depth_end);
   assert(out.zsbuf_base[1]+out.bin_w*out.bin_h*samples<=screen.gmemsize_bytes);
  }
  assert(layout_gmem(key,1,1,&out));
 }
}
puts("PASS: production MSAA 1/2/4x, packed/split stencil, FD506 GMEM capacity and non-overlap");
}
'''
# A null separate-stencil resource is not dereferenced in production either.
source = source.replace('key->zsbuf_cpp[1] =', 'if (split) key->zsbuf_cpp[1] =')
with tempfile.TemporaryDirectory(prefix='gmem-test-') as work:
    c = pathlib.Path(work) / 'regression.c'
    exe = pathlib.Path(work) / 'regression'
    c.write_text(source)
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', str(c), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=3)
