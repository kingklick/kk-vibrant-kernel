################################################################################

1. How to Build
	- get Toolchain
		From Codesourcery site(	http://www.codesourcery.com )
		Ex) Download : http://www.codesourcery.com/sgpp/lite/arm/portal/package6493/public/arm-none-eabi/arm-2010q1-188-arm-none-eabi-i686-pc-linux-gnu.tar.bz2

		recommand : up to 2009q3 version.
					Feature : ARM
					Target OS : "EABI"
					package : "IA32 GNU/Linux TAR"

	- edit build_kernel.sh
		edit "TOOLCHAIN" and "TOOLCHAIN_PREFIX" to right toolchain path(You downloaded).
		Ex)  30 TOOLCHAIN=`pwd`/../arm-2010q1/bin/		// `pwd` means current path.
			 31 TOOLCHAIN_PREFIX=arm-none-eabi-			// You have to check.

	- run build_kernel.sh
		$ ./build_kernel.sh

2. Output files
	- Kernel : linux-2.6.29/arch/arm/boot/zImage
	- module : modules/*/*.ko

3. How to Clean
	- run build_kernel.sh Clean
		$ ./build_kernel.sh Clean

################################################################################
