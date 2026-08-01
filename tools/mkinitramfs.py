#!/usr/bin/env python3
"""Generate an initramfs cpio archive with minimal TRAILER entry."""
import struct
import sys
import os

def format_hex(val):
    return format(val, '08x')

def cpio_add_file(name, data):
    namesize = len(name) + 1  # include null terminator
    filesize = len(data)
    
    header = b'070701'
    header += format_hex(0).encode()      # ino
    header += format_hex(0o100755).encode() # mode
    header += format_hex(0).encode()       # uid
    header += format_hex(0).encode()       # gid
    header += format_hex(1).encode()       # nlink
    header += format_hex(0).encode()       # mtime
    header += format_hex(len(data)).encode() # filesize
    header += format_hex(0).encode()       # devmajor
    header += format_hex(0).encode()       # devminor
    header += format_hex(0).encode()       # rdevmajor
    header += format_hex(0).encode()       # rdevminor
    header += format_hex(namesize).encode() # namesize
    header += format_hex(0).encode()       # check
    
    entry = header + name.encode() + b'\x00'
    while len(entry) % 4:
        entry += b'\x00'
    entry += data
    while len(entry) % 4:
        entry += b'\x00'
    return entry

def cpio_trailer():
    name = "TRAILER!!!"
    return cpio_add_file(name, b'')

def main():
    outpath = sys.argv[1] if len(sys.argv) > 1 else 'initramfs.cpio'
    mode = sys.argv[2] if len(sys.argv) > 2 else 'binary'
    
    # Create cpio archive
    data = b''
    
    # Add ELF binaries if any
    usrdir = os.path.join(os.path.dirname(os.path.dirname(__file__)), 'user')
    if os.path.isdir(usrdir):
        for f in sorted(os.listdir(usrdir)):
            if f.endswith('.elf') or f.endswith('.so') or f in ('crt0.o', 'libc.o', 'va_list.o'):   # .so = shared libs (v5.8.28+); crt0.o+libc.o = in-OS tcc link inputs (v6.2.3+); va_list.o = tcc va_arg runtime (v6.3.5)
                fpath = os.path.join(usrdir, f)
                with open(fpath, 'rb') as fp:
                    elfdata = fp.read()
                data += cpio_add_file(f, elfdata)
                print(f"Added {f} ({len(elfdata)} bytes)")

    # Ship the tcc OS-adapter + libtcc1 runtime OBJECTS (GCC-built, NyxOS target) at the
    # initramfs root, so the in-OS `cc` can LINK the tcc-built tcc_self.o into a runnable
    # tcc_self.elf:  cc /mnt/tcc_self.o /nyxshim.o /libtcc1.o -o /mnt/tcc_self.elf  (v6.4.11).
    # nyxshim.o supplies tcc's POSIX glue (read/write/open/exit/strtod/sscanf/...); libtcc1.o
    # supplies the long-double float<->int helpers tcc's codegen emits (__floatundixf/
    # __fixunsxfdi). They live in user/tcc/ (a subdir the scan above does not walk), so ship
    # them explicitly. abort() was dropped from nyxshim so it no longer collides with libc.o.
    for objrel, objname in (('tcc/nyxshim.o', 'nyxshim.o'), ('tcc/libtcc1.o', 'libtcc1.o')):
        op = os.path.join(usrdir, objrel)
        if os.path.isfile(op):
            with open(op, 'rb') as fp:
                od = fp.read()
            data += cpio_add_file(objname, od)
            print(f"Added {objname} ({len(od)} bytes)")

    # Ship tcc's freestanding system headers under /usr/lib/tcc/include so the in-OS
    # tcc (CONFIG_TCCDIR=/usr/lib/tcc) + `cc -I/usr/lib/tcc/include` can find <stddef.h>,
    # <stdbool.h>, <stdarg.h>, ... (v6.3.4). These are small text headers; the initramfs
    # loader creates the nested parent dirs (initramfs_mkparents).
    incdir = os.path.join(usrdir, 'tcc', 'include')
    if os.path.isdir(incdir):
        for f in sorted(os.listdir(incdir)):
            if f.endswith('.h'):
                with open(os.path.join(incdir, f), 'rb') as fp:
                    hdrdata = fp.read()
                data += cpio_add_file('usr/lib/tcc/include/' + f, hdrdata)
                print(f"Added usr/lib/tcc/include/{f} ({len(hdrdata)} bytes)")

    # Ship the NyxOS libc sources under /usr/src/nyx so the in-OS tcc can compile the
    # OS's own libc -- the self-hosting milestone (v6.4.0). All three sit in one dir so
    # libc.c's `#include "libc.h"` and libc.h's `#include "syscall.h"` (quote-includes,
    # which search the including file's directory) resolve without extra -I.
    for f in ('libc.c', 'libc.h', 'syscall.h'):
        p = os.path.join(usrdir, f)
        if os.path.isfile(p):
            with open(p, 'rb') as fp:
                srcdata = fp.read()
            data += cpio_add_file('usr/src/nyx/' + f, srcdata)
            print(f"Added usr/src/nyx/{f} ({len(srcdata)} bytes)")

    # Also ship tcc's own va-runtime source (its libtcc1 va_list.c) into the same dir, so
    # the in-OS tcc can compile a piece of tcc itself -- `cc --self-libc` self-compiles it
    # alongside libc.c to build a program whose whole C runtime is tcc-produced (v6.4.2).
    vlp = os.path.join(usrdir, 'tcc', 'lib', 'va_list.c')
    if os.path.isfile(vlp):
        with open(vlp, 'rb') as fp:
            vldata = fp.read()
        data += cpio_add_file('usr/src/nyx/va_list.c', vldata)
        print(f"Added usr/src/nyx/va_list.c ({len(vldata)} bytes)")

    # And ship tcc's main runtime library source (its libtcc1.c) into the same dir, so the
    # in-OS tcc can compile the REST of tcc's own runtime -- `cc --self-libc` self-compiles it
    # too, alongside libc.c + va_list.c, so a program's whole C runtime including the float
    # <-> 64-bit-int helpers (__floatundidf/__floatundisf/__fix*di, which tcc's own codegen
    # lowers unsigned-64<->float conversions to) is tcc-produced (v6.4.5). libtcc1.c is
    # header-free, so it needs no include path.
    ltp = os.path.join(usrdir, 'tcc', 'lib', 'libtcc1.c')
    if os.path.isfile(ltp):
        with open(ltp, 'rb') as fp:
            ltdata = fp.read()
        data += cpio_add_file('usr/src/nyx/libtcc1.c', ltdata)
        print(f"Added usr/src/nyx/libtcc1.c ({len(ltdata)} bytes)")

    # Ship the OS's own shell source too (its 1042-line userspace shell), so the in-OS
    # tcc can rebuild the shell from source -- `cc /usr/src/nyx/sh.c -o sh` compiles the
    # OS's biggest real program (fork/execve/pipe/dup2/waitpid, quoting, $(...), jobs) and
    # the result runs byte-identical to the shipped GCC-built /sh.elf (v6.4.4). sh.c's
    # `#include "libc.h"` resolves against this same dir (quote-include search).
    shp = os.path.join(usrdir, 'sh.c')
    if os.path.isfile(shp):
        with open(shp, 'rb') as fp:
            shdata = fp.read()
        data += cpio_add_file('usr/src/nyx/sh.c', shdata)
        print(f"Added usr/src/nyx/sh.c ({len(shdata)} bytes)")

    # Ship a couple of real coreutil sources too, so the in-OS tcc can rebuild them from
    # source and the results run byte-identical to the shipped GCC builds (v6.4.7): grep
    # (substring match: strstr, multi-file "file:" prefixing) and sort (a static-.bss line
    # buffer + insertion sort over strcmp). Both `#include "libc.h"` from this same dir.
    for cu in ('grep.c', 'sort.c', 'hello_cc.c'):
        cup = os.path.join(usrdir, cu)
        if os.path.isfile(cup):
            with open(cup, 'rb') as fp:
                cudata = fp.read()
            data += cpio_add_file('usr/src/nyx/' + cu, cudata)
            print(f"Added usr/src/nyx/{cu} ({len(cudata)} bytes)")

    # Ship tcc's OWN source tree so the in-OS tcc can compile tcc itself -- the
    # tcc-compiles-tcc self-host arc (v6.4.9). Only the files tcc.c actually pulls in
    # the x86_64/ELF ONE_SOURCE build (found via `tcc -E`), placed under
    # /usr/src/nyx/tcc/ mirroring user/tcc/. <stdarg.h> etc. resolve from the already-
    # shipped /usr/lib/tcc/include, and "libc.h" from /usr/src/nyx, so neither
    # user/tcc/include/ nor user/libc.h is re-shipped here.
    tccdir = os.path.join(usrdir, 'tcc')
    tcc_top = ['tcc.c', 'tcc.h', 'libtcc.c', 'libtcc.h', 'tccpp.c', 'tccgen.c',
               'tccelf.c', 'tccasm.c', 'tccrun.c', 'tcctools.c', 'x86_64-gen.c',
               'x86_64-link.c', 'i386-asm.c', 'elf.h', 'stab.h', 'stab.def',
               'tcctok.h', 'i386-tok.h', 'x86_64-asm.h']
    for f in tcc_top:
        p = os.path.join(tccdir, f)
        if os.path.isfile(p):
            with open(p, 'rb') as fp:
                d = fp.read()
            data += cpio_add_file('usr/src/nyx/tcc/' + f, d)
            print(f"Added usr/src/nyx/tcc/{f} ({len(d)} bytes)")
    # The nyxshim standard-header shims (+ config.h + sys/) live under tcc/nyxshim/.
    shimdir = os.path.join(tccdir, 'nyxshim')
    if os.path.isdir(shimdir):
        for root, _dirs, files in os.walk(shimdir):
            for f in sorted(files):
                if f.endswith('.h'):
                    full = os.path.join(root, f)
                    rel = os.path.relpath(full, tccdir).replace('\\', '/')
                    with open(full, 'rb') as fp:
                        d = fp.read()
                    data += cpio_add_file('usr/src/nyx/tcc/' + rel, d)
                    print(f"Added usr/src/nyx/tcc/{rel} ({len(d)} bytes)")

    # Ship the package repository (pkg manager, v6.4.19): each user/pkg/<name>/ directory
    # becomes /usr/pkg/<name>/ in the initramfs (a `recipe` manifest + the package's
    # source), so `pkg install <name>` can read the recipe and compile the source in-OS
    # with `cc`. The loader creates the nested parent dirs.
    pkgdir = os.path.join(usrdir, 'pkg')
    if os.path.isdir(pkgdir):
        for root, _dirs, files in os.walk(pkgdir):
            for f in sorted(files):
                full = os.path.join(root, f)
                rel = os.path.relpath(full, usrdir).replace('\\', '/')   # e.g. pkg/hello/recipe
                with open(full, 'rb') as fp:
                    d = fp.read()
                data += cpio_add_file('usr/' + rel, d)                    # -> usr/pkg/hello/recipe
                print(f"Added usr/{rel} ({len(d)} bytes)")

    # Add trailer
    data += cpio_trailer()
    
    if mode == 'c':
        # Output C file
        with open(outpath, 'w') as fp:
            fp.write('// Generated initramfs data\n')
            fp.write(f'#ifndef _INITRAMFS_DATA_H\n#define _INITRAMFS_DATA_H\n\n')
            fp.write(f'static const unsigned char initramfs_data[] = {{\n    ')
            for i, b in enumerate(data):
                fp.write(f'0x{b:02x}, ')
                if (i + 1) % 16 == 0:
                    fp.write('\n    ')
            fp.write('\n};\n')
            fp.write(f'#define INITRAMFS_SIZE {len(data)}u\n')
            fp.write('#endif\n')
        print(f"Created C header {outpath} ({len(data)} bytes)")
    else:
        with open(outpath, 'wb') as fp:
            fp.write(data)
        print(f"Created {outpath} ({len(data)} bytes)")

if __name__ == '__main__':
    main()
