#!/bin/sh

destdir=/mnt/usb
splashdev=/dev/mmcblk0p3

startup=/dev/mmcblk0p1

kerneldev=/dev/mmcblk0p1
rootfsdev=/dev/mmcblk0p4
rootfsdevSize=`cat /proc/partitions | grep mmcblk0p4 | awk '{print $3}'`

kerneldev_1=/dev/mmcblk0p4
rootfsdev_1=/dev/mmcblk0p5
rootfsdevSize_1=`cat /proc/partitions | grep mmcblk0p5 | awk '{print $3}'`

kerneldev_2=/dev/mmcblk0p6
rootfsdev_2=/dev/mmcblk0p7
rootfsdevSize_2=`cat /proc/partitions | grep mmcblk0p7 | awk '{print $3}'`

kerneldev_3=/dev/mmcblk0p8
rootfsdev_3=/dev/mmcblk0p9
rootfsdevSize_3=`cat /proc/partitions | grep mmcblk0p9 | awk '{print $3}'`

kerneldev_4=/dev/mmcblk0p10
rootfsdev_4=/dev/mmcblk0p11
rootfsdevSize_4=`cat /proc/partitions | grep mmcblk0p11 | awk '{print $3}'`

ROOTFSDEV_MIN_SIZE=943104	# 921 MB

#ENDSECTOR=`sgdisk -E /dev/mmcblk0`
#ENDSECTOR=$((5746688+1886207))
ENDSECTOR=7632895

REV=3e59d5c

echo "Vuplus Update Start.."
insmod /etc/vfd_proc.ko

VU_MODEL=solo4k

V_ROOTFS_STANDARD_FILENAME=/vuplus/$VU_MODEL/rootfs.tar.bz2

V_ROOTFS_1_FILENAME_BZ2=/vuplus/$VU_MODEL/rootfs1.tar.bz2
V_ROOTFS_2_FILENAME_BZ2=/vuplus/$VU_MODEL/rootfs2.tar.bz2
V_ROOTFS_3_FILENAME_BZ2=/vuplus/$VU_MODEL/rootfs3.tar.bz2
V_ROOTFS_4_FILENAME_BZ2=/vuplus/$VU_MODEL/rootfs4.tar.bz2

V_ROOTFS_1_FILENAME_GZ=/vuplus/$VU_MODEL/rootfs1.tar.gz
V_ROOTFS_2_FILENAME_GZ=/vuplus/$VU_MODEL/rootfs2.tar.gz
V_ROOTFS_3_FILENAME_GZ=/vuplus/$VU_MODEL/rootfs3.tar.gz
V_ROOTFS_4_FILENAME_GZ=/vuplus/$VU_MODEL/rootfs4.tar.gz

V_REBOOT_FILENAME=/vuplus/$VU_MODEL/reboot.update
V_MKPART_FILENAME=/vuplus/$VU_MODEL/mkpart.update

V_KERNEL_STANDARD_FILENAME=/vuplus/$VU_MODEL/kernel_auto.bin

V_KERNEL_1_FILENAME=/vuplus/$VU_MODEL/kernel1_auto.bin
V_KERNEL_2_FILENAME=/vuplus/$VU_MODEL/kernel2_auto.bin
V_KERNEL_3_FILENAME=/vuplus/$VU_MODEL/kernel3_auto.bin
V_KERNEL_4_FILENAME=/vuplus/$VU_MODEL/kernel4_auto.bin

V_SPLASH_FILENAME=/vuplus/$VU_MODEL/splash_auto.bin

V_ENV_BOOT_PART1=/vuplus/$VU_MODEL/STARTUP_1
V_ENV_BOOT_PART2=/vuplus/$VU_MODEL/STARTUP_2
V_ENV_BOOT_PART3=/vuplus/$VU_MODEL/STARTUP_3
V_ENV_BOOT_PART4=/vuplus/$VU_MODEL/STARTUP_4
V_ENV_BOOT_USB_WITH_HDD=/vuplus/$VU_MODEL/STARTUP_USB_HDD
V_ENV_BOOT_USB_WITHOUT_HDD=/vuplus/$VU_MODEL/STARTUP_USB_NOHDD

update_welcome_message () {
	echo -e "Update Script\nVer $REV\n" > /tmp/msg
	cat /tmp/msg > /proc/vfd
	sleep 5;
}

update_error_kernel () {
	echo -e "Updating\nKernel Error!\n" > /tmp/msg
	cat /tmp/msg > /proc/vfd
	exit 0;
}

update_error_rootfs () {
	echo -e "Updating\nRootfs Error!\n" > /tmp/msg
	cat /tmp/msg > /proc/vfd
	exit 0;
}

update_error_part () {
	echo -e "Updating\nPart Error!\n" > /tmp/msg
	cat /tmp/msg > /proc/vfd
	exit 0;
}

mk_partition () {
	echo -e "Creating\nPartition" > /tmp/msg
	cat /tmp/msg > /proc/vfd

	echo -e "Making\nFile System" > /tmp/msg
	cat /tmp/msg > /proc/vfd
	mk_clean_part () {
		sgdisk -o /dev/mmcblk0
	}
	mk_standard_part () {
		sgdisk -a 1 -n 2:20480:45055 -c 2:"initrd" /dev/mmcblk0
		sgdisk -a 1 -n 3:45056:49151 -c 3:"splash" /dev/mmcblk0
		sgdisk -a 1 -n 1:8192:20479 -c 1:"kernel" /dev/mmcblk0
		sgdisk -a 1 -n 4:49152:${ENDSECTOR} -c 4:"rootfs" /dev/mmcblk0
		mkfs.ext4 $1
	}
	mk_multiboot_simple_part () {
		sgdisk -a 1 -n 2:10240:34815 -c 2:"initrd" /dev/mmcblk0
		sgdisk -a 1 -n 3:34816:38911 -c 3:"splash" /dev/mmcblk0
		sgdisk -a 1 -n 1:8192:10239 -c 1:"startup" /dev/mmcblk0
		mkfs.vfat $1
	}
	mk_multiboot_part_1 () {
		sgdisk -a 1 -n 4:38912:51199 -c 4:"kernel_1" /dev/mmcblk0
		sgdisk -a 1 -n 5:51200:1937407 -c 5:"rootfs_1" /dev/mmcblk0
		mkfs.ext4 $1
	}
	mk_multiboot_part_2 () {
		sgdisk -a 1 -n 6:1937408:1949695 -c 6:"kernel_2" /dev/mmcblk0
		sgdisk -a 1 -n 7:1949696:3835903 -c 7:"rootfs_2" /dev/mmcblk0
		mkfs.ext4 $1
	}
	mk_multiboot_part_3 () {
		sgdisk -a 1 -n 8:3835904:3848191 -c 8:"kernel_3" /dev/mmcblk0
		sgdisk -a 1 -n 9:3848192:5734399 -c 9:"rootfs_3" /dev/mmcblk0
		mkfs.ext4 $1
	}
	mk_multiboot_part_4 () {
		sgdisk -a 1 -n 10:5734400:5746687 -c 10:"kernel_4" /dev/mmcblk0
		sgdisk -a 1 -n 11:5746688:${ENDSECTOR} -c 11:"rootfs_4" /dev/mmcblk0
		mkfs.ext4 $1
	}
	if [ -e ${destdir}/$1/$V_ROOTFS_STANDARD_FILENAME ] \
	&& [ ! -e ${destdir}/$1/$V_ROOTFS_1_FILENAME_BZ2 ] \
	&& [ ! -e ${destdir}/$1/$V_ROOTFS_2_FILENAME_BZ2 ] \
	&& [ ! -e ${destdir}/$1/$V_ROOTFS_3_FILENAME_BZ2 ] \
	&& [ ! -e ${destdir}/$1/$V_ROOTFS_4_FILENAME_BZ2 ] \
	&& [ ! -e ${destdir}/$1/$V_ROOTFS_1_FILENAME_GZ ] \
	&& [ ! -e ${destdir}/$1/$V_ROOTFS_2_FILENAME_GZ ] \
	&& [ ! -e ${destdir}/$1/$V_ROOTFS_3_FILENAME_GZ ] \
	&& [ ! -e ${destdir}/$1/$V_ROOTFS_4_FILENAME_GZ ]; then
		mk_clean_part
		mk_standard_part ${rootfsdev}
	elif [ -e ${destdir}/$1/$V_ROOTFS_1_FILENAME_BZ2 ] \
	&& [ -e ${destdir}/$1/$V_ROOTFS_2_FILENAME_BZ2 ] \
	&& [ -e ${destdir}/$1/$V_ROOTFS_3_FILENAME_BZ2 ] \
	&& [ -e ${destdir}/$1/$V_ROOTFS_4_FILENAME_BZ2 ] \
	&& [ -e ${destdir}/$1/$V_ROOTFS_1_FILENAME_GZ ] \
	&& [ -e ${destdir}/$1/$V_ROOTFS_2_FILENAME_GZ ] \
	&& [ -e ${destdir}/$1/$V_ROOTFS_3_FILENAME_GZ ] \
	&& [ -e ${destdir}/$1/$V_ROOTFS_4_FILENAME_GZ ]; then
		update_error_part
	elif [ -e ${destdir}/$1/$V_ROOTFS_1_FILENAME_BZ2 ] \
	|| [ -e ${destdir}/$1/$V_ROOTFS_2_FILENAME_BZ2 ] \
	|| [ -e ${destdir}/$1/$V_ROOTFS_3_FILENAME_BZ2 ] \
	|| [ -e ${destdir}/$1/$V_ROOTFS_4_FILENAME_BZ2 ]; then
		if [ -e ${destdir}/$1/$V_ROOTFS_1_FILENAME_BZ2 ] \
		&& [ -e ${destdir}/$1/$V_ROOTFS_2_FILENAME_BZ2 ] \
		&& [ -e ${destdir}/$1/$V_ROOTFS_3_FILENAME_BZ2 ] \
		&& [ -e ${destdir}/$1/$V_ROOTFS_4_FILENAME_BZ2 ]; then
			mk_clean_part
			mk_multiboot_simple_part ${startup}
		fi
		if [ -e ${destdir}/$1/$V_ROOTFS_1_FILENAME_BZ2 ]; then
			mk_multiboot_part_1 ${rootfsdev_1}
		fi
		if [ -e ${destdir}/$1/$V_ROOTFS_2_FILENAME_BZ2 ]; then
			mk_multiboot_part_2 ${rootfsdev_2}
		fi
		if [ -e ${destdir}/$1/$V_ROOTFS_3_FILENAME_BZ2 ]; then
			mk_multiboot_part_3 ${rootfsdev_3}
		fi
		if [ -e ${destdir}/$1/$V_ROOTFS_4_FILENAME_BZ2 ]; then
			mk_multiboot_part_4 ${rootfsdev_4}
		fi
	elif [ -e ${destdir}/$1/$V_ROOTFS_1_FILENAME_GZ ] \
	|| [ -e ${destdir}/$1/$V_ROOTFS_2_FILENAME_GZ ] \
	|| [ -e ${destdir}/$1/$V_ROOTFS_3_FILENAME_GZ ] \
	|| [ -e ${destdir}/$1/$V_ROOTFS_4_FILENAME_GZ ]; then
		if [ -e ${destdir}/$1/$V_ROOTFS_1_FILENAME_GZ ] \
		&& [ -e ${destdir}/$1/$V_ROOTFS_2_FILENAME_GZ ] \
		&& [ -e ${destdir}/$1/$V_ROOTFS_3_FILENAME_GZ ] \
		&& [ -e ${destdir}/$1/$V_ROOTFS_4_FILENAME_GZ ]; then
			mk_clean_part
			mk_multiboot_simple_part ${startup}
		fi
		if [ -e ${destdir}/$1/$V_ROOTFS_1_FILENAME_GZ ]; then
			mk_multiboot_part_1 ${rootfsdev_1}
		fi
		if [ -e ${destdir}/$1/$V_ROOTFS_2_FILENAME_GZ ]; then
			mk_multiboot_part_2 ${rootfsdev_2}
		fi
		if [ -e ${destdir}/$1/$V_ROOTFS_3_FILENAME_GZ ]; then
			mk_multiboot_part_3 ${rootfsdev_3}
		fi
		if [ -e ${destdir}/$1/$V_ROOTFS_4_FILENAME_GZ ]; then
			mk_multiboot_part_4 ${rootfsdev_4}
		fi
	else
		update_error_part
	fi
}

update_kernel () {
	echo -e "Updating\nKernel" > /tmp/msg
	cat /tmp/msg > /proc/vfd
	if [ -e ${destdir}/$1/$V_KERNEL_STANDARD_FILENAME ] \
	&& [ ! -e ${destdir}/$1/$V_KERNEL_1_FILENAME ] \
	&& [ ! -e ${destdir}/$1/$V_KERNEL_2_FILENAME ] \
	&& [ ! -e ${destdir}/$1/$V_KERNEL_3_FILENAME ] \
	&& [ ! -e ${destdir}/$1/$V_KERNEL_4_FILENAME ]; then
		dd if=${destdir}/$1/$V_KERNEL_STANDARD_FILENAME of=${kerneldev}
	elif [ -e ${destdir}/$1/$V_KERNEL_1_FILENAME_BZ2 ] \
	|| [ -e ${destdir}/$1/$V_KERNEL_2_FILENAME ] \
	|| [ -e ${destdir}/$1/$V_KERNEL_3_FILENAME ] \
	|| [ -e ${destdir}/$1/$V_KERNEL_4_FILENAME ]; then
		if [ -e ${destdir}/$1/$V_KERNEL_1_FILENAME ]; then
			dd if=${destdir}/$1/$V_KERNEL_1_FILENAME of=${kerneldev_1}
		fi
		if [ -e ${destdir}/$1/$V_KERNEL_2_FILENAME ]; then
			dd if=${destdir}/$1/$V_KERNEL_2_FILENAME of=${kerneldev_2}
		fi
		if [ -e ${destdir}/$1/$V_KERNEL_3_FILENAME ]; then
			dd if=${destdir}/$1/$V_KERNEL_3_FILENAME of=${kerneldev_3}
		fi
		if [ -e ${destdir}/$1/$V_KERNEL_4_FILENAME ]; then
			dd if=${destdir}/$1/$V_KERNEL_4_FILENAME of=${kerneldev_4}
		fi
	else
		update_error_kernel
	fi
}

update_splash () {
	echo -e "Updating\nSplash" > /tmp/msg
	cat /tmp/msg > /proc/vfd
	dd if=${destdir}/$1/$V_SPLASH_FILENAME of=${splashdev}
}

update_rootfs () {
	echo "update_rootfs ${destdir}/$1"
	echo -e "Updating\nRootFS" > /tmp/msg
	cat /tmp/msg > /proc/vfd

	if [ -e ${destdir}/$1/$V_ROOTFS_STANDARD_FILENAME ] \
	&& [ ! -e ${destdir}/$1/$V_ROOTFS_1_FILENAME_BZ2 ] \
	&& [ ! -e ${destdir}/$1/$V_ROOTFS_2_FILENAME_BZ2 ] \
	&& [ ! -e ${destdir}/$1/$V_ROOTFS_3_FILENAME_BZ2 ] \
	&& [ ! -e ${destdir}/$1/$V_ROOTFS_4_FILENAME_BZ2 ] \
	&& [ ! -e ${destdir}/$1/$V_ROOTFS_1_FILENAME_GZ ] \
	&& [ ! -e ${destdir}/$1/$V_ROOTFS_2_FILENAME_GZ ] \
	&& [ ! -e ${destdir}/$1/$V_ROOTFS_3_FILENAME_GZ ] \
	&& [ ! -e ${destdir}/$1/$V_ROOTFS_4_FILENAME_GZ ]; then
		mount -t ext4 -o data=journal ${rootfsdev} /mnt/hd
		if [ $? != 0 ]; then
			mkfs.ext4 ${rootfsdev}
			mount -t ext4 -o data=journal ${rootfsdev} /mnt/hd
		fi
		cd /mnt/hd
		rm -rf *
		tar xjf ${destdir}/$1/$V_ROOTFS_STANDARD_FILENAME
		mv romfs/* .
		rmdir romfs
		cd /
		sync
		umount ${rootfsdev}
		echo "update end"
		nvram unset STARTUP
		sleep 0.6
		return 0
	elif [ -e ${destdir}/$1/$V_ROOTFS_1_FILENAME_BZ2 ] \
	|| [ -e ${destdir}/$1/$V_ROOTFS_2_FILENAME_BZ2 ] \
	|| [ -e ${destdir}/$1/$V_ROOTFS_3_FILENAME_BZ2 ] \
	|| [ -e ${destdir}/$1/$V_ROOTFS_4_FILENAME_BZ2 ] \
	|| [ -e ${destdir}/$1/$V_ROOTFS_1_FILENAME_GZ ] \
	|| [ -e ${destdir}/$1/$V_ROOTFS_2_FILENAME_GZ ] \
	|| [ -e ${destdir}/$1/$V_ROOTFS_3_FILENAME_GZ ] \
	|| [ -e ${destdir}/$1/$V_ROOTFS_4_FILENAME_GZ ]; then
		do_first_part () {
			echo -e "Update\nStartup Part\n" > /tmp/msg
			cat /tmp/msg > /proc/vfd
			if [ ! -e /mnt/startup ]; then
				mkdir /mnt/startup
			fi
			mount ${startup} /mnt/startup
			if [ $? != 0 ]; then
				mkfs.vfat ${startup}
				mount ${startup} /mnt/startup
			fi
			if [ $1 == 0 ]; then
				echo "boot emmcflash0.kernel_1 'root=/dev/mmcblk0p5 rw rootwait rootflags=data=journal debug coherent_pool=2M brcm_cma=504M@0x10000000 brcm_cma=260M@0x2f800000 brcm_cma=1024M@0x80000000'" >> /mnt/startup/STARTUP # boot from first partition default
				echo "boot emmcflash0.kernel_1 'root=/dev/mmcblk0p5 rw rootwait rootflags=data=journal debug coherent_pool=2M brcm_cma=504M@0x10000000 brcm_cma=260M@0x2f800000 brcm_cma=1024M@0x80000000'" >> /mnt/startup/STARTUP_1
				echo "boot emmcflash0.kernel_2 'root=/dev/mmcblk0p7 rw rootwait rootflags=data=journal debug coherent_pool=2M brcm_cma=504M@0x10000000 brcm_cma=260M@0x2f800000 brcm_cma=1024M@0x80000000'" >> /mnt/startup/STARTUP_2
				echo "boot emmcflash0.kernel_3 'root=/dev/mmcblk0p9 rw rootwait rootflags=data=journal debug coherent_pool=2M brcm_cma=504M@0x10000000 brcm_cma=260M@0x2f800000 brcm_cma=1024M@0x80000000'" >> /mnt/startup/STARTUP_3
				echo "boot emmcflash0.kernel_4 'root=/dev/mmcblk0p11 rw rootwait rootflags=data=journal debug coherent_pool=2M brcm_cma=504M@0x10000000 brcm_cma=260M@0x2f800000 brcm_cma=1024M@0x80000000'" >> /mnt/startup/STARTUP_4
				echo "boot -fatfs usbdisk0:zImage 'root=/dev/sdb2 rw rootwait rootflags=data=journal rootfstype=ext4 systemd.gpt_auto=0 consoleblank=0 vt.global_cursor_default=0 loglevel=7 coherent_pool=2M brcm_cma=504M@0x10000000 brcm_cma=260M@0x2f800000 brcm_cma=1024M@0x80000000'" >> /mnt/startup/usb_with_hdd
				echo "boot -fatfs usbdisk0:zImage 'root=/dev/sda2 rw rootwait rootflags=data=journal rootfstype=ext4 systemd.gpt_auto=0 consoleblank=0 vt.global_cursor_default=0 loglevel=7 coherent_pool=2M brcm_cma=504M@0x10000000 brcm_cma=260M@0x2f800000 brcm_cma=1024M@0x80000000'" >> /mnt/startup/usb_without_hdd
			elif [ $1 == 1 ]; then
				cp -R $2 /mnt/startup/STARTUP_1
			elif [ $1 == 2 ]; then
				cp -R $2 /mnt/startup/STARTUP_2
			elif [ $1 == 3 ]; then
				cp -R $2 /mnt/startup/STARTUP_3
			elif [ $1 == 4 ]; then
				cp -R $2 /mnt/startup/STARTUP_4
                        elif [ $1 == 5 ]; then
                                cp -R $2 /mnt/startup/STARTUP_USB_HDD
                        elif [ $1 == 6 ]; then
                                cp -R $2 /mnt/startup/STARTUP_USB_NOHDD
			fi

			nvram unset STARTUP
			nvram write STARTUP "batch -fatfs emmcflash0.startup:STARTUP" # set variable STARTUP to nvram
			#return 0
		}
		do_rootfs_part1 () {
			echo -e "Update\nRootFS Part 1\n" > /tmp/msg
			cat /tmp/msg > /proc/vfd
			if [ ! -e /mnt/rootfs_1 ]; then
				mkdir /mnt/rootfs_1
			fi
			mount -t ext4 -o data=journal ${rootfsdev_1} /mnt/rootfs_1
			if [ $? != 0 ]; then
				mkfs.ext4 ${rootfsdev_1}
				mount -t ext4 -o data=journal ${rootfsdev_1} /mnt/rootfs_1
			fi
			cd /mnt/rootfs_1
			rm -rf *
			if [ $1 == bz2 ]; then
				tar xjf $2
			elif [ $1 == gz ]; then
				tar xzf $2
			fi
			mv romfs/* .
			rmdir romfs
			cd /
			sync
			umount ${rootfsdev_1}
			echo "update end"
			sleep 0.6
			#return 0
		}
		do_rootfs_part2 () {
			echo -e "Update\nRootFS Part 2\n" > /tmp/msg
			cat /tmp/msg > /proc/vfd
			if [ ! -e /mnt/rootfs_2 ]; then
				mkdir /mnt/rootfs_2
			fi
			mount -t ext4 -o data=journal ${rootfsdev_2} /mnt/rootfs_2
			if [ $? != 0 ]; then
				mkfs.ext4 ${rootfsdev_2}
				mount -t ext4 -o data=journal ${rootfsdev_2} /mnt/rootfs_2
			fi
			cd /mnt/rootfs_2
			rm -rf *
			if [ $1 == bz2 ]; then
				tar xjf $2
			elif [ $1 == gz ]; then
				tar xzf $2
			fi
			mv romfs/* .
			rmdir romfs
			cd /
			sync
			umount ${rootfsdev_2}
			echo "update end"
			sleep 0.6
			#return 0
		}
		do_rootfs_part3 () {
			echo -e "Update\nRootFS Part 3\n" > /tmp/msg
			cat /tmp/msg > /proc/vfd
			if [ ! -e /mnt/rootfs_3 ]; then
				mkdir /mnt/rootfs_3
			fi
			mount -t ext4 -o data=journal ${rootfsdev_3} /mnt/rootfs_3
			if [ $? != 0 ]; then
				mkfs.ext4 ${rootfsdev_3}
				mount -t ext4 -o data=journal ${rootfsdev_3} /mnt/rootfs_3
			fi
			cd /mnt/rootfs_3
			rm -rf *
			if [ $1 == bz2 ]; then
				tar xjf $2
			elif [ $1 == gz ]; then
				tar xzf $2
			fi
			mv romfs/* .
			rmdir romfs
			cd /
			sync
			umount ${rootfsdev_3}
			echo "update end"
			sleep 0.6
			#return 0
		}
		do_rootfs_part4 () {
			echo -e "Update\nRootFS Part 4\n" > /tmp/msg
			cat /tmp/msg > /proc/vfd
			if [ ! -e /mnt/rootfs_4 ]; then
				mkdir /mnt/rootfs_4
			fi
			mount -t ext4 -o data=journal ${rootfsdev_4} /mnt/rootfs_4
			if [ $? != 0 ]; then
				mkfs.ext4 ${rootfsdev_4}
				mount -t ext4 -o data=journal ${rootfsdev_4} /mnt/rootfs_4
			fi
			cd /mnt/rootfs_4
			rm -rf *
 			if [ $1 == bz2 ]; then
				tar xjf $2
			elif [ $1 == gz ]; then
				tar xzf $2
			fi
			mv romfs/* .
			rmdir romfs
			cd /
			sync
			umount ${rootfsdev_4}
			echo "update end"
			sleep 0.6
			#return 0
		}
		if [ -e ${destdir}/$1/$V_ROOTFS_1_FILENAME_BZ2 ] \
		|| [ -e ${destdir}/$1/$V_ROOTFS_2_FILENAME_BZ2 ] \
		|| [ -e ${destdir}/$1/$V_ROOTFS_3_FILENAME_BZ2 ] \
		|| [ -e ${destdir}/$1/$V_ROOTFS_4_FILENAME_BZ2 ]; then
			if [ -e ${destdir}/$1/$V_ROOTFS_1_FILENAME_BZ2 ] \
			&& [ -e ${destdir}/$1/$V_ROOTFS_2_FILENAME_BZ2 ] \
			&& [ -e ${destdir}/$1/$V_ROOTFS_3_FILENAME_BZ2 ] \
			&& [ -e ${destdir}/$1/$V_ROOTFS_4_FILENAME_BZ2 ]; then
				if [ -e ${destdir}/$1/$V_ENV_BOOT_PART1 ] \
				&& [ -e ${destdir}/$1/$V_ENV_BOOT_PART2 ] \
				&& [ -e ${destdir}/$1/$V_ENV_BOOT_PART3 ] \
				&& [ -e ${destdir}/$1/$V_ENV_BOOT_PART4 ] \
				&& [ -e ${destdir}/$1/$V_ENV_BOOT_USB_WITH_HDD ] \
				&& [ -e ${destdir}/$1/$V_ENV_BOOT_USB_WITHOUT_HDD ]; then
					if [ -e ${destdir}/$1/$V_ENV_BOOT_PART1 ]; then
						do_first_part 1 ${destdir}/$1/$V_ENV_BOOT_PART1
					elif [ -e ${destdir}/$1/$V_ENV_BOOT_PART2 ]; then
						do_first_part 2 ${destdir}/$1/$V_ENV_BOOT_PART2
					elif [ -e ${destdir}/$1/$V_ENV_BOOT_PART3 ]; then
						do_first_part 3 ${destdir}/$1/$V_ENV_BOOT_PART3
					elif [ -e ${destdir}/$1/$V_ENV_BOOT_PART4 ]; then
						do_first_part 4 ${destdir}/$1/$V_ENV_BOOT_PART4
					elif [ -e ${destdir}/$1/$V_ENV_BOOT_USB_WITH_HDD ]; then
						do_first_part 5 $V_ENV_BOOT_USB_WITH_HDD
					elif [ -e ${destdir}/$1/$V_ENV_BOOT_USB_WITHOUT_HDD ]; then
						do_first_part 6 $V_ENV_BOOT_USB_WITHOUT_HDD
					fi
				else
					do_first_part 0
				fi
			fi
			if [ -e ${destdir}/$1/$V_ROOTFS_1_FILENAME_BZ2 ]; then
				do_rootfs_part1 bz2 ${destdir}/$1/$V_ROOTFS_1_FILENAME_BZ2
			fi
			if [ -e ${destdir}/$1/$V_ROOTFS_2_FILENAME_BZ2 ]; then
				do_rootfs_part2 bz2 ${destdir}/$1/$V_ROOTFS_2_FILENAME_BZ2
			fi
			if [ -e ${destdir}/$1/$V_ROOTFS_3_FILENAME_BZ2 ]; then
				do_rootfs_part3 bz2 ${destdir}/$1/$V_ROOTFS_3_FILENAME_BZ2
			fi
			if [ -e ${destdir}/$1/$V_ROOTFS_4_FILENAME_BZ2 ]; then
				do_rootfs_part4 bz2 ${destdir}/$1/$V_ROOTFS_4_FILENAME_BZ2
			fi
			return 0
		elif [ -e ${destdir}/$1/$V_ROOTFS_1_FILENAME_GZ ] \
		|| [ -e ${destdir}/$1/$V_ROOTFS_2_FILENAME_GZ ] \
		|| [ -e ${destdir}/$1/$V_ROOTFS_3_FILENAME_GZ ] \
		|| [ -e ${destdir}/$1/$V_ROOTFS_4_FILENAME_GZ ]; then
			if [ -e ${destdir}/$1/$V_ROOTFS_1_FILENAME_GZ ] \
			&& [ -e ${destdir}/$1/$V_ROOTFS_2_FILENAME_GZ ] \
			&& [ -e ${destdir}/$1/$V_ROOTFS_3_FILENAME_GZ ] \
			&& [ -e ${destdir}/$1/$V_ROOTFS_4_FILENAME_GZ ]; then
				if [ -e ${destdir}/$1/$V_ENV_BOOT_PART1 ] \
				&& [ -e ${destdir}/$1/$V_ENV_BOOT_PART2 ] \
				&& [ -e ${destdir}/$1/$V_ENV_BOOT_PART3 ] \
				&& [ -e ${destdir}/$1/$V_ENV_BOOT_PART4 ] \
				&& [ -e ${destdir}/$1/$V_ENV_BOOT_USB_WITH_HDD ] \
				&& [ -e ${destdir}/$1/$V_ENV_BOOT_USB_WITHOUT_HDD ]; then
					if [ -e ${destdir}/$1/$V_ENV_BOOT_PART1 ]; then
						do_first_part 1 ${destdir}/$1/$V_ENV_BOOT_PART1
					elif [ -e ${destdir}/$1/$V_ENV_BOOT_PART2 ]; then
						do_first_part 2 ${destdir}/$1/$V_ENV_BOOT_PART2
					elif [ -e ${destdir}/$1/$V_ENV_BOOT_PART3 ]; then
						do_first_part 3 ${destdir}/$1/$V_ENV_BOOT_PART3
					elif [ -e ${destdir}/$1/$V_ENV_BOOT_PART4 ]; then
						do_first_part 4 ${destdir}/$1/$V_ENV_BOOT_PART4
					elif [ -e ${destdir}/$1/$V_ENV_BOOT_USB_WITH_HDD ]; then
						do_first_part 5 $V_ENV_BOOT_USB_WITH_HDD
					elif [ -e ${destdir}/$1/$V_ENV_BOOT_USB_WITHOUT_HDD ]; then
						do_first_part 6 $V_ENV_BOOT_USB_WITHOUT_HDD
					fi
				else
					do_first_part 0
				fi
			fi
			if [ -e ${destdir}/$1/$V_ROOTFS_1_FILENAME_GZ ]; then
				do_rootfs_part1 gz ${destdir}/$1/$V_ROOTFS_1_FILENAME_GZ
			fi
			if [ -e ${destdir}/$1/$V_ROOTFS_2_FILENAME_GZ ]; then
				do_rootfs_part2 gz ${destdir}/$1/$V_ROOTFS_2_FILENAME_GZ
			fi
			if [ -e ${destdir}/$1/$V_ROOTFS_3_FILENAME_GZ ]; then
				do_rootfs_part3 gz ${destdir}/$1/$V_ROOTFS_3_FILENAME_GZ
			fi
			if [ -e ${destdir}/$1/$V_ROOTFS_4_FILENAME_GZ ]; then
				do_rootfs_part4 gz ${destdir}/$1/$V_ROOTFS_4_FILENAME_GZ
			fi
			return 0
		fi
	else
		update_error_rootfs
		echo "No update file in ${destdir}/$1"
		return 1
	fi
}

display_vfd_loop () {
	delay_cnt=10
	if [ -e ${destdir}/$1/$V_REBOOT_FILENAME ]; then
		sync
		loop_count=1
		reboot_sec=0
		while [ $loop_count -le $delay_cnt ]
		do
			let loop_count=$loop_count+1
			let reboot_sec=$delay_cnt-$loop_count+1
			echo "Rebooting in $reboot_sec seconds"
			echo -e "Reboot\nin $reboot_sec seconds" > /tmp/msg
			cat /tmp/msg > /proc/vfd
			sleep 0.6
		done
		echo " " > /proc/vfd
		return 0
	fi
	sync
	sync
	while [ 1 ]
	do
		echo "Update Complete"
		echo -e "Update\nComplete" > /tmp/msg
		cat /tmp/msg > /proc/vfd
		sleep 1
		done

	return 0
}

usb_umount()
{
	if grep -qs "^/dev/$1 " /proc/mounts ; then
		umount "${destdir}/$1";
	fi

	[ -d "${destdir}/$1" ] && rmdir "${destdir}/$1"
}

usb_mount()
{
	string=$1

	if [ -e /dev/${string}1 -a ${#string} -eq 3 ]; then
		exit 1
	fi

	mkdir -p "${destdir}/$1" || exit 1

	if ! mount -t auto -o sync "/dev/$1" "${destdir}/$1"; then
# failed to mount, clean up mountpoint
		rmdir "${destdir}/$1"
		exit 1
	fi
}

case "${ACTION}" in
add|"")
	usb_umount ${MDEV}
	usb_mount ${MDEV}

	if [ -e ${destdir}/${MDEV}/$V_ROOTFS_STANDARD_FILENAME ] \
	|| [ -e ${destdir}/${MDEV}/$V_ROOTFS_1_FILENAME_BZ2 ] \
	|| [ -e ${destdir}/${MDEV}/$V_ROOTFS_2_FILENAME_BZ2 ] \
	|| [ -e ${destdir}/${MDEV}/$V_ROOTFS_3_FILENAME_BZ2 ] \
	|| [ -e ${destdir}/${MDEV}/$V_ROOTFS_4_FILENAME_BZ2 ] \
	|| [ -e ${destdir}/${MDEV}/$V_ROOTFS_1_FILENAME_GZ ] \
	|| [ -e ${destdir}/${MDEV}/$V_ROOTFS_2_FILENAME_GZ ] \
	|| [ -e ${destdir}/${MDEV}/$V_ROOTFS_3_FILENAME_GZ ] \
	|| [ -e ${destdir}/${MDEV}/$V_ROOTFS_4_FILENAME_GZ ]; then
		update_welcome_message
		if [ ! -n "$rootfsdevSize" ] \
		|| [ ! -n "$rootfsdevSize_1" ] \
		|| [ ! -n "$rootfsdevSize_2" ] \
		|| [ ! -n "$rootfsdevSize_3" ] \
		|| [ ! -n "$rootfsdevSize_4" ]; then
			rootfsdevSize=0
			rootfsdevSize_1=0
			rootfsdevSize_2=0
			rootfsdevSize_3=0
			rootfsdevSize_4=0
		fi

		if [ ! -b ${rootfsdev} -o -e ${destdir}/${MDEV}/$V_MKPART_FILENAME -o ${rootfsdevSize} -lt ${ROOTFSDEV_MIN_SIZE} ] \
		|| [ ! -b ${rootfsdev_1} -o -e ${destdir}/${MDEV}/$V_MKPART_FILENAME -o ${rootfsdevSize_1} -lt ${ROOTFSDEV_MIN_SIZE} ] \
		|| [ ! -b ${rootfsdev_2} -o -e ${destdir}/${MDEV}/$V_MKPART_FILENAME -o ${rootfsdevSize_2} -lt ${ROOTFSDEV_MIN_SIZE} ] \
		|| [ ! -b ${rootfsdev_3} -o -e ${destdir}/${MDEV}/$V_MKPART_FILENAME -o ${rootfsdevSize_3} -lt ${ROOTFSDEV_MIN_SIZE} ] \
		|| [ ! -b ${rootfsdev_4} -o -e ${destdir}/${MDEV}/$V_MKPART_FILENAME -o ${rootfsdevSize_4} -lt ${ROOTFSDEV_MIN_SIZE} ]; then
			mk_partition ${MDEV}
			update_kernel ${MDEV}
			update_splash ${MDEV}
		fi

		update_rootfs ${MDEV}
		display_vfd_loop ${MDEV}
		reboot
	fi
	;;
remove)
	usb_umount ${MDEV}
	;;
esac
