#!/bin/bash
# Script outline to install and build kernel.
# Author: Siddhant Jajoo.

set -e
set -u

OUTDIR=/tmp/aeld
KERNEL_REPO=git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git
KERNEL_VERSION=v5.15.163
BUSYBOX_VERSION=1_33_1
FINDER_APP_DIR=$(realpath $(dirname $0))
ARCH=arm64
CROSS_COMPILE=aarch64-none-linux-gnu-

if [ $# -lt 1 ]
then
	echo "Using default directory ${OUTDIR} for output"
else
	OUTDIR=$1
	echo "Using passed directory ${OUTDIR} for output"
fi

mkdir -p ${OUTDIR}

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/linux-stable" ]; then
    #Clone only if the repository does not exist.
	echo "CLONING GIT LINUX STABLE VERSION ${KERNEL_VERSION} IN ${OUTDIR}"
	git clone ${KERNEL_REPO} --depth 1 --single-branch --branch ${KERNEL_VERSION}
fi
if [ ! -e ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ]; then
    cd linux-stable
    echo "Checking out version ${KERNEL_VERSION}"
    git checkout ${KERNEL_VERSION}

    # TODO: Add your kernel build steps here
	# 1. Deep clean
	make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} mrproper
	# 2. Build defconfig
	make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} defconfig
	# 3. Build vmlinux
	make -j4 ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} all
	# 4. Build modules
	make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} modules
	# 5. Build devicetree
	make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} dtbs
fi

echo "Adding the Image in outdir"
cp "${OUTDIR}/linux-stable/arch/arm64/boot/Image" ${OUTDIR}

echo "Creating the staging directory for the root filesystem"
cd "$OUTDIR"
if [ -d "${OUTDIR}/rootfs" ]
then
	echo "Deleting rootfs directory at ${OUTDIR}/rootfs and starting over"
    sudo rm  -rf ${OUTDIR}/rootfs
fi

# TODO: Create necessary base directories
cd "$OUTDIR"
mkdir -p rootfs
cd rootfs
mkdir -p bin etc dev home lib lib64 proc sbin sys tmp usr var
mkdir -p usr/bin usr/lib usr/sbin
mkdir -p var/log

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/busybox" ]
then
    cd /tmp
    git clone git://busybox.net/busybox.git
    cp -rL  busybox "${OUTDIR}"
    cd "${OUTDIR}/busybox" 
    git checkout ${BUSYBOX_VERSION}
    # TODO:  Configure busybox
	make distclean
	make defconfig	
else
    cd busybox
fi

# TODO: Make and install busybox
make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE}
make CONFIG_PREFIX="$OUTDIR/rootfs" ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} install

echo "Library dependencies"
# awk command: with the help from ChatGPT
INTERPRETER=$(${CROSS_COMPILE}readelf -a "${OUTDIR}/rootfs/bin/busybox" | grep "program interpreter" | awk -F': ' '{print $2}' | tr -d '[]')
LIBS=$(${CROSS_COMPILE}readelf -a "${OUTDIR}/rootfs/bin/busybox" | grep "Shared library" | awk -F'[][]' '{print $2}')
SYSROOT=$(${CROSS_COMPILE}gcc -print-sysroot)

# TODO: Add library dependencies to rootfs
cp "${SYSROOT}/${INTERPRETER}" "$OUTDIR/rootfs/lib"

# For loop: with the help from ChatGPT
for lib in $LIBS; do
    LIBPATH=$(find "${SYSROOT}" -name "$lib" | head -n 1)
    if [ -n "$LIBPATH" ]; then
        cp "$LIBPATH" "$OUTDIR/rootfs/lib64/"
    else
        echo "Warning: $lib not found in SYSROOT"
    fi
done


# TODO: Make device nodes
cd "$OUTDIR/rootfs"
sudo mknod -m 666 dev/null c 1 3
sudo mknod -m 600 dev/console c 5 1

# TODO: Clean and build the writer utility
cd "$FINDER_APP_DIR"
make clean
make CROSS_COMPILE=${CROSS_COMPILE}

# TODO: Copy the finder related scripts and executables to the /home directory
# on the target rootfs
cd "$FINDER_APP_DIR"
cp writer "$OUTDIR/rootfs/home"
cp finder.sh "$OUTDIR/rootfs/home"
cp -rL conf "$OUTDIR/rootfs/home"
cp finder-test.sh "$OUTDIR/rootfs/home"
cp autorun-qemu.sh "$OUTDIR/rootfs/home"
 
# TODO: Chown the root directory
cd "$OUTDIR/rootfs"
sudo chown -R root:root *

# TODO: Create initramfs.cpio.gz
cd "$OUTDIR/rootfs"
find . | cpio -H newc -ov --owner root:root > ${OUTDIR}/initramfs.cpio
cd ..
gzip initramfs.cpio