# SPDX-License-Identifier: GPL-2.0-only
# One case per build: CASE=pass|caches_on|bad_link|bad_source|short_count|budget
CROSS ?= arm-linux-gnueabi-
CASE ?= pass
CFLAGS = -std=gnu11 -O1 -g -Wall -Wextra -Werror -march=armv7-a -marm -ffreestanding -fno-pic -fno-stack-protector -I../.. -I.
SOURCES = start.S main.c ../../jump.S ../../copylist.c
all: jump-test.elf
jump-test.elf: $(SOURCES) link.ld
	$(CROSS)gcc $(CFLAGS) -DKOA2_JUMP_BUILD -DCASE_$(CASE)=1 -nostdlib -static -T link.ld $(SOURCES) -o $@
clean:
	rm -f jump-test.elf
