#!/bin/sh

qemu-system-x86_64 -cpu kvm64 -m 64M -kernel ./bzImage -initrd ./rootfs.cpio \
-append "root=/dev/ram rw console=ttyS0 oops=panic panic=1 quiet" \
-nographic -no-reboot
