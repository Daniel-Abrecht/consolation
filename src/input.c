/* Adpated from test/event-debug.c from the libinput distribution */
/* test/event-debug.c 1.3.3:
 * Copyright © 2014 Red Hat, Inc.
 * Modifications: Copyright © 2016 Bill Allombert <ballombe@debian.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */


#define _GNU_SOURCE
#include <errno.h>
#include <inttypes.h>
#include <fcntl.h>
#include <getopt.h>
#include <poll.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <linux/input.h>
#include <sys/ioctl.h>

#include <libinput.h>
#include "config.h"
#include "shared.h"
#include "consolation.h"

static struct tools_options options;
static unsigned int stop = 0;
static enum tools_backend backend = BACKEND_UDEV;
static const char *seat_or_device = "seat0";
static bool grab = false;
static bool verbose = false;
static const char *word_chars = NULL;

static void
handle_motion_event(struct libinput_event *ev)
{
  struct libinput_event_pointer *p = libinput_event_get_pointer_event(ev);
  double x = libinput_event_pointer_get_dx(p);
  double y = libinput_event_pointer_get_dy(p);
  move_pointer(x, y);
}

static void
handle_absmotion_event(struct libinput_event *ev)
{
  struct libinput_event_pointer *p = libinput_event_get_pointer_event(ev);
  double x = libinput_event_pointer_get_absolute_x_transformed(p, screen_width);
  double y = libinput_event_pointer_get_absolute_y_transformed(p, screen_height);
  set_pointer(x, y);
}

static void
handle_pointer_button_event(struct libinput_event *ev)
{
  struct libinput_event_pointer *p = libinput_event_get_pointer_event(ev);
  enum libinput_button_state state;
  int button;
  button = libinput_event_pointer_get_button(p);
  state = libinput_event_pointer_get_button_state(p);
  switch(button)
  {
  case BTN_LEFT:
    if (state==LIBINPUT_BUTTON_STATE_PRESSED)
      press_left_button();
    else
      release_left_button();
    break;
  case BTN_MIDDLE:
    if (state==LIBINPUT_BUTTON_STATE_PRESSED)
      press_middle_button();
    break;
  case BTN_RIGHT:
    if (state==LIBINPUT_BUTTON_STATE_PRESSED)
      press_right_button();
    break;
  }
}

static void
handle_pointer_axis_event(struct libinput_event *ev)
{
  struct libinput_event_pointer *p = libinput_event_get_pointer_event(ev);
  double v = 0;
  if (libinput_event_pointer_has_axis(p,
        LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL))
  {
    v = libinput_event_pointer_get_axis_value(p,
        LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL);
    vertical_axis(v);
  }
}

static void
handle_touch_event_down(struct libinput_event *ev)
{
  struct libinput_event_touch *t = libinput_event_get_touch_event(ev);
  double x = libinput_event_touch_get_x_transformed(t, screen_width);
  double y = libinput_event_touch_get_y_transformed(t, screen_height);
  set_pointer(x, y);
  press_left_button();
}

static void
handle_touch_event_motion(struct libinput_event *ev)
{
  struct libinput_event_touch *t = libinput_event_get_touch_event(ev);
  double x = libinput_event_touch_get_x_transformed(t, screen_width);
  double y = libinput_event_touch_get_y_transformed(t, screen_height);
  set_pointer(x, y);
}

static void
handle_touch_event_up(struct libinput_event *ev)
{
  release_left_button();
}

static int
handle_events(struct libinput *li)
{
  int rc = -1;
  struct libinput_event *ev;

  libinput_dispatch(li);
  set_screen_size();
  while ((ev = libinput_get_event(li))) {

    switch (libinput_event_get_type(ev)) {
    case LIBINPUT_EVENT_NONE:
      abort();
    case LIBINPUT_EVENT_DEVICE_ADDED:
    case LIBINPUT_EVENT_DEVICE_REMOVED:
      tools_device_apply_config(libinput_event_get_device(ev),
          &options);
      break;
    case LIBINPUT_EVENT_POINTER_MOTION:
      handle_motion_event(ev);
      break;
    case LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE:
      handle_absmotion_event(ev);
      break;
    case LIBINPUT_EVENT_POINTER_BUTTON:
      handle_pointer_button_event(ev);
      break;
    case LIBINPUT_EVENT_POINTER_AXIS:
      handle_pointer_axis_event(ev);
      break;
    case LIBINPUT_EVENT_TOUCH_DOWN:
      handle_touch_event_down(ev);
      break;
    case LIBINPUT_EVENT_TOUCH_MOTION:
      handle_touch_event_motion(ev);
      break;
    case LIBINPUT_EVENT_TOUCH_UP:
      handle_touch_event_up(ev);
      break;
    default:
      break;
    }
    libinput_event_destroy(ev);
    libinput_dispatch(li);
    rc = 0;
  }
  return rc;
}

static void
sighandler(int signal, siginfo_t *siginfo, void *userdata)
{
  stop = 1;
}

static void
mainloop(struct libinput *li)
{
  struct pollfd fds;
  struct sigaction act;

  fds.fd = libinput_get_fd(li);
  fds.events = POLLIN;
  fds.revents = 0;

  memset(&act, 0, sizeof(act));
  act.sa_sigaction = sighandler;
  act.sa_flags = SA_SIGINFO;

  if (sigaction(SIGINT, &act, NULL) == -1) {
    fprintf(stderr, "Failed to set up signal handling (%s)\n",
        strerror(errno));
    return;
  }

  /* Handle already-pending device added events */
  if (handle_events(li))
    fprintf(stderr, "Expected device added events on startup but got none. "
        "Maybe you don't have the right permissions?\n");

  while (!stop && poll(&fds, 1, -1) > -1)
    handle_events(li);
}

void
usage(void)
{
  printf("Usage: %s [options] [--udev [<seat>]|--device /dev/input/event0]\n"
         "--udev <seat>.... Use udev device discovery (default).\n"
         "                  Specifying a seat ID is optional.\n"
         "--device /path/to/device .... open the given device only\n"
         "\n"
         "Features:\n"
         "--enable-tap\n"
         "--disable-tap.... enable/disable tapping\n"
         "--enable-drag\n"
         "--disable-drag.... enable/disable tap-n-drag\n"
         "--enable-drag-lock\n"
         "--disable-drag-lock.... enable/disable tapping drag lock\n"
         "--enable-natural-scrolling\n"
         "--disable-natural-scrolling.... enable/disable natural scrolling\n"
         "--enable-left-handed\n"
         "--disable-left-handed.... enable/disable left-handed button configuration\n"
         "--enable-middlebutton\n"
         "--disable-middlebutton.... enable/disable middle button emulation\n"
         "--enable-dwt\n"
         "--disable-dwt..... enable/disable disable-while-typing\n"
         "--set-click-method=[none|clickfinger|buttonareas] .... set the desired click method\n"
         "--set-scroll-method=[none|twofinger|edge|button] ... set the desired scroll method\n"
         "--set-scroll-button=BTN_MIDDLE ... set the button to the given button code\n"
         "--set-profile=[adaptive|flat].... set pointer acceleration profile\n"
         "--set-speed=<value>.... set pointer acceleration speed (allowed range [-1, 1]) \n"
         "--set-tap-map=[lrm|lmr] ... set button mapping for tapping\n"
         "\n"
         "These options apply to all applicable devices, if a feature\n"
         "is not explicitly specified it is left at each device's default.\n"
         "\n"
         "Other options:\n"
         "--word-chars=<string>.... List of characters that make up words.\n"
         "                          Ranges (a-z, A-Z, 0-9 etc.) are allowed.\n"
         "--grab .......... Exclusively grab all opened devices.\n"
         "--verbose ....... Print debugging output.\n"
         "--version ....... Print version information.\n"
         "--help .......... Print this help.\n",
         program_invocation_short_name);
}

static void
version(void)
{
  printf("%s %s\n", PACKAGE_NAME, PACKAGE_VERSION);
}

static int
parse_args(int argc, char **argv)
{
  tools_init_options(&options);

  while (1) {
    int c;
    int option_index = 0;
    enum {
      OPT_DEVICE = 1,
      OPT_UDEV,
      OPT_GRAB,
      OPT_HELP,
      OPT_VERBOSE,
      OPT_VERSION,
      OPT_WORD_CHARS
    };
    static struct option opts[] = {
      CONFIGURATION_OPTIONS,
      { "help",                      no_argument,       0, 'h' },
      { "device",                    required_argument, 0, OPT_DEVICE },
      { "udev",                      required_argument, 0, OPT_UDEV },
      { "grab",                      no_argument,       0, OPT_GRAB },
      { "verbose",                   no_argument,       0, OPT_VERBOSE },
      { "version",                   no_argument,       0, OPT_VERSION },
      { "word-chars",                required_argument, 0, OPT_WORD_CHARS },
      { 0, 0, 0, 0}
    };

    c = getopt_long(argc, argv, "h", opts, &option_index);
    if (c == -1)
      break;

    switch(c) {
    case '?':
      exit(1);
      break;
    case 'h':
      usage();
      exit(0);
      break;
    case OPT_VERSION:
      version();
      exit(0);
      break;
    case OPT_DEVICE:
      backend = BACKEND_DEVICE;
      seat_or_device = optarg;
      break;
    case OPT_UDEV:
      backend = BACKEND_UDEV;
      seat_or_device = optarg;
      break;
    case OPT_GRAB:
      grab = true;
      break;
    case OPT_VERBOSE:
      verbose = true;
      break;
    case OPT_WORD_CHARS:
      word_chars = optarg;
      break;
    default:
      if (tools_parse_option(c, optarg, &options) != 0) {
        usage();
        return 1;
      }
      break;
    }
  }
  if (optind < argc) {
    usage();
    return 1;
  }
  return 0;
}


int
event_init(int argc, char **argv)
{
  tools_init_options(&options);
  return parse_args(argc, argv);
}

int
event_main(void)
{
  struct libinput *li;

  set_lut(word_chars);
  li = tools_open_backend(backend, seat_or_device, verbose, grab);
  if (!li)
    return 1;

  mainloop(li);

  libinput_unref(li);

  return 0;
}
