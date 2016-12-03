/* Copyright © 2016 Bill Allombert

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  Check the License for details. You should have received a copy of it, along
  with the package; see the file 'COPYING'. If not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/tiocl.h>
#include <stdint.h>

#include "consolation.h"

void
set_screen_size(void)
{
  struct winsize s;
  int fd = open("/dev/tty0",O_RDONLY);
  if (ioctl(fd, TIOCGWINSZ, &s)) perror("TIOCGWINSZ");
  close(fd);
  screen_width  = s.ws_col;
  screen_height = s.ws_row;
}

static void
linux_selection(int xs, int ys, int xe, int ye, int sel_mode)
{
  int fd;
  struct {
    char argp[2]; /*Force struct alignment*/
    struct tiocl_selection sel;
  } s;
  s.argp[0] = 0; /* unused */
  s.argp[1] = TIOCL_SETSEL;
  s.sel.xs = xs;
  s.sel.ys = ys;
  s.sel.xe = xe;
  s.sel.ye = ye;
  s.sel.sel_mode = sel_mode;
  fd = open("/dev/tty0",O_RDONLY);
  if(ioctl(fd, TIOCLINUX, ((char*)&s)+1)<0)
    perror("selection: TIOCLINUX");
  close(fd);
}

void
draw_pointer(int x, int y)
{
  linux_selection(x, y, x, y, TIOCL_SELPOINTER);
}

void
select_region(int x, int y, int x2, int y2)
{
  if (x2 < 0) x2 = x;
  if (y2 < 0) y2 = y;
  linux_selection(x, y, x2, y2, TIOCL_SELCHAR);
}

void
select_word(int x, int y)
{
  linux_selection(x, y, x, y, TIOCL_SELWORD);
}

void
select_line(int x, int y)
{
  linux_selection(x, y, x, y, TIOCL_SELLINE);
}

void paste(void)
{
  int fd;
  char subcode = TIOCL_PASTESEL;
  fd = open("/dev/tty0",O_RDWR);
  if(ioctl(fd, TIOCLINUX, &subcode)<0)
    perror("paste: TIOCLINUX");
  close(fd);
}

void scroll(int sc)
{
  int fd;
  struct {
    char subcode[4];
    int sc;
  } scr;
  scr.subcode[0] = TIOCL_SCROLLCONSOLE;
  scr.subcode[1] = 0;
  scr.subcode[2] = 0;
  scr.subcode[3] = 0;
  scr.sc = sc;
  fd = open("/dev/tty0",O_RDONLY);
  if(ioctl(fd, TIOCLINUX, &scr)<0)
    perror("scroll: TIOCLINUX");
  close(fd);
}

static int goodchar(char x)
{
  return x >= 0x20 && x < 0x7f;
}

void set_lut(const char *def)
{
  int fd;
  struct {
    char subcode;
    char padding[3];
    uint32_t lut[8];
  } l = { TIOCL_SELLOADLUT, 0, 0, 0,
    0x00000000, /* control chars     */
    0x03FFE000, /* digits and "-./"  */
    0x87FFFFFE, /* uppercase and '_' */
    0x07FFFFFE, /* lowercase         */
    0x00000000,
    0x00000000,
    0xFF7FFFFF, /* latin-1 accented letters, not multiplication sign */
    0xFF7FFFFF  /* latin-1 accented letters, not division sign */
  }; /* all of Unicode above U+00FF is considered "word" chars, even
        frames and the likes */

  /* we allow changing only U+0020..U+7E */
  l.lut[1] = l.lut[2] = l.lut[3] = 0;

  while (*def) {
    char c = *def++;
    if (!goodchar(c))
      continue;
    if (*def == '-' && goodchar(def[1])) {
      ++def;
      for (; c <= *def; ++c)
        l.lut[c >> 5] |= 1 << (uint32_t)(c & 31);
      ++def;
    }
    else
      l.lut[c >> 5] |= 1 << (uint32_t)(c & 31);
  }

  fd = open("/dev/tty0",O_RDWR);
  if(ioctl(fd, TIOCLINUX, &l)<0)
    perror("set_lut: TIOCLINUX");
  close(fd);
}
