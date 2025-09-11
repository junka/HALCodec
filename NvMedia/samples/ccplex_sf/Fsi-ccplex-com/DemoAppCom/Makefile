#############################################################################
#
# Copyright (c) 2021 NVIDIA CORPORATION.  All Rights Reserved.
#
# NVIDIA CORPORATION and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA CORPORATION is strictly prohibited.
#
###############################################################################

include ../../../../make/nvdefs.mk

ODIR = $(NV_PLATFORM_DIR)/filesystem/contents/bin
DEMO_TARGET = $(ODIR)/DemoAppCom

CFLAGS += -g -Wall $(NV_PLATFORM_OPT) $(NV_PLATFORM_CFLAGS)
CPPFLAGS = $(NV_PLATFORM_SDK_INC) $(NV_PLATFORM_CPPFLAGS)
LDFLAGS  := \
	$(NV_PLATFORM_SDK_LIB) \
	$(NV_PLATFORM_LDFLAGS) \
	-L$(NV_PLATFORM_DIR)/nvidia-bsp/aarch64le/usr/lib
LDLIBS   := \
	-lNvFsiCom \
	-lpthread

OBJS    := \
	./DemoAppCom.o

$(DEMO_TARGET): $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $^ $(LDLIBS)

clean clobber:
	rm -rf $(OBJS) $(DEMO_TARGET)
