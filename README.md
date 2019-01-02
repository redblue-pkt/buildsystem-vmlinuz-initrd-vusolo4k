Based on broadcom official repository: https://github.com/Broadcom/stblinux-3.14/commit/1e17f6453693f1e9536b11f23277cf3eb82ff3dd
Build:
wget --no-check-certificate -t6 -T20 -c https://github.com/Broadcom/stbgcc-6.3/releases/download/stbgcc-6.3-1.2/stbgcc-6.3-1.2.i386.tar.bz2 -P /opt/toolchains
tar -xvjf /opt/toolchains/stbgcc-6.3-1.2.i386.tar.bz2 -C /opt/toolchains
export ARCH=arm
export PATH=/opt/toolchains/stbgcc-6.3-1.2/bin:$PATH
export CROSS_COMPILE=arm-linux-gnu-
make defaults-7366c0
make images -j8
cd images
tar -cvf vmlinuz-initrd_vusolo4k_$(date +%Y%m%d).tar vmlinuz-initrd-7366c0
gzip vmlinuz-initrd_vusolo4k_$(date +%Y%m%d).tar

