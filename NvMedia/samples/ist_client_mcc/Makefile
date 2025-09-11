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
# Makefile for compiling sample ist_client_mcc library
#
###############################################################################

include ../../make/nvdefs.mk
BASE_DIR := $(NV_PLATFORM_DIR)/samples/ist_client_mcc
BIN_OUTDIR := $(BASE_DIR)

TARGETS := libist_client_mcc.so

OBJS := ist_client_mcc.o \
	ist_client_mcc_ipc.o

CFLAGS   := $(NV_PLATFORM_OPT) $(NV_PLATFORM_CFLAGS)
CXXFLAGS := $(NV_PLATFORM_OPT) $(NV_PLATFORM_CXXFLAGS)
CPPFLAGS += $(NV_PLATFORM_SDK_INC) $(NV_PLATFORM_CPPFLAGS)
LDFLAGS  += $(NV_PLATFORM_SDK_LIB) $(NV_PLATFORM_TARGET_LIB) $(NV_PLATFORM_LDFLAGS)

# Shared library build flags
CFLAGS   += -fpic
CXXFLAGS += -fpic
LDFLAGS  += -shared

LDLIBS += -lnvsciipc -lnvscievent

#-------------------------------------------------------------------------------
# Pull in nvos lib for logging APIs
#-------------------------------------------------------------------------------
ifeq ($(NV_BUILD_CONFIGURATION_OS_IS_QNX),1)
	LDLIBS += -lnvos_s3_safety
else
	LDLIBS += -lnvos
endif

$(TARGETS): $(OBJS)
	@echo Linking object files
	$(LD) $(LDFLAGS) -o $(BIN_OUTDIR)/$@ $^ $(LDLIBS)

clean clobber:
	rm -rf $(OBJS) $(BIN_OUTDIR)/$(TARGETS)
