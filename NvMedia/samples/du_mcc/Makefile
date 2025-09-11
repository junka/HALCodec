################################### tell Emacs this is a -*- makefile-gmake -*-
#
# Copyright (c) 2022-2023 NVIDIA CORPORATION.  All rights reserved.
#
# NVIDIA CORPORATION and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA CORPORATION is strictly prohibited.
#
# Makefile for compiling du_mcc library for common interface APIs
#
###############################################################################

include ../../make/nvdefs.mk
BASE_DIR := $(NV_PLATFORM_DIR)/samples/du_mcc
BIN_OUTDIR := $(BASE_DIR)

TARGETS = libdu_mcc.so

LOCAL_INCLUDES += -I$(BASE_DIR)/ \
				  -I$(NV_PLATFORM_DIR)/include/ \
				  -I$(NV_PLATFORM_DIR)/samples/NvSocketWrapper/include/

LOCAL_LIBPATHS += -L$(NV_PLATFORM_DIR)/nvidia-bsp/aarch64le/usr/lib \
				  -L$(NV_PLATFORM_DIR)/samples/NvSocketWrapper/

CFLAGS   := $(NV_PLATFORM_OPT) $(NV_PLATFORM_CFLAGS)
CXXFLAGS := $(NV_PLATFORM_OPT) $(NV_PLATFORM_CXXFLAGS)
CPPFLAGS += $(NV_PLATFORM_SDK_INC) $(LOCAL_INCLUDES) $(NV_PLATFORM_CPPFLAGS)
LDFLAGS  += $(NV_PLATFORM_SDK_LIB) \
			$(LOCAL_LIBPATHS) \
			$(NV_PLATFORM_TARGET_LIB) \
			$(NV_PLATFORM_LDFLAGS)

LDLIBS += -lnvsciipc -lnvscievent -lnvsocketwrapper

ifeq ($(NV_PLATFORM_OS), Linux)
    CFLAGS += -DNVSOCKETLIB_LINUX
    LDLIBS += -lpthread -lstdc++ -lrt
else
    CFLAGS += -DNVSOCKETLIB_QNX
    LDLIBS +=  -lc++ -lsocket
endif

CFLAGS += -fPIC
CPPFLAGS += -fPIC

OBJS := du_mcc.o du_mcc_utils.o

$(TARGETS): $(OBJS)
	@echo Linking object files
	$(CC) $(LDFLAGS) -shared -fPIC -o $(BIN_OUTDIR)/$@ $^ $(LDLIBS)

clean:
	rm -rf $(OBJS) $(BIN_OUTDIR)/$(TARGETS)
