#!/bin/bash
GCC_SUBVER="1.4"
GCC_VER="stbgcc-6.3"
CROSS_GCC=$GCC_VER-$GCC_SUBVER
HOST_ARCH="i386"
TOOLCHAINS_DIR="/opt/toolchains"
export ARCH=arm
export PATH=$TOOLCHAINS_DIR/$CROSS_GCC/bin:$PATH
export CROSS_COMPILE=arm-linux-gnueabihf-
