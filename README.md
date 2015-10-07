# dvtm - dynamic virtual terminal manager

[dvtm](http://www.brain-dump.org/projects/dvtm/) brings the concept
of tiling window management, popularized by X11-window managers like
[dwm](http://dwm.suckless.org) to the console. As a console window
manager it tries to make it easy to work with multiple console based
programs.

![abduco+dvtm demo](https://raw.githubusercontent.com/martanne/dvtm/gh-pages/screencast.gif)

## News

- [dvtm-0.14](http://www.brain-dump.org/projects/dvtm/dvtm-0.14.tar.gz)
  [released](http://lists.suckless.org/dev/1502/25558.html) (19.02.2015)
- [dvtm-0.13](http://www.brain-dump.org/projects/dvtm/dvtm-0.13.tar.gz)
  [released](http://lists.suckless.org/dev/1411/24449.html) (15.11.2014)
- [dvtm-0.12](http://www.brain-dump.org/projects/dvtm/dvtm-0.12.tar.gz)
  [released](http://lists.suckless.org/dev/1407/22702.html) (05.07.2014)
- [dvtm-0.11](http://www.brain-dump.org/projects/dvtm/dvtm-0.11.tar.gz)
  [released](http://lists.suckless.org/dev/1403/20371.html) (08.03.2014)
- [dvtm-0.10](http://www.brain-dump.org/projects/dvtm/dvtm-0.10.tar.gz)
  [released](http://lists.suckless.org/dev/1312/18805.html) (28.12.2013)
- [dvtm-0.9](http://www.brain-dump.org/projects/dvtm/dvtm-0.9.tar.gz)
  [released](http://lists.suckless.org/dev/1304/15112.html) (3.04.2013)
- [dvtm-0.8](http://www.brain-dump.org/projects/dvtm/dvtm-0.8.tar.gz)
  [released](http://lists.suckless.org/dev/1208/12004.html) (1.08.2012)
- [dvtm-0.7](http://www.brain-dump.org/projects/dvtm/dvtm-0.7.tar.gz)
  [released](http://lists.suckless.org/dev/1109/9266.html) (4.09.2011)
- [dvtm-0.6](http://www.brain-dump.org/projects/dvtm/dvtm-0.6.tar.gz)
  [released](http://lists.suckless.org/dev/1010/6146.html) (8.10.2010)
- [dvtm-0.5.2](http://www.brain-dump.org/projects/dvtm/dvtm-0.5.2.tar.gz)
  [released](http://lists.suckless.org/dev/0907/0520.html) (7.07.2009)
- [dvtm-0.5.1](http://www.brain-dump.org/projects/dvtm/dvtm-0.5.1.tar.gz)
  [released](http://lists.suckless.org/dwm/0902/7405.html) (8.02.2009)
- [dvtm-0.5](http://www.brain-dump.org/projects/dvtm/dvtm-0.5.tar.gz)
  [released](http://lists.suckless.org/dwm/0901/7354.html) (26.01.2009)
- [dvtm-0.4.1](http://www.brain-dump.org/projects/dvtm/dvtm-0.4.1.tar.gz)
  [released](http://lists.suckless.org/dwm/0805/5672.html) (10.05.2008)
- [dvtm-0.4](http://www.brain-dump.org/projects/dvtm/dvtm-0.4.tar.gz)
  [released](http://lists.suckless.org/dwm/0802/4850.html) (17.02.2008)
- [dvtm-0.3](http://www.brain-dump.org/projects/dvtm/dvtm-0.3.tar.gz)
  [released](http://lists.suckless.org/dwm/0801/4735.html) (12.01.2008)
- [dvtm-0.2](http://www.brain-dump.org/projects/dvtm/dvtm-0.2.tar.gz)
  [released](http://lists.suckless.org/dwm/0712/4677.html) (29.12.2007)
- [dvtm-0.1](http://www.brain-dump.org/projects/dvtm/dvtm-0.1.tar.gz)
  [released](http://lists.suckless.org/dwm/0712/4632.html) (21.12.2007)
- [dvtm-0.01](http://www.brain-dump.org/projects/dvtm/dvtm-0.01.tar.gz)
  [released](http://lists.suckless.org/dwm/0712/4424.html) (08.12.2007)

## Download

Either Download the latest source tarball
[dvtm-0.14.tar.gz](http://www.brain-dump.org/projects/dvtm/dvtm-0.14.tar.gz)
with sha1sum

    205a2165e70455309f7ed6a6f11b3072fb9b13c3  dvtm-0.14.tar.gz

compile (you will need curses headers) and install it

    $EDITOR config.mk && $EDITOR config.def.h && make && sudo make install

or use one of the distribution provided packages:

 * [Debian](http://packages.debian.org/dvtm)
 * [Ubuntu](http://packages.ubuntu.com/dvtm)
 * [Fedora](https://admin.fedoraproject.org/pkgdb/package/dvtm/)
 * [openSUSE](http://software.opensuse.org/package/dvtm?search_term=dvtm)
 * [ArchLinux](http://www.archlinux.org/packages/?q=dvtm)
 * [Gentoo](http://packages.gentoo.org/package/app-misc/dvtm)
 * [Slackware](http://slackbuilds.org/result/?search=dvtm)
 * [FreeBSD](http://www.freshports.org/sysutils/dvtm/)
 * [NetBSD](http://www.pkgsrc.se/misc/dvtm/)
 * [OpenBSD](http://openports.se/misc/dvtm)
 * [Mac OS X](http://braumeister.org/formula/dvtm) via homebrew

## Why dvtm? The philosophy behind

dvtm strives to adhere to the
[Unix philosophy](http://www.catb.org/esr/writings/taoup/html/ch01s06.html).
It tries to do one thing, *dynamic* window management on the console,
and to do it well.

As such dvtm does *not* implement [session management](#faq) but instead
delegates this task to a separate tool called
[abduco](http://www.brain-dump.org/projects/abduco/).

Similarly dvtm's copy mode is implemented by piping the scroll back buffer
content to an external editor and only storing whatever the editor writes
to `stdout`. Hence the selection process is delegated to the editor
where powerful features such as regular expression search are available.

As a result dvtm's source code is relatively small
([~4000 lines of C](http://www.ohloh.net/p/dvtm/analyses/latest/languages_summary)),
simple and therefore easy to hack on.


## Quickstart

All of dvtm keybindings start with a common modifier which from now
on is refered to as `MOD`. By default `MOD` is set to `CTRL+g` however
this can be changed at runttime with the `-m` command line option.
For example setting `MOD` to `CTRL-b` is accomplished by starting
`dvtm -m ^b`.

### Windows

New windows are created with `MOD+c` and closed with `MOD+x`.
To switch among the windows use `MOD+j` and `MOD+k` or `MOD+[1..9]`
where the digit corresponds to the window number which is displayed
in the title bar. Windows can be minimized and restored with `MOD+.`.
Input can be directed to all visible window by pressing `MOD+a`,
issuing the same key combination again restores normal behaviour
i.e. only the currently focused window will receive input.

### Layouts

Visible Windows are arranged by a layout. Each layout consists of a
master and a tile area. Typically the master area occupies the largest
part of the screen and is intended for the currently most important
window. The size of the master area can be shrunk with `MOD+h`
and enlarged with `MOD-l` respectively. Windows can be zoomed into
the master area with `MOD+Enter`. The number of windows in the
master area can be increased and decreased with `MOD+i` and `MOD+d`.

By default dvtm comes with 4 different layouts which can be cycled
through via `MOD+Space`

 * vertical stack: master area on the left half, other clients
   stacked on the right
 * bottom stack: master area on the top half, other clients stacked below
 * grid: every window gets an equally sized portion of the screen
 * fullscreen: only the selected window is shown and occupies the
   whole available display area `MOD+m`

Further layouts are included in the source tarball but disabled by
default.

### Tagging

Each window has a non empty set of tags [1..n] associated with it. A view
consists of a number of tags. The current view includes all windows
which are tagged with the currently active tags. The following key
bindings are used to manipulate the tagsets.

- `MOD-0`  view all windows with any tag
- `Mod-v-Tab` toggles to the previously selected tags
- `MOD-v-[1..n]` view all windows with nth tag
- `Mod-V-[1..n]` add/remove all windows with nth tag to/from the view
- `Mod-t-[1..n]` apply nth tag to focused window
- `Mod-T-[1..n]` add/remove nth tag to/from focused window

### Statusbar

dvtm can be instructed to read and display status messages from a named
pipe. As an example the `dvtm-status` script is provided which shows the
current time.

    #!/bin/sh

    FIFO="/tmp/dvtm-status.$$"

    [ -p "$FIFO" ] || mkfifo -m 600 "$FIFO" || exit 1

    while true; do
        date +%H:%M
        sleep 60
    done > "$FIFO" &

    STATUS_PID=$!
    dvtm -s "$FIFO" "$@" 2> /dev/null
    kill $STATUS_PID
    wait $STATUS_PID 2> /dev/null
    rm -f "$FIFO"

### Copymode ###

`MOD+e` pipes the whole scroll buffer content to an external editor.
What ever the editor writes to `stdout` is remembered by dvtm and can
later be pasted with `MOD+p`.

In order for this to work the editor needs to be usable as a filter
and should use `stderr` for its user interface. Examples where this is
the case include [sandy](http://tools.suckless.org/sandy) and
[vis](http://github.com/martanne/vis).

    $ echo Hello World | vis - | cat

## Patches

There exist a number of out of tree patches which customize dvtm's
behaviour:

 - [pertag](http://waxandwane.org/dvtm.html) (see also the corresponding
   [mailing list post](http://lists.suckless.org/hackers/1510/8186.html))

## FAQ

### Detach / reattach functionality

dvtm doesn't have session support built in. Use
[abduco](http://www.brain-dump.org/projects/abduco/) instead.

    $ abduco -c dvtm-session

Detach using `CTRL-\` and later reattach with

    $ abduco -a dvtm-session

### Copy / Paste does not work under X

If you have mouse support enabled, which is the case with the
default settings, you need to hold down shift while selecting
and inserting text. In case you don't like this behaviour either
run dvtm with the `-M` command line argument, disable it at run
time with `MOD+M` or modify `config.def.h` to disable it completely
at compile time. You will however no longer be able to perform
other mouse actions like selecting windows etc.

### How to change the key bindings?

The configuration of dvtm is done by creating a custom `config.h`
and (re)compiling the source code. See the default `config.def.h`
as an example, adapting it to your preference should be straightforward.
You basically define a set of layouts and keys which dvtm will use.
There are some pre defined macros to ease configuration.

### WARNING: terminal is not fully functional

This means you haven't installed the `dvtm.info` terminfo description
which can be done with `tic -s dvtm.info`. If for some reason you
can't install new terminfo descriptions set the `DVTM_TERM` environment
variable to a known terminal when starting `dvtm` as in

    $ DVTM_TERM=rxvt dvtm

This will instruct dvtm to use rxvt as `$TERM` value within its windows.

### How to set the window title?

The window title can be changed by means of a
[xterm extension](http://tldp.org/HOWTO/Xterm-Title-3.html#ss3.2)
terminal escape sequence

    $ echo -ne "\033]0;Your title here\007"

So for example in `bash` if you want to display the current working
directory in the window title this can be accomplished by means of
the following section in your startup files.

    # If this is an xterm set the title to user@host:dir
    case "$TERM" in
    dvtm*|xterm*|rxvt*)
        PROMPT_COMMAND='echo -ne "\033]0;${USER}@${HOSTNAME}: ${PWD/$HOME/~}\007"'
        ;;
    *)
        ;;
    esac

Other shells provide similar functionality, zsh as an example has a
[precmd function](http://zsh.sourceforge.net/Doc/Release/Functions.html#Hook-Functions)
which can be used to achieve the same effect.

### Something is wrong with the displayed colors

Make sure you have set `$TERM` correctly for example if you want to
use 256 color profiles you probably have to append `-256color` to
your regular terminal name. Also due to limitations of ncurses by
default you can only use 255 color pairs simultaneously. If you
need more than 255 different color pairs at the same time, then you
have to rebuild ncurses with

    $ ./configure ... --enable-ext-colors

Note that this changes the ABI and therefore sets SONAME of the
library to 6 (i.e. you have to link against `libncursesw.so.6`).

### Some characters are displayed like garbage

Make sure you compiled dvtm against a unicode aware curses library
(in case of ncurses this would be `libncursesw`). Also make sure
that your locale settings contain UTF-8.

### The numeric keypad does not work with Putty

Disable [application keypad mode](http://the.earth.li/~sgtatham/putty/0.64/htmldoc/Chapter4.html#config-features-application)
in the Putty configuration under `Terminal => Features => Disable application keypad mode`.

### Unicode characters do not work within Putty

You have to tell Putty in which
[character encoding](http://the.earth.li/~sgtatham/putty/0.64/htmldoc/Chapter4.html#config-translation)
the received data is. Set the dropdown box under `Window => Translation`
to UTF-8. In order to get proper line drawing characters you proabably
also want to set the TERM environment variable to `putty` or `putty-256color`.
If that still doesn't do the trick then try running dvtm with the
following ncurses related environment variable set `NCURSES_NO_UTF8_ACS=1`.

## Development

You can always fetch the current code base from the git repository.

    git clone https://github.com/martanne/dvtm.git

or

    git clone git://repo.or.cz/dvtm.git

If you have comments, suggestions, ideas, a bug report, a patch or something
else related to abduco then write to the
[suckless developer mailing list](http://suckless.org/community)
or contact me directly mat[at]brain-dump.org.

[![Build Status](https://travis-ci.org/martanne/dvtm.svg?branch=master)](https://travis-ci.org/martanne/dvtm)
[![Coverity Scan Build Status](https://scan.coverity.com/projects/4256/badge.svg)](https://scan.coverity.com/projects/4256)

## License

dvtm reuses some code of dwm and is released under the same
[MIT/X11 license](https://raw.githubusercontent.com/martanne/dvtm/master/LICENSE).
The terminal emulation part is licensed under the ISC license.
