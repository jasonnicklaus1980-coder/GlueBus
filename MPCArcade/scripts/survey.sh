#!/bin/sh
# MPC Arcade step 1: system survey.  READ-ONLY: it only reads system information.
# The one file it writes is the report (default /sdcard/mpcx-survey.txt; delete it whenever you like).
#   sh survey.sh [report-file]
R="${1:-/sdcard/mpcx-survey.txt}"
{
echo "== date";              date
echo "== cpu / kernel";      uname -a; nproc; grep -m1 -iE "model name|Hardware" /proc/cpuinfo; grep -m1 Features /proc/cpuinfo
echo "== memory";            head -3 /proc/meminfo
echo "== os / libc";         cat /etc/os-release 2>/dev/null; ldd --version 2>&1 | head -1
echo "== storage";           df -h; mount | grep -vE "cgroup|proc|sys|devpts|tmpfs"
echo "== mpc service";       systemctl is-enabled acvs; systemctl is-active acvs; systemctl cat acvs --no-pager 2>&1 | head -40
echo "== display";           ls -l /dev/fb* /dev/dri 2>&1; for f in virtual_size bits_per_pixel stride name; do printf "fb0 %s: " $f; cat /sys/class/graphics/fb0/$f 2>&1; done
echo "== gfx libs";          ls /usr/lib /usr/lib/*-linux-* /lib 2>/dev/null | grep -iE "sdl|egl|gles|mali|drm|gbm|wayland|libX11" | sort -u
echo "== display processes"; ps -eo pid,comm,args | grep -iE "weston|xorg|wayland|MPC" | grep -v grep
echo "== audio";             cat /proc/asound/cards 2>&1; ls /usr/lib /lib 2>/dev/null | grep -iE "asound|jack|pulse" | sort -u; ps -eo comm | grep -iE "jack|pulse"
echo "== input";             cat /proc/bus/input/devices
echo "== tools";             for t in gcc cc make python3 opkg apt-get git timeout setsid nohup; do printf "%s: " $t; command -v $t || echo no; done
echo "== usb mounts";        ls /media 2>&1; ls /run/media 2>&1
echo "== hakai";             find / -xdev -maxdepth 4 -iname "*hakai*" 2>/dev/null | head -20
} > "$R" 2>&1
echo "saved $R ($(wc -l < "$R") lines)"
