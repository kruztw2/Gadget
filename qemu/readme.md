# How to set up QEMU with busybox

## install busybox

[busybox index](https://www.busybox.net/downloads/)

```shell=
$ wget https://busybox.net/downloads/busybox-<version>.tar.bz2
$ tar xvf busybox-<version>.tar.bz2
$ cd busybox-<version>
$ make menuconfig # test version: 1-24.0
Busybox Settings --->
    Build Options --->
        [*] Build BusyBox as a static binary (no shared libs)
$ make install
```

## install linux
```shell=
$ git clone https://github.com/torvalds/linux
$ cd linux
$ git checkout <version>  # Use `git tag` to check available
$ make menuconfig   # test version: v5.6
Kernel hacking --->
    Compile-time checks and compiler options --->
        [*] Compile the kernel with debug info
$ make -j$(nproc)
```

## move each components into one directory
```shell=
$ cp <linux_dir>/arch/x86/boot/bzImage <dir>  # create <dir> anywhere you want
$ cp <busybox_dir>/_install <dir> -r
$ cp run.sh <dir>
$ chmod +x <dir>/run.sh
```

## setup init, make cpio and run
```shell=
cd <dir>
cp init _install && cd _install
chmod +x ./init
find . | cpio -o --format=newc > ../rootfs.cpio && cd ..
./run.sh
```