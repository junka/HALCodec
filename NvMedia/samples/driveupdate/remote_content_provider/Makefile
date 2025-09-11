# Copyright (c) 2020-2021, NVIDIA Corporation.  All rights reserved.
#
# NVIDIA Corporation and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an
# express license agreement from NVIDIA Corporation is strictly prohibited.

include ../../../make/nvdefs.mk

TARGETS = remote_content_provider

NV_DU_INCLUDE := -I$(NV_PLATFORM_DIR)/include/driveupdate	\
				 -I$(NV_PLATFORM_DIR)/samples/driveupdate

CFLAGS   := $(NV_PLATFORM_SDK_INC) $(NV_PLATFORM_OPT) $(NV_PLATFORM_CFLAGS) $(NV_DU_INCLUDE)
LDFLAGS  := $(NV_PLATFORM_SDK_LIB) $(NV_PLATFORM_TARGET_LIB) $(NV_PLATFORM_LDFLAGS)

LDLIBS += -lcurl -lnvdulink -lnvduplugin

OBJS    := remote_content_provider.o

$(TARGETS):$(OBJS)
	$(LD) $(LDFLAGS) -o $@ $^ $(LDLIBS)

clobber clean :
	rm -rf $(OBJS) $(TARGETS)
