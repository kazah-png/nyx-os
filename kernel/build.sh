#!/bin/bash
export PATH="/mnt/c/Users/kzh/Desktop/Proyectos/nyx-os/cross/bin:$PATH"
cd /mnt/c/Users/kzh/Desktop/Proyectos/nyx-os/kernel
make clean 2>&1
make -j4 2>&1
