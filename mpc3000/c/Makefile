# MPC3000 emulator core (C port of x86cpu.py)
CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wno-misleading-indentation

all: mpc3k mpc3k_mpcx

mpc3k_mpcx: mpc3k_mpcx.c mpc3k.c mpc3k_kit.c pgm_template.h
	$(CC) $(CFLAGS) -o $@ mpc3k_mpcx.c -lm

mpc3k: mpc3k.c
	$(CC) $(CFLAGS) -o $@ $<

# MPC X (Rockchip RK3288, 32-bit ARMv7, Cortex-A17). Same toolchain as the MPC4000 project.
mpcx-arm: mpc3k.c mpc3k_mpcx.c mpc3k_kit.c pgm_template.h
	arm-linux-gnueabihf-gcc -O2 -static -march=armv7-a -mtune=cortex-a17 -mfpu=neon-vfpv4 -mfloat-abi=hard \
	    -Wall -Wno-misleading-indentation -o mpc3k-arm mpc3k.c
	arm-linux-gnueabihf-gcc -O2 -static -march=armv7-a -mtune=cortex-a17 -mfpu=neon-vfpv4 -mfloat-abi=hard \
	    -Wall -Wno-misleading-indentation -o mpc3k_mpcx_armhf mpc3k_mpcx.c -lm

# Lockstep check against the Python model (needs the interleaved ROM image)
check: mpc3k
	./mpc3k $(ROM) --steps 3000000 --trace 1000 > /tmp/c_trace.txt 2>/dev/null
	cd .. && python3 trace_py.py 3000000 1000 > /tmp/py_trace.txt
	cmp /tmp/c_trace.txt /tmp/py_trace.txt && echo "lockstep OK"

clean:
	rm -f mpc3k mpc3k-arm mpc3k_mpcx mpc3k_mpcx_armhf
