##############################################################################
#
# Copyright (c) 2021, NVIDIA CORPORATION.  All Rights Reserved.
#
# NVIDIA CORPORATION and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA CORPORATION is strictly prohibited.
#
#
###############################################################################

include ../../../make/nvdefs.mk
TARGETS = efs_key
NV_PKCS11_INCLUDE := -I $(NV_PLATFORM_DIR)/include/nvpkcs11
CFLAGS   := $(NV_PLATFORM_SDK_INC) $(NV_PLATFORM_OPT) $(NV_PLATFORM_CFLAGS) $(NV_PKCS11_INCLUDE) -Iinclude
CPPFLAGS := $(NV_PLATFORM_SDK_INC) $(NV_PLATFORM_BSP_INC) $(NV_PLATFORM_CPPFLAGS)
LDFLAGS  := $(NV_PLATFORM_SDK_LIB) $(NV_PLATFORM_TARGET_LIB) $(NV_PLATFORM_LDFLAGS)
LDLIBS   += -lnvpkcs11
OBJS     := efs_key.o efs_key_debug.o

$(TARGETS): $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $^ $(LDLIBS)

clobber clean :
	rm -rf $(OBJS) $(TARGETS)
