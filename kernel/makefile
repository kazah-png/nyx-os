# ============================================================
# kernel/Makefile - Compilación del kernel NyxOS x86_64
# ============================================================

CC = gcc
AS = nasm
LD = ld

CFLAGS = -std=gnu99 -ffreestanding -Os -Wall -Wextra \
         -nostdlib -nostartfiles -nodefaultlibs \
         -fno-stack-protector -fno-pie -m64 \
         -mno-red-zone -mno-sse -mno-mmx -mno-sse2 \
         -mno-3dnow -mcmodel=large \
         -ffunction-sections -fdata-sections \
         -MMD -MP \
         -I.



ASFLAGS = -f elf64

LDFLAGS = -T linker.ld -nostdlib -m elf_x86_64

OBJS_KERNEL = boot.o kernel.o gdt.o idt.o isr.o isr_stubs.o irq.o \
        memory.o heap.o slab.o process.o syscall.o signal.o mmap.o pipe.o vfs.o ext2.o ata.o net.o udp.o \
        ip.o arp.o ethernet.o rtl8139.o icmp.o timer.o keyboard.o screen.o \
        serial.o umoddi3.o paging.o switch.o stubs.o dhcp.o \
        vbe.o fb.o mouse.o gui.o font.o compositor.o speaker.o tcp.o \
        sb16.o terminal_win.o fileman_win.o paint_win.o taskman_win.o \
        editor_win.o imageview_win.o soundtest_win.o calc_win.o wallpaper_win.o minesweeper_win.o \
        elf.o initramfs.o rtc.o apic.o dns.o http.o smp.o trampoline.o bootsplash.o auth.o login.o sha256.o shared_libc.o

OBJS = $(OBJS_KERNEL)

# Pin the default goal to `all` BEFORE -include pulls in the .d files below. Each
# auto-generated .d holds a rule like `kernel.o: kernel.c ...`; since -include sits
# above the `all:` target, on any incremental build (where .d files already exist)
# the first included rule would otherwise HIJACK the default goal to kernel.o — so a
# bare `make` (and build.ps1, which runs `make -C kernel` with no target) built only
# kernel.o and never relinked nyx-kernel.bin, silently shipping a stale kernel.
# `make clean` deletes the .d files, which is the only reason clean builds worked.
.DEFAULT_GOAL := all

# Auto-generated per-object header dependencies (from -MMD): every kernel .o now
# depends on EVERY header it #includes, so editing e.g. tcp.h's TCP_MAX_CONNS
# rebuilds tcp.o/net.o. Before this, only kernel.h was a tracked prerequisite, so
# changing a macro in any other header silently left STALE objects — a bump that
# "didn't take effect" until an unrelated .c edit forced a recompile (this masked a
# TCP_MAX_CONNS capacity fix as a phantom "Heisenbug"). `-include` ignores the .d
# files that don't exist yet (first build) or belong to .asm objects.
-include $(OBJS_KERNEL:.o=.d)

all: $(USER_ELFS) nyx-kernel.bin

# linker.ld is a prerequisite so editing the SECTIONS layout (e.g. capturing the
# -mcmodel=large .lbss/.ldata into .bss/.data) forces a relink. It is NOT passed to
# ld as an input object — the recipe uses $(OBJS), not $^, and LDFLAGS has -T linker.ld.
# (Before this, a linker.ld-only change left nyx-kernel.bin stale on incremental builds,
# because every .o was unchanged — the same stale-build class as the tcp.h/default-goal bugs.)
nyx-kernel.bin: $(OBJS) linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

# Header prerequisites are tracked automatically now (-MMD + the -include above), so
# a change to ANY included header — kernel.h's KERNEL_VERSION, tcp.h's TCP_MAX_CONNS,
# … — rebuilds the objects that include it. (kernel.h stays listed as a belt-and-
# braces prereq for the very first build, before any .d files exist.)
%.o: %.c kernel.h
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.asm
	$(AS) $(ASFLAGS) $< -o $@

# User-space programs (x86_64 native)
USER_DIR = ../user
USER_ELFS = $(USER_DIR)/hello.elf $(USER_DIR)/init.elf $(USER_DIR)/spin.elf $(USER_DIR)/fdleak.elf $(USER_DIR)/hello_nyx.elf $(USER_DIR)/args.elf \
            $(USER_DIR)/sh.elf $(USER_DIR)/echo.elf $(USER_DIR)/upper.elf $(USER_DIR)/cat.elf $(USER_DIR)/wc.elf $(USER_DIR)/ls.elf \
            $(USER_DIR)/grep.elf $(USER_DIR)/head.elf $(USER_DIR)/tail.elf \
            $(USER_DIR)/mkdir.elf $(USER_DIR)/rm.elf $(USER_DIR)/touch.elf $(USER_DIR)/sort.elf $(USER_DIR)/find.elf \
            $(USER_DIR)/ps.elf $(USER_DIR)/kill.elf $(USER_DIR)/top.elf $(USER_DIR)/edit.elf $(USER_DIR)/dltest.elf \
            $(USER_DIR)/pmap.elf $(USER_DIR)/free.elf $(USER_DIR)/env.elf $(USER_DIR)/date.elf $(USER_DIR)/sleep.elf \
            $(USER_DIR)/cp.elf $(USER_DIR)/mv.elf $(USER_DIR)/uname.elf $(USER_DIR)/whoami.elf $(USER_DIR)/which.elf $(USER_DIR)/clear.elf \
            $(USER_DIR)/fault.elf $(USER_DIR)/less.elf $(USER_DIR)/more.elf \
            $(USER_DIR)/pstorm.elf $(USER_DIR)/bigread.elf $(USER_DIR)/segv.elf \
            $(USER_DIR)/sockdemo.elf $(USER_DIR)/srvdemo.elf $(USER_DIR)/udpdemo.elf \
            $(USER_DIR)/nc.elf $(USER_DIR)/sigrec.elf $(USER_DIR)/alarmdemo.elf \
            $(USER_DIR)/polldemo.elf $(USER_DIR)/wget.elf $(USER_DIR)/netstorm.elf $(USER_DIR)/stat.elf \
            $(USER_DIR)/vfsfill.elf $(USER_DIR)/fsx.elf $(USER_DIR)/stacktest.elf $(USER_DIR)/threads.elf $(USER_DIR)/vmtest.elf $(USER_DIR)/munmaptest.elf

# Flags for Nyx C output (GCC: statement-exprs, __auto_type). -mno-red-zone is mandatory.
NYXCFLAGS = -std=gnu99 -Os -ffreestanding -nostdlib -m64 -mno-red-zone -I$(USER_DIR)

$(USER_DIR)/hello.elf: $(USER_DIR)/hello.asm
	nasm -f elf64 $< -o $(USER_DIR)/hello.o
	$(LD) -nostdlib -m elf_x86_64 -e _start -Ttext 0x10000 -o $@ $(USER_DIR)/hello.o

$(USER_DIR)/spin.elf: $(USER_DIR)/spin.asm
	nasm -f elf64 $< -o $(USER_DIR)/spin.o
	$(LD) -nostdlib -m elf_x86_64 -e _start -Ttext 0x10000 -o $@ $(USER_DIR)/spin.o

$(USER_DIR)/fdleak.elf: $(USER_DIR)/fdleak.asm
	nasm -f elf64 $< -o $(USER_DIR)/fdleak.o
	$(LD) -nostdlib -m elf_x86_64 -e _start -Ttext 0x10000 -o $@ $(USER_DIR)/fdleak.o

$(USER_DIR)/init.elf: $(USER_DIR)/crt0.o $(USER_DIR)/libc.so $(USER_DIR)/init.o
	$(LD) -nostdlib -m elf_x86_64 -e _start -Ttext 0x10000 -o $@ $(USER_DIR)/crt0.o --just-symbols=$(USER_DIR)/libc.so $(USER_DIR)/init.o

$(USER_DIR)/args.elf: $(USER_DIR)/crt0.o $(USER_DIR)/libc.so $(USER_DIR)/args.o
	$(LD) -nostdlib -m elf_x86_64 -e _start -Ttext 0x10000 -o $@ $(USER_DIR)/crt0.o --just-symbols=$(USER_DIR)/libc.so $(USER_DIR)/args.o

$(USER_DIR)/args.o: $(USER_DIR)/args.c $(USER_DIR)/libc.h $(USER_DIR)/syscall.h
	$(CC) -std=gnu99 -Os -ffreestanding -nostdlib -m64 -mno-red-zone -I$(USER_DIR) -c $< -o $@

# A deliberately-crashing program (NULL deref) to test ring-3 fault -> SIGSEGV.
$(USER_DIR)/fault.elf: $(USER_DIR)/crt0.o $(USER_DIR)/libc.so $(USER_DIR)/fault.o
	$(LD) -nostdlib -m elf_x86_64 -e _start -Ttext 0x10000 -o $@ $(USER_DIR)/crt0.o --just-symbols=$(USER_DIR)/libc.so $(USER_DIR)/fault.o

$(USER_DIR)/fault.o: $(USER_DIR)/fault.c $(USER_DIR)/libc.h $(USER_DIR)/syscall.h
	$(CC) -std=gnu99 -Os -ffreestanding -nostdlib -m64 -mno-red-zone -I$(USER_DIR) -c $< -o $@

# Catchable-SIGSEGV demo (installs a handler, derefs NULL, recovers via longjmp).
$(USER_DIR)/segv.elf: $(USER_DIR)/crt0.o $(USER_DIR)/libc.so $(USER_DIR)/segv.o
	$(LD) -nostdlib -m elf_x86_64 -e _start -Ttext 0x10000 -o $@ $(USER_DIR)/crt0.o --just-symbols=$(USER_DIR)/libc.so $(USER_DIR)/segv.o
$(USER_DIR)/segv.o: $(USER_DIR)/segv.c $(USER_DIR)/libc.h $(USER_DIR)/syscall.h
	$(CC) -std=gnu99 -Os -ffreestanding -nostdlib -m64 -mno-red-zone -I$(USER_DIR) -c $< -o $@

# Userspace TCP sockets demo (connects to the kernel loopback echo on 127.0.0.1:7).
$(USER_DIR)/sockdemo.elf: $(USER_DIR)/crt0.o $(USER_DIR)/libc.so $(USER_DIR)/sockdemo.o
	$(LD) -nostdlib -m elf_x86_64 -e _start -Ttext 0x10000 -o $@ $(USER_DIR)/crt0.o --just-symbols=$(USER_DIR)/libc.so $(USER_DIR)/sockdemo.o
$(USER_DIR)/sockdemo.o: $(USER_DIR)/sockdemo.c $(USER_DIR)/libc.h $(USER_DIR)/syscall.h
	$(CC) -std=gnu99 -Os -ffreestanding -nostdlib -m64 -mno-red-zone -I$(USER_DIR) -c $< -o $@

# Userspace TCP server demo (fork: parent bind/listen/accepts, child connects).
$(USER_DIR)/srvdemo.elf: $(USER_DIR)/crt0.o $(USER_DIR)/libc.so $(USER_DIR)/srvdemo.o
	$(LD) -nostdlib -m elf_x86_64 -e _start -Ttext 0x10000 -o $@ $(USER_DIR)/crt0.o --just-symbols=$(USER_DIR)/libc.so $(USER_DIR)/srvdemo.o
$(USER_DIR)/srvdemo.o: $(USER_DIR)/srvdemo.c $(USER_DIR)/libc.h $(USER_DIR)/syscall.h
	$(CC) -std=gnu99 -Os -ffreestanding -nostdlib -m64 -mno-red-zone -I$(USER_DIR) -c $< -o $@

# Userspace UDP sockets demo (SOCK_DGRAM sendto/recvfrom over loopback).
$(USER_DIR)/udpdemo.elf: $(USER_DIR)/crt0.o $(USER_DIR)/libc.so $(USER_DIR)/udpdemo.o
	$(LD) -nostdlib -m elf_x86_64 -e _start -Ttext 0x10000 -o $@ $(USER_DIR)/crt0.o --just-symbols=$(USER_DIR)/libc.so $(USER_DIR)/udpdemo.o
$(USER_DIR)/udpdemo.o: $(USER_DIR)/udpdemo.c $(USER_DIR)/libc.h $(USER_DIR)/syscall.h
	$(CC) -std=gnu99 -Os -ffreestanding -nostdlib -m64 -mno-red-zone -I$(USER_DIR) -c $< -o $@

# nc — netcat: bridge stdin/stdout to a TCP or UDP socket.
$(USER_DIR)/nc.elf: $(USER_DIR)/crt0.o $(USER_DIR)/libc.so $(USER_DIR)/nc.o
	$(LD) -nostdlib -m elf_x86_64 -e _start -Ttext 0x10000 -o $@ $(USER_DIR)/crt0.o --just-symbols=$(USER_DIR)/libc.so $(USER_DIR)/nc.o
$(USER_DIR)/nc.o: $(USER_DIR)/nc.c $(USER_DIR)/libc.h $(USER_DIR)/syscall.h
	$(CC) -std=gnu99 -Os -ffreestanding -nostdlib -m64 -mno-red-zone -I$(USER_DIR) -c $< -o $@

# wget — userspace HTTP/1.0 client (TCP sockets + a DNS-over-UDP resolver).
$(USER_DIR)/wget.elf: $(USER_DIR)/crt0.o $(USER_DIR)/libc.so $(USER_DIR)/wget.o
	$(LD) -nostdlib -m elf_x86_64 -e _start -Ttext 0x10000 -o $@ $(USER_DIR)/crt0.o --just-symbols=$(USER_DIR)/libc.so $(USER_DIR)/wget.o
$(USER_DIR)/wget.o: $(USER_DIR)/wget.c $(USER_DIR)/libc.h $(USER_DIR)/syscall.h
	$(CC) -std=gnu99 -Os -ffreestanding -nostdlib -m64 -mno-red-zone -I$(USER_DIR) -c $< -o $@

# netstorm — concurrent multi-process TCP stress repro (fork + loopback echo).
$(USER_DIR)/netstorm.elf: $(USER_DIR)/crt0.o $(USER_DIR)/libc.so $(USER_DIR)/netstorm.o
	$(LD) -nostdlib -m elf_x86_64 -e _start -Ttext 0x10000 -o $@ $(USER_DIR)/crt0.o --just-symbols=$(USER_DIR)/libc.so $(USER_DIR)/netstorm.o
$(USER_DIR)/netstorm.o: $(USER_DIR)/netstorm.c $(USER_DIR)/libc.h $(USER_DIR)/syscall.h
	$(CC) -std=gnu99 -Os -ffreestanding -nostdlib -m64 -mno-red-zone -I$(USER_DIR) -c $< -o $@

# stat — demo of the v5.8.80 POSIX syscalls (stat/fstat/lseek/getppid).
$(USER_DIR)/stat.elf: $(USER_DIR)/crt0.o $(USER_DIR)/libc.so $(USER_DIR)/stat.o
	$(LD) -nostdlib -m elf_x86_64 -e _start -Ttext 0x10000 -o $@ $(USER_DIR)/crt0.o --just-symbols=$(USER_DIR)/libc.so $(USER_DIR)/stat.o
$(USER_DIR)/stat.o: $(USER_DIR)/stat.c $(USER_DIR)/libc.h $(USER_DIR)/syscall.h
	$(CC) -std=gnu99 -Os -ffreestanding -nostdlib -m64 -mno-red-zone -I$(USER_DIR) -c $< -o $@

# vfsfill — VFS directory-child-cap hardening test (v5.8.81)
$(USER_DIR)/vfsfill.elf: $(USER_DIR)/crt0.o $(USER_DIR)/libc.so $(USER_DIR)/vfsfill.o
	$(LD) -nostdlib -m elf_x86_64 -e _start -Ttext 0x10000 -o $@ $(USER_DIR)/crt0.o --just-symbols=$(USER_DIR)/libc.so $(USER_DIR)/vfsfill.o
$(USER_DIR)/vfsfill.o: $(USER_DIR)/vfsfill.c $(USER_DIR)/libc.h $(USER_DIR)/syscall.h
	$(CC) -std=gnu99 -Os -ffreestanding -nostdlib -m64 -mno-red-zone -I$(USER_DIR) -c $< -o $@

# fsx — dup / rename / per-process cwd demo (v5.8.82)
$(USER_DIR)/fsx.elf: $(USER_DIR)/crt0.o $(USER_DIR)/libc.so $(USER_DIR)/fsx.o
	$(LD) -nostdlib -m elf_x86_64 -e _start -Ttext 0x10000 -o $@ $(USER_DIR)/crt0.o --just-symbols=$(USER_DIR)/libc.so $(USER_DIR)/fsx.o
$(USER_DIR)/fsx.o: $(USER_DIR)/fsx.c $(USER_DIR)/libc.h $(USER_DIR)/syscall.h
	$(CC) -std=gnu99 -Os -ffreestanding -nostdlib -m64 -mno-red-zone -I$(USER_DIR) -c $< -o $@

# vmtest — proves the user/kernel address boundary (user_v2p hardening)
$(USER_DIR)/vmtest.elf: $(USER_DIR)/crt0.o $(USER_DIR)/libc.so $(USER_DIR)/vmtest.o
	$(LD) -nostdlib -m elf_x86_64 -e _start -Ttext 0x10000 -o $@ $(USER_DIR)/crt0.o --just-symbols=$(USER_DIR)/libc.so $(USER_DIR)/vmtest.o
$(USER_DIR)/vmtest.o: $(USER_DIR)/vmtest.c $(USER_DIR)/libc.h $(USER_DIR)/syscall.h
	$(CC) -std=gnu99 -Os -ffreestanding -nostdlib -m64 -mno-red-zone -I$(USER_DIR) -c $< -o $@
# stacktest — demand-grown user stack + guard-page demo (v5.8.85)
$(USER_DIR)/stacktest.elf: $(USER_DIR)/crt0.o $(USER_DIR)/libc.so $(USER_DIR)/stacktest.o
	$(LD) -nostdlib -m elf_x86_64 -e _start -Ttext 0x10000 -o $@ $(USER_DIR)/crt0.o --just-symbols=$(USER_DIR)/libc.so $(USER_DIR)/stacktest.o
$(USER_DIR)/stacktest.o: $(USER_DIR)/stacktest.c $(USER_DIR)/libc.h $(USER_DIR)/syscall.h
	$(CC) -std=gnu99 -Os -ffreestanding -nostdlib -m64 -mno-red-zone -I$(USER_DIR) -c $< -o $@

# threads — clone(CLONE_VM) + futex mutex demo (v5.8.87)
$(USER_DIR)/threads.elf: $(USER_DIR)/crt0.o $(USER_DIR)/libc.so $(USER_DIR)/threads.o
	$(LD) -nostdlib -m elf_x86_64 -e _start -Ttext 0x10000 -o $@ $(USER_DIR)/crt0.o --just-symbols=$(USER_DIR)/libc.so $(USER_DIR)/threads.o
$(USER_DIR)/threads.o: $(USER_DIR)/threads.c $(USER_DIR)/libc.h $(USER_DIR)/syscall.h
	$(CC) -std=gnu99 -Os -ffreestanding -nostdlib -m64 -mno-red-zone -I$(USER_DIR) -c $< -o $@

# sigrec — multi-fault recovery demo (sigsetjmp/siglongjmp catch repeated SIGSEGV).
$(USER_DIR)/sigrec.elf: $(USER_DIR)/crt0.o $(USER_DIR)/libc.so $(USER_DIR)/sigrec.o
	$(LD) -nostdlib -m elf_x86_64 -e _start -Ttext 0x10000 -o $@ $(USER_DIR)/crt0.o --just-symbols=$(USER_DIR)/libc.so $(USER_DIR)/sigrec.o
$(USER_DIR)/sigrec.o: $(USER_DIR)/sigrec.c $(USER_DIR)/libc.h $(USER_DIR)/syscall.h
	$(CC) -std=gnu99 -Os -ffreestanding -nostdlib -m64 -mno-red-zone -I$(USER_DIR) -c $< -o $@

# alarmdemo — timer-driven SIGALRM via alarm().
$(USER_DIR)/alarmdemo.elf: $(USER_DIR)/crt0.o $(USER_DIR)/libc.so $(USER_DIR)/alarmdemo.o
	$(LD) -nostdlib -m elf_x86_64 -e _start -Ttext 0x10000 -o $@ $(USER_DIR)/crt0.o --just-symbols=$(USER_DIR)/libc.so $(USER_DIR)/alarmdemo.o
$(USER_DIR)/alarmdemo.o: $(USER_DIR)/alarmdemo.c $(USER_DIR)/libc.h $(USER_DIR)/syscall.h
	$(CC) -std=gnu99 -Os -ffreestanding -nostdlib -m64 -mno-red-zone -I$(USER_DIR) -c $< -o $@

# polldemo — poll() I/O multiplexing over a pipe + a socket.
$(USER_DIR)/polldemo.elf: $(USER_DIR)/crt0.o $(USER_DIR)/libc.so $(USER_DIR)/polldemo.o
	$(LD) -nostdlib -m elf_x86_64 -e _start -Ttext 0x10000 -o $@ $(USER_DIR)/crt0.o --just-symbols=$(USER_DIR)/libc.so $(USER_DIR)/polldemo.o
$(USER_DIR)/polldemo.o: $(USER_DIR)/polldemo.c $(USER_DIR)/libc.h $(USER_DIR)/syscall.h
	$(CC) -std=gnu99 -Os -ffreestanding -nostdlib -m64 -mno-red-zone -I$(USER_DIR) -c $< -o $@

# The pager. less.elf and more.elf are the same program (full-screen pager).
$(USER_DIR)/less.elf: $(USER_DIR)/crt0.o $(USER_DIR)/libc.so $(USER_DIR)/less.o
	$(LD) -nostdlib -m elf_x86_64 -e _start -Ttext 0x10000 -o $@ $(USER_DIR)/crt0.o --just-symbols=$(USER_DIR)/libc.so $(USER_DIR)/less.o

$(USER_DIR)/more.elf: $(USER_DIR)/crt0.o $(USER_DIR)/libc.so $(USER_DIR)/less.o
	$(LD) -nostdlib -m elf_x86_64 -e _start -Ttext 0x10000 -o $@ $(USER_DIR)/crt0.o --just-symbols=$(USER_DIR)/libc.so $(USER_DIR)/less.o

$(USER_DIR)/less.o: $(USER_DIR)/less.c $(USER_DIR)/libc.h $(USER_DIR)/syscall.h
	$(CC) -std=gnu99 -Os -ffreestanding -nostdlib -m64 -mno-red-zone -I$(USER_DIR) -c $< -o $@

# pstorm + bigread: deterministic fork+pipe+execve churn reproducer (debug-only).
$(USER_DIR)/pstorm.elf: $(USER_DIR)/crt0.o $(USER_DIR)/libc.so $(USER_DIR)/pstorm.o
	$(LD) -nostdlib -m elf_x86_64 -e _start -Ttext 0x10000 -o $@ $(USER_DIR)/crt0.o --just-symbols=$(USER_DIR)/libc.so $(USER_DIR)/pstorm.o
$(USER_DIR)/pstorm.o: $(USER_DIR)/pstorm.c $(USER_DIR)/libc.h $(USER_DIR)/syscall.h
	$(CC) -std=gnu99 -Os -ffreestanding -nostdlib -m64 -mno-red-zone -I$(USER_DIR) -c $< -o $@
$(USER_DIR)/bigread.elf: $(USER_DIR)/crt0.o $(USER_DIR)/libc.so $(USER_DIR)/bigread.o
	$(LD) -nostdlib -m elf_x86_64 -e _start -Ttext 0x10000 -o $@ $(USER_DIR)/crt0.o --just-symbols=$(USER_DIR)/libc.so $(USER_DIR)/bigread.o
$(USER_DIR)/bigread.o: $(USER_DIR)/bigread.c $(USER_DIR)/libc.h $(USER_DIR)/syscall.h
	$(CC) -std=gnu99 -Os -ffreestanding -nostdlib -m64 -mno-red-zone -I$(USER_DIR) -c $< -o $@

# The userspace shell + its coreutils (crt0 + libc + program)
$(USER_DIR)/sh.elf: $(USER_DIR)/crt0.o $(USER_DIR)/libc.so $(USER_DIR)/sh.o
	$(LD) -nostdlib -m elf_x86_64 -e _start -Ttext 0x10000 -o $@ $(USER_DIR)/crt0.o --just-symbols=$(USER_DIR)/libc.so $(USER_DIR)/sh.o

$(USER_DIR)/echo.elf: $(USER_DIR)/crt0.o $(USER_DIR)/libc.so $(USER_DIR)/echo.o
	$(LD) -nostdlib -m elf_x86_64 -e _start -Ttext 0x10000 -o $@ $(USER_DIR)/crt0.o --just-symbols=$(USER_DIR)/libc.so $(USER_DIR)/echo.o

$(USER_DIR)/upper.elf: $(USER_DIR)/crt0.o $(USER_DIR)/libc.so $(USER_DIR)/upper.o
	$(LD) -nostdlib -m elf_x86_64 -e _start -Ttext 0x10000 -o $@ $(USER_DIR)/crt0.o --just-symbols=$(USER_DIR)/libc.so $(USER_DIR)/upper.o

$(USER_DIR)/cat.elf: $(USER_DIR)/crt0.o $(USER_DIR)/libc.so $(USER_DIR)/cat.o
	$(LD) -nostdlib -m elf_x86_64 -e _start -Ttext 0x10000 -o $@ $(USER_DIR)/crt0.o --just-symbols=$(USER_DIR)/libc.so $(USER_DIR)/cat.o

$(USER_DIR)/wc.elf: $(USER_DIR)/crt0.o $(USER_DIR)/libc.so $(USER_DIR)/wc.o
	$(LD) -nostdlib -m elf_x86_64 -e _start -Ttext 0x10000 -o $@ $(USER_DIR)/crt0.o --just-symbols=$(USER_DIR)/libc.so $(USER_DIR)/wc.o

$(USER_DIR)/ls.elf: $(USER_DIR)/crt0.o $(USER_DIR)/libc.so $(USER_DIR)/ls.o
	$(LD) -nostdlib -m elf_x86_64 -e _start -Ttext 0x10000 -o $@ $(USER_DIR)/crt0.o --just-symbols=$(USER_DIR)/libc.so $(USER_DIR)/ls.o

$(USER_DIR)/grep.elf: $(USER_DIR)/crt0.o $(USER_DIR)/libc.so $(USER_DIR)/grep.o
	$(LD) -nostdlib -m elf_x86_64 -e _start -Ttext 0x10000 -o $@ $(USER_DIR)/crt0.o --just-symbols=$(USER_DIR)/libc.so $(USER_DIR)/grep.o

$(USER_DIR)/mkdir.elf: $(USER_DIR)/crt0.o $(USER_DIR)/libc.so $(USER_DIR)/mkdir.o
	$(LD) -nostdlib -m elf_x86_64 -e _start -Ttext 0x10000 -o $@ $(USER_DIR)/crt0.o --just-symbols=$(USER_DIR)/libc.so $(USER_DIR)/mkdir.o

$(USER_DIR)/rm.elf: $(USER_DIR)/crt0.o $(USER_DIR)/libc.so $(USER_DIR)/rm.o
	$(LD) -nostdlib -m elf_x86_64 -e _start -Ttext 0x10000 -o $@ $(USER_DIR)/crt0.o --just-symbols=$(USER_DIR)/libc.so $(USER_DIR)/rm.o

$(USER_DIR)/touch.elf: $(USER_DIR)/crt0.o $(USER_DIR)/libc.so $(USER_DIR)/touch.o
	$(LD) -nostdlib -m elf_x86_64 -e _start -Ttext 0x10000 -o $@ $(USER_DIR)/crt0.o --just-symbols=$(USER_DIR)/libc.so $(USER_DIR)/touch.o

$(USER_DIR)/sort.elf: $(USER_DIR)/crt0.o $(USER_DIR)/libc.so $(USER_DIR)/sort.o
	$(LD) -nostdlib -m elf_x86_64 -e _start -Ttext 0x10000 -o $@ $(USER_DIR)/crt0.o --just-symbols=$(USER_DIR)/libc.so $(USER_DIR)/sort.o

$(USER_DIR)/find.elf: $(USER_DIR)/crt0.o $(USER_DIR)/libc.so $(USER_DIR)/find.o
	$(LD) -nostdlib -m elf_x86_64 -e _start -Ttext 0x10000 -o $@ $(USER_DIR)/crt0.o --just-symbols=$(USER_DIR)/libc.so $(USER_DIR)/find.o

$(USER_DIR)/head.elf: $(USER_DIR)/crt0.o $(USER_DIR)/libc.so $(USER_DIR)/head.o
	$(LD) -nostdlib -m elf_x86_64 -e _start -Ttext 0x10000 -o $@ $(USER_DIR)/crt0.o --just-symbols=$(USER_DIR)/libc.so $(USER_DIR)/head.o

$(USER_DIR)/tail.elf: $(USER_DIR)/crt0.o $(USER_DIR)/libc.so $(USER_DIR)/tail.o
	$(LD) -nostdlib -m elf_x86_64 -e _start -Ttext 0x10000 -o $@ $(USER_DIR)/crt0.o --just-symbols=$(USER_DIR)/libc.so $(USER_DIR)/tail.o

$(USER_DIR)/ps.elf: $(USER_DIR)/crt0.o $(USER_DIR)/libc.so $(USER_DIR)/ps.o
	$(LD) -nostdlib -m elf_x86_64 -e _start -Ttext 0x10000 -o $@ $(USER_DIR)/crt0.o --just-symbols=$(USER_DIR)/libc.so $(USER_DIR)/ps.o

$(USER_DIR)/kill.elf: $(USER_DIR)/crt0.o $(USER_DIR)/libc.so $(USER_DIR)/kill.o
	$(LD) -nostdlib -m elf_x86_64 -e _start -Ttext 0x10000 -o $@ $(USER_DIR)/crt0.o --just-symbols=$(USER_DIR)/libc.so $(USER_DIR)/kill.o

$(USER_DIR)/top.elf: $(USER_DIR)/crt0.o $(USER_DIR)/libc.so $(USER_DIR)/top.o
	$(LD) -nostdlib -m elf_x86_64 -e _start -Ttext 0x10000 -o $@ $(USER_DIR)/crt0.o --just-symbols=$(USER_DIR)/libc.so $(USER_DIR)/top.o

$(USER_DIR)/edit.elf: $(USER_DIR)/crt0.o $(USER_DIR)/libc.so $(USER_DIR)/edit.o
	$(LD) -nostdlib -m elf_x86_64 -e _start -Ttext 0x10000 -o $@ $(USER_DIR)/crt0.o --just-symbols=$(USER_DIR)/libc.so $(USER_DIR)/edit.o

# dltest links against libc (--just-symbols) but NOT libdemo — it loads that at
# runtime with dlopen()/dlsym().
$(USER_DIR)/dltest.elf: $(USER_DIR)/crt0.o $(USER_DIR)/libc.so $(USER_DIR)/dltest.o
	$(LD) -nostdlib -m elf_x86_64 -e _start -Ttext 0x10000 -o $@ $(USER_DIR)/crt0.o --just-symbols=$(USER_DIR)/libc.so $(USER_DIR)/dltest.o

# libdemo.so: a demo shared library prelinked at 0x31000000 (above libc), loaded
# on demand by dlopen. Self-contained, so a plain link with no libc.
$(USER_DIR)/libdemo.so: $(USER_DIR)/libdemo.o
	$(LD) -nostdlib -m elf_x86_64 -e 0 -Ttext 0x31000000 -o $@ $(USER_DIR)/libdemo.o

$(USER_DIR)/pmap.elf: $(USER_DIR)/crt0.o $(USER_DIR)/libc.so $(USER_DIR)/pmap.o
	$(LD) -nostdlib -m elf_x86_64 -e _start -Ttext 0x10000 -o $@ $(USER_DIR)/crt0.o --just-symbols=$(USER_DIR)/libc.so $(USER_DIR)/pmap.o

$(USER_DIR)/free.elf: $(USER_DIR)/crt0.o $(USER_DIR)/libc.so $(USER_DIR)/free.o
	$(LD) -nostdlib -m elf_x86_64 -e _start -Ttext 0x10000 -o $@ $(USER_DIR)/crt0.o --just-symbols=$(USER_DIR)/libc.so $(USER_DIR)/free.o

$(USER_DIR)/env.elf: $(USER_DIR)/crt0.o $(USER_DIR)/libc.so $(USER_DIR)/env.o
	$(LD) -nostdlib -m elf_x86_64 -e _start -Ttext 0x10000 -o $@ $(USER_DIR)/crt0.o --just-symbols=$(USER_DIR)/libc.so $(USER_DIR)/env.o

$(USER_DIR)/date.elf: $(USER_DIR)/crt0.o $(USER_DIR)/libc.so $(USER_DIR)/date.o
	$(LD) -nostdlib -m elf_x86_64 -e _start -Ttext 0x10000 -o $@ $(USER_DIR)/crt0.o --just-symbols=$(USER_DIR)/libc.so $(USER_DIR)/date.o

$(USER_DIR)/sleep.elf: $(USER_DIR)/crt0.o $(USER_DIR)/libc.so $(USER_DIR)/sleep.o
	$(LD) -nostdlib -m elf_x86_64 -e _start -Ttext 0x10000 -o $@ $(USER_DIR)/crt0.o --just-symbols=$(USER_DIR)/libc.so $(USER_DIR)/sleep.o

$(USER_DIR)/cp.elf: $(USER_DIR)/crt0.o $(USER_DIR)/libc.so $(USER_DIR)/cp.o
	$(LD) -nostdlib -m elf_x86_64 -e _start -Ttext 0x10000 -o $@ $(USER_DIR)/crt0.o --just-symbols=$(USER_DIR)/libc.so $(USER_DIR)/cp.o

$(USER_DIR)/mv.elf: $(USER_DIR)/crt0.o $(USER_DIR)/libc.so $(USER_DIR)/mv.o
	$(LD) -nostdlib -m elf_x86_64 -e _start -Ttext 0x10000 -o $@ $(USER_DIR)/crt0.o --just-symbols=$(USER_DIR)/libc.so $(USER_DIR)/mv.o

$(USER_DIR)/uname.elf: $(USER_DIR)/crt0.o $(USER_DIR)/libc.so $(USER_DIR)/uname.o
	$(LD) -nostdlib -m elf_x86_64 -e _start -Ttext 0x10000 -o $@ $(USER_DIR)/crt0.o --just-symbols=$(USER_DIR)/libc.so $(USER_DIR)/uname.o

$(USER_DIR)/whoami.elf: $(USER_DIR)/crt0.o $(USER_DIR)/libc.so $(USER_DIR)/whoami.o
	$(LD) -nostdlib -m elf_x86_64 -e _start -Ttext 0x10000 -o $@ $(USER_DIR)/crt0.o --just-symbols=$(USER_DIR)/libc.so $(USER_DIR)/whoami.o

$(USER_DIR)/which.elf: $(USER_DIR)/crt0.o $(USER_DIR)/libc.so $(USER_DIR)/which.o
	$(LD) -nostdlib -m elf_x86_64 -e _start -Ttext 0x10000 -o $@ $(USER_DIR)/crt0.o --just-symbols=$(USER_DIR)/libc.so $(USER_DIR)/which.o

$(USER_DIR)/clear.elf: $(USER_DIR)/crt0.o $(USER_DIR)/libc.so $(USER_DIR)/clear.o
	$(LD) -nostdlib -m elf_x86_64 -e _start -Ttext 0x10000 -o $@ $(USER_DIR)/crt0.o --just-symbols=$(USER_DIR)/libc.so $(USER_DIR)/clear.o

$(USER_DIR)/sh.o: $(USER_DIR)/sh.c $(USER_DIR)/libc.h $(USER_DIR)/syscall.h
	$(CC) -std=gnu99 -Os -ffreestanding -nostdlib -m64 -mno-red-zone -I$(USER_DIR) -c $< -o $@

$(USER_DIR)/echo.o: $(USER_DIR)/echo.c $(USER_DIR)/libc.h $(USER_DIR)/syscall.h
	$(CC) -std=gnu99 -Os -ffreestanding -nostdlib -m64 -mno-red-zone -I$(USER_DIR) -c $< -o $@

$(USER_DIR)/upper.o: $(USER_DIR)/upper.c $(USER_DIR)/libc.h $(USER_DIR)/syscall.h
	$(CC) -std=gnu99 -Os -ffreestanding -nostdlib -m64 -mno-red-zone -I$(USER_DIR) -c $< -o $@

$(USER_DIR)/cat.o: $(USER_DIR)/cat.c $(USER_DIR)/libc.h $(USER_DIR)/syscall.h
	$(CC) -std=gnu99 -Os -ffreestanding -nostdlib -m64 -mno-red-zone -I$(USER_DIR) -c $< -o $@

$(USER_DIR)/wc.o: $(USER_DIR)/wc.c $(USER_DIR)/libc.h $(USER_DIR)/syscall.h
	$(CC) -std=gnu99 -Os -ffreestanding -nostdlib -m64 -mno-red-zone -I$(USER_DIR) -c $< -o $@

$(USER_DIR)/ls.o: $(USER_DIR)/ls.c $(USER_DIR)/libc.h $(USER_DIR)/syscall.h
	$(CC) -std=gnu99 -Os -ffreestanding -nostdlib -m64 -mno-red-zone -I$(USER_DIR) -c $< -o $@

$(USER_DIR)/grep.o: $(USER_DIR)/grep.c $(USER_DIR)/libc.h $(USER_DIR)/syscall.h
	$(CC) -std=gnu99 -Os -ffreestanding -nostdlib -m64 -mno-red-zone -I$(USER_DIR) -c $< -o $@

$(USER_DIR)/head.o: $(USER_DIR)/head.c $(USER_DIR)/libc.h $(USER_DIR)/syscall.h
	$(CC) -std=gnu99 -Os -ffreestanding -nostdlib -m64 -mno-red-zone -I$(USER_DIR) -c $< -o $@

$(USER_DIR)/tail.o: $(USER_DIR)/tail.c $(USER_DIR)/libc.h $(USER_DIR)/syscall.h
	$(CC) -std=gnu99 -Os -ffreestanding -nostdlib -m64 -mno-red-zone -I$(USER_DIR) -c $< -o $@

$(USER_DIR)/mkdir.o: $(USER_DIR)/mkdir.c $(USER_DIR)/libc.h $(USER_DIR)/syscall.h
	$(CC) -std=gnu99 -Os -ffreestanding -nostdlib -m64 -mno-red-zone -I$(USER_DIR) -c $< -o $@

$(USER_DIR)/rm.o: $(USER_DIR)/rm.c $(USER_DIR)/libc.h $(USER_DIR)/syscall.h
	$(CC) -std=gnu99 -Os -ffreestanding -nostdlib -m64 -mno-red-zone -I$(USER_DIR) -c $< -o $@

$(USER_DIR)/touch.o: $(USER_DIR)/touch.c $(USER_DIR)/libc.h $(USER_DIR)/syscall.h
	$(CC) -std=gnu99 -Os -ffreestanding -nostdlib -m64 -mno-red-zone -I$(USER_DIR) -c $< -o $@

$(USER_DIR)/sort.o: $(USER_DIR)/sort.c $(USER_DIR)/libc.h $(USER_DIR)/syscall.h
	$(CC) -std=gnu99 -Os -ffreestanding -nostdlib -m64 -mno-red-zone -I$(USER_DIR) -c $< -o $@

$(USER_DIR)/find.o: $(USER_DIR)/find.c $(USER_DIR)/libc.h $(USER_DIR)/syscall.h
	$(CC) -std=gnu99 -Os -ffreestanding -nostdlib -m64 -mno-red-zone -I$(USER_DIR) -c $< -o $@

$(USER_DIR)/ps.o: $(USER_DIR)/ps.c $(USER_DIR)/libc.h $(USER_DIR)/syscall.h
	$(CC) -std=gnu99 -Os -ffreestanding -nostdlib -m64 -mno-red-zone -I$(USER_DIR) -c $< -o $@

$(USER_DIR)/kill.o: $(USER_DIR)/kill.c $(USER_DIR)/libc.h $(USER_DIR)/syscall.h
	$(CC) -std=gnu99 -Os -ffreestanding -nostdlib -m64 -mno-red-zone -I$(USER_DIR) -c $< -o $@

$(USER_DIR)/top.o: $(USER_DIR)/top.c $(USER_DIR)/libc.h $(USER_DIR)/syscall.h
	$(CC) -std=gnu99 -Os -ffreestanding -nostdlib -m64 -mno-red-zone -I$(USER_DIR) -c $< -o $@

$(USER_DIR)/edit.o: $(USER_DIR)/edit.c $(USER_DIR)/libc.h $(USER_DIR)/syscall.h
	$(CC) -std=gnu99 -Os -ffreestanding -nostdlib -m64 -mno-red-zone -I$(USER_DIR) -c $< -o $@

$(USER_DIR)/dltest.o: $(USER_DIR)/dltest.c $(USER_DIR)/libc.h $(USER_DIR)/syscall.h
	$(CC) -std=gnu99 -Os -ffreestanding -nostdlib -m64 -mno-red-zone -I$(USER_DIR) -c $< -o $@

$(USER_DIR)/libdemo.o: $(USER_DIR)/libdemo.c
	$(CC) -std=gnu99 -Os -ffreestanding -nostdlib -m64 -mno-red-zone -I$(USER_DIR) -c $< -o $@

$(USER_DIR)/pmap.o: $(USER_DIR)/pmap.c $(USER_DIR)/libc.h $(USER_DIR)/syscall.h
	$(CC) -std=gnu99 -Os -ffreestanding -nostdlib -m64 -mno-red-zone -I$(USER_DIR) -c $< -o $@

$(USER_DIR)/free.o: $(USER_DIR)/free.c $(USER_DIR)/libc.h $(USER_DIR)/syscall.h
	$(CC) -std=gnu99 -Os -ffreestanding -nostdlib -m64 -mno-red-zone -I$(USER_DIR) -c $< -o $@

$(USER_DIR)/env.o: $(USER_DIR)/env.c $(USER_DIR)/libc.h $(USER_DIR)/syscall.h
	$(CC) -std=gnu99 -Os -ffreestanding -nostdlib -m64 -mno-red-zone -I$(USER_DIR) -c $< -o $@

$(USER_DIR)/date.o: $(USER_DIR)/date.c $(USER_DIR)/libc.h $(USER_DIR)/syscall.h
	$(CC) -std=gnu99 -Os -ffreestanding -nostdlib -m64 -mno-red-zone -I$(USER_DIR) -c $< -o $@

$(USER_DIR)/sleep.o: $(USER_DIR)/sleep.c $(USER_DIR)/libc.h $(USER_DIR)/syscall.h
	$(CC) -std=gnu99 -Os -ffreestanding -nostdlib -m64 -mno-red-zone -I$(USER_DIR) -c $< -o $@

$(USER_DIR)/cp.o: $(USER_DIR)/cp.c $(USER_DIR)/libc.h $(USER_DIR)/syscall.h
	$(CC) -std=gnu99 -Os -ffreestanding -nostdlib -m64 -mno-red-zone -I$(USER_DIR) -c $< -o $@

$(USER_DIR)/mv.o: $(USER_DIR)/mv.c $(USER_DIR)/libc.h $(USER_DIR)/syscall.h
	$(CC) -std=gnu99 -Os -ffreestanding -nostdlib -m64 -mno-red-zone -I$(USER_DIR) -c $< -o $@

$(USER_DIR)/uname.o: $(USER_DIR)/uname.c $(USER_DIR)/libc.h $(USER_DIR)/syscall.h
	$(CC) -std=gnu99 -Os -ffreestanding -nostdlib -m64 -mno-red-zone -I$(USER_DIR) -c $< -o $@

$(USER_DIR)/whoami.o: $(USER_DIR)/whoami.c $(USER_DIR)/libc.h $(USER_DIR)/syscall.h
	$(CC) -std=gnu99 -Os -ffreestanding -nostdlib -m64 -mno-red-zone -I$(USER_DIR) -c $< -o $@

$(USER_DIR)/which.o: $(USER_DIR)/which.c $(USER_DIR)/libc.h $(USER_DIR)/syscall.h
	$(CC) -std=gnu99 -Os -ffreestanding -nostdlib -m64 -mno-red-zone -I$(USER_DIR) -c $< -o $@

$(USER_DIR)/clear.o: $(USER_DIR)/clear.c $(USER_DIR)/libc.h $(USER_DIR)/syscall.h
	$(CC) -std=gnu99 -Os -ffreestanding -nostdlib -m64 -mno-red-zone -I$(USER_DIR) -c $< -o $@

$(USER_DIR)/crt0.o: $(USER_DIR)/crt0.asm
	nasm -f elf64 $< -o $@

# Nyx C "hello": crt0 + runtime + transpiled output. Reuses the existing crt0
# (it calls main / exit). hello_nyx.c is nyxc's C output from hello.nyx.
$(USER_DIR)/hello_nyx.elf: $(USER_DIR)/crt0.o $(USER_DIR)/nyxrt.o $(USER_DIR)/hello_nyx.o
	$(LD) -nostdlib -m elf_x86_64 -e _start -Ttext 0x10000 -o $@ \
	    $(USER_DIR)/crt0.o $(USER_DIR)/nyxrt.o $(USER_DIR)/hello_nyx.o

$(USER_DIR)/nyxrt.o: $(USER_DIR)/nyxrt.c $(USER_DIR)/nyxrt.h
	$(CC) $(NYXCFLAGS) -c $< -o $@

$(USER_DIR)/hello_nyx.o: $(USER_DIR)/hello_nyx.c $(USER_DIR)/nyxrt.h
	$(CC) $(NYXCFLAGS) -c $< -o $@

$(USER_DIR)/libc.o: $(USER_DIR)/libc.c $(USER_DIR)/libc.h $(USER_DIR)/syscall.h
	$(CC) -std=gnu99 -Os -ffreestanding -nostdlib -m64 -mno-red-zone -I$(USER_DIR) -c $< -o $@

# Shared libc: libc.o prelinked at the fixed VA SHARED_LIBC_BASE (0x30000000). The
# kernel loads this at boot and maps it into every process; programs link against
# it with --just-symbols (above) instead of bundling libc.o. (-e 0 silences the
# missing-_start warning; libc.so has no entry point — it's never executed as a
# program.) It ships in the initramfs (see mkinitramfs) as /libc.so.
$(USER_DIR)/libc.so: $(USER_DIR)/libc.o
	$(LD) -nostdlib -m elf_x86_64 -e 0 -Ttext 0x30000000 -o $@ $(USER_DIR)/libc.o

$(USER_DIR)/init.o: $(USER_DIR)/init.c $(USER_DIR)/libc.h $(USER_DIR)/syscall.h
	$(CC) -std=gnu99 -Os -ffreestanding -nostdlib -m64 -mno-red-zone -I$(USER_DIR) -c $< -o $@

# Generate initramfs C header.
# FORCE + content-compare: the recipe runs on EVERY make (FORCE is a phony that is
# always "out of date"), so a freshly-built user/*.elf can never be missed. But we
# regenerate into a temp and only replace initramfs_data.h when the bytes actually
# differ, so initramfs.o relinks only on a real change — not every build. This is
# the canonical regenerate-only-if-changed idiom, and here it also defends against
# the WSL /mnt/c (9p) mtime skew that once let a stale initramfs — missing a
# just-built .elf — ship silently despite the .h being newer on disk. Companion to
# the -MMD/-MP auto-deps that track the hand-written headers.
MKINITRAMFS = ../tools/mkinitramfs.py
initramfs_data.h: FORCE $(MKINITRAMFS) $(USER_ELFS) $(USER_DIR)/libc.so $(USER_DIR)/libdemo.so
	@python3 $(MKINITRAMFS) $@.tmp c >/dev/null
	@if cmp -s $@.tmp $@ 2>/dev/null; then rm -f $@.tmp; else mv $@.tmp $@; echo "  [initramfs] regenerated $@"; fi

initramfs.o: initramfs.c initramfs_data.h

.PHONY: FORCE
FORCE:

# SMP trampoline — flat binary embedded as ELF object
trampoline.bin: trampoline.asm
	$(AS) -f bin -o $@ $<

trampoline.o: trampoline.bin
	$(LD) -r -b binary -m elf_x86_64 -o $@ $<

# Persistent storage: create a fresh empty ext2 disk for /mnt. Boot NyxOS with
# `-hda ext2-test.img` and files written under /mnt survive reboots (it auto-mounts at
# boot). Needs mkfs.ext2 (e2fsprogs); NyxOS's ext2 driver uses classic 1024-byte
# blocks / 128-byte inodes. `make clean` never removes it (it's your data).
disk: ../ext2-test.img
../ext2-test.img:
	dd if=/dev/zero of=$@ bs=1M count=16
	mkfs.ext2 -q -b 1024 -I 128 -O ^resize_inode,^dir_index,^ext_attr $@
	@echo "Created $@ (16 MB ext2). Boot: qemu-system-x86_64 -cdrom NyxOS.iso -hda ext2-test.img"
.PHONY: disk

clean:
	rm -f *.o *.d *.bin initramfs_data.h initramfs_data.h.tmp trampoline.bin $(USER_DIR)/*.o $(USER_DIR)/*.elf $(USER_DIR)/hello.o

# munmaptest: partial-munmap VMA split/trim test (v5.9.27)
$(USER_DIR)/munmaptest.elf: $(USER_DIR)/crt0.o $(USER_DIR)/libc.so $(USER_DIR)/munmaptest.o
	$(LD) -nostdlib -m elf_x86_64 -e _start -Ttext 0x10000 -o $@ $(USER_DIR)/crt0.o --just-symbols=$(USER_DIR)/libc.so $(USER_DIR)/munmaptest.o
$(USER_DIR)/munmaptest.o: $(USER_DIR)/munmaptest.c $(USER_DIR)/libc.h $(USER_DIR)/syscall.h
	$(CC) -std=gnu99 -Os -ffreestanding -nostdlib -m64 -mno-red-zone -I$(USER_DIR) -c $< -o $@
