#!/bin/sh
PREBUILT=/home/sky/android/android-ndk-r8/toolchains/arm-linux-androideabi-4.4.3/prebuilt/linux-x86
PLATFORM=/home/sky/android/android-ndk-r8/platforms/android-14/arch-arm

./configure --target-os=linux \
	--arch=arm \
	--disable-static \
	--enable-gpl \
	--enable-version3 \
	--enable-nonfree \
	--disable-doc \
	--disable-ffmpeg \
	--enable-ffplay \
	--disable-ffprobe \
	--disable-ffserver \
	--disable-avdevice \
	--disable-avfilter \
	--disable-postproc \
	--enable-small \
	--enable-shared \
	--enable-cross-compile \
	--cc=$PREBUILT/bin/arm-linux-androideabi-gcc \
	--cross-prefix=$PREBUILT/bin/arm-linux-androideabi- \
	--strip=$PREBUILT/bin/arm-linux-androideabi-strip \
	--extra-cflags="-fPIC -DANDROID" \
	--extra-ldflags="-Wl,-T,$PREBUILT/arm-linux-androideabi/lib/ldscripts/armelf_linux_eabi.x -Wl,-rpath-link=$PLATFORM/usr/lib -L$PLATFORM/usr/lib -nostdlib $PREBUILT/lib/gcc/arm-linux-androideabi/4.4.3/crtbegin.o $PREBUILT/lib/gcc/arm-linux-androideabi/4.4.3/crtend.o -lc -lm -ldl" \
	$FFCONFIG_COMMON
