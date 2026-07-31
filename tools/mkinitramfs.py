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
            if f.endswith('.elf') or f.endswith('.so') or f in ('crt0.o', 'libc.o'):   # .so = shared libs (v5.8.28+); crt0.o+libc.o = in-OS tcc link inputs (v6.2.3+)
                fpath = os.path.join(usrdir, f)
                with open(fpath, 'rb') as fp:
                    elfdata = fp.read()
                data += cpio_add_file(f, elfdata)
                print(f"Added {f} ({len(elfdata)} bytes)")

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
