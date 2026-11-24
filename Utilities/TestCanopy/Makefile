## @file
# Runs the OpenCanopy boot picker on the host (see Canopy.c).
# Copyright (C) 2026, DrogaBox. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause
##

PROJECT = Canopy
PRODUCT = $(PROJECT)$(INFIX)$(SUFFIX)
OBJS    = $(PROJECT).o
#
# From OpenCanopy, the code which is tested (input is replaced by Canopy.c, output runs on a fake GOP).
#
OBJS   += OpenCanopy.o GuiApp.o OcBootstrap.o BitmapFont.o Images.o Blending.o Common.o BootPicker.o Password.o OutputStGop.o
#
# From OpenCore.
#
OBJS   += OcPng.o lodepng.o OcCompressionLib.o OcTimerLib.o

VPATH   = ../../Platform/OpenCanopy:$\
          ../../Platform/OpenCanopy/Views:$\
          ../../Platform/OpenCanopy/Output:$\
          ../../Library/OcPngLib:$\
          ../../Library/OcCompressionLib:$\
          ../../Library/OcTimerLib

include ../../User/Makefile

CFLAGS += -I../../Platform/OpenCanopy
#
# OpenCanopy reads the theme through OcStorageLib, replaced here with a directory of the host.
#
CFLAGS += -DOcStorageReadFileUnicode=TestStorageReadFileUnicode \
          -DOcStorageExistsFileUnicode=TestStorageExistsFileUnicode
#
# The draw loop paces itself to 60 frames per second of the TSC; the harness runs frames back to back.
#
%/OpenCanopy.o: CFLAGS += -DOcGetTSCFrequency=TestGetTSCFrequency

ifeq ($(SANITIZE),1)
	SHARED_CFLAGS	+= -fsanitize=undefined,address -D SANITIZE_TEST
	LDFLAGS		+= -fsanitize=undefined,address
endif
