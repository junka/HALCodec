################################### tell Emacs this is a -*- makefile-gmake -*-
#
# Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
#
# NVIDIA CORPORATION and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA CORPORATION is strictly prohibited.
#
# Makefile for compiling mcc_daemon application
#
###############################################################################

include ../../make/nvdefs.mk
BASE_DIR := $(NV_PLATFORM_DIR)/samples/mcc_daemon/
BIN_OUTDIR := $(BASE_DIR)

TARGETS = mcc_daemon

LOCAL_INCLUDES += -I$(NV_PLATFORM_DIR)/include/
LOCAL_INCLUDES += -I$(NV_PLATFORM_DIR)/samples/NvSocketWrapper/include/
LOCAL_INCLUDES += -I$(BASE_DIR)/

LOCAL_LIBPATHS += -L$(NV_PLATFORM_DIR)/nvidia-bsp/aarch64le/usr/lib
LOCAL_LIBPATHS += -L$(NV_PLATFORM_DIR)/samples/NvSocketWrapper/

CFLAGS   := $(NV_PLATFORM_OPT) $(NV_PLATFORM_CFLAGS)
CXXFLAGS := $(NV_PLATFORM_OPT) $(NV_PLATFORM_CXXFLAGS)
CPPFLAGS += $(NV_PLATFORM_SDK_INC) $(LOCAL_INCLUDES) $(NV_PLATFORM_CPPFLAGS)
LDFLAGS  += $(NV_PLATFORM_SDK_LIB) $(LOCAL_LIBPATHS) $(NV_PLATFORM_TARGET_LIB) $(NV_PLATFORM_LDFLAGS)

LDLIBS += -lnvsciipc
LDLIBS += -lnvscievent
LDLIBS += -lnvsocketwrapper

OBJS := daemon.o
OBJS += ipc.o
OBJS += nvsocket.o

ifeq ($(NV_PLATFORM_OS), Linux)
  LDLIBS  += -pthread
endif

$(TARGETS): $(OBJS)
	@echo Linking object files
	$(LD) $(LDFLAGS) -o $(BIN_OUTDIR)/$@ $^ $(LDLIBS)

clean clobber:
	rm -rf $(OBJS) $(BIN_OUTDIR)/$(TARGETS)
