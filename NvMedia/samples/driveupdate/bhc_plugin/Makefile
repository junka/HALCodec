# Copyright (c) 2020, NVIDIA Corporation.  All rights reserved.
#
# NVIDIA Corporation and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an
# express license agreement from NVIDIA Corporation is strictly prohibited.

include ../../../make/nvdefs.mk

TARGETS = libnvdubhc_api
SOURCES = dubhc_api.c

NV_DU_INCLUDE := -I$(NV_PLATFORM_DIR)/sample/driveupdate/bhc_plugin

CFLAGS   := $(NV_PLATFORM_SDK_INC) $(NV_PLATFORM_OPT) $(NV_PLATFORM_CFLAGS) $(NV_DU_INCLUDE)

ifeq ($(DU_BHC_FAILURE),1)
	CFLAGS += -DDU_BHC_FAILURE
endif

OBJS    := dubhc_api.o

all: $(TARGETS).so

$(OBJS): $(SOURCES)
	$(CC) -c $(CFLAGS) $^

$(TARGETS).so: $(OBJS)
	$(CROSSBIN)ld -shared --soname $(TARGETS).so $^ -o $@

clobber clean :
	rm -rf $(OBJS) $(TARGETS).so
