# 
# File	         Makefile
# Title          Services kbuild Makefile
#
# Copyright      2007 by Imagination Technologies Limited.
#                All rights reserved.  No part of this software, either
#                material or conceptual may be copied or distributed,
#                transmitted, transcribed, stored in a retrieval system
#                or translated into any human or computer language in any
#                form by any means, electronic, mechanical, manual or
#                other-wise, or disclosed to third parties without the
#                express written permission of Imagination Technologies
#                Limited, Unit 8, HomePark Industrial Estate,
#                King's Langley, Hertfordshire, WD4 8LZ, U.K.
#
# $Log: Makefile $
#

include $(EURASIAROOT)/eurasiacon/build/linux/kbuild/Makefile.kbuild_subdir_common

MODULE		= s3c_lcd

MODULE_CFLAGS = -DDC_NOHW_DISCONTIG_BUFFERS

INCLUDES = 	-I$(EURASIAROOT)/include4 \
			-I$(EURASIAROOT)/services4/include \
			-I$(EURASIAROOT)/services4/system/$(PVR_SYSTEM) \
			-I$(EURASIAROOT)/services4/system/include \

SOURCES	      = ../s3c_displayclass.c \
				../s3c_lcd.c




