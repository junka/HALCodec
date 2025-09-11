##############################################################################
#
# Copyright (c) 2020-2024, NVIDIA CORPORATION.  All Rights Reserved.
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
TARGETS = pkcs11_reference_application
NV_PKCS11_INCLUDE := -I $(NV_PLATFORM_DIR)/include/nvpkcs11
CFLAGS   := $(NV_PLATFORM_SDK_INC) $(NV_PLATFORM_OPT) $(NV_PLATFORM_CFLAGS) $(NV_PKCS11_INCLUDE) -Iinclude
CPPFLAGS := $(NV_PLATFORM_SDK_INC) $(NV_PLATFORM_BSP_INC) $(NV_PLATFORM_CPPFLAGS)
LDFLAGS  := $(NV_PLATFORM_SDK_LIB) $(NV_PLATFORM_TARGET_LIB) $(NV_PLATFORM_LDFLAGS)
LDLIBS   += -lnvpkcs11
OBJS     += src/eddsa_verify_reference.o
OBJS     += src/gcm_enc_dec_reference_code.o
OBJS     += src/digest_reference_code.o
OBJS     += src/key_object_setup_reference_code.o
OBJS     += src/pkcs11_reference_apps_code.o
OBJS     += src/unwrap_reference_code.o
OBJS     += src/hmac_reference_code.o
OBJS     += src/gmac_reference_code.o
OBJS     += src/token_object_reference_code.o
OBJS     += src/wrap_reference_code.o
OBJS     += src/pkcs11_error_handling.o
OBJS     += src/ecdsa_reference_code.o

$(TARGETS): $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $^ $(LDLIBS)

clobber clean :
	rm -rf $(OBJS) $(TARGETS)
