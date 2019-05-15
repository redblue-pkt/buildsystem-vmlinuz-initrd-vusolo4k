#!/bin/bash
source toolchain_helper.sh
if [ ! -e $TOOLCHAINS_DIR/$CROSS_GCC/ ]; then
	wget --no-check-certificate -t6 -T20 -c https://github.com/Broadcom/$GCC_VER/releases/download/$CROSS_GCC/$CROSS_GCC.$HOST_ARCH.tar.bz2 -P $TOOLCHAINS_DIR
	tar -xvjf $TOOLCHAINS_DIR/$CROSS_GCC.$HOST_ARCH.tar.bz2 -C $TOOLCHAINS_DIR
fi
