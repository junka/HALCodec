# Copyright (c) 2015-2019, NVIDIA CORPORATION.  All rights reserved.
#
# NVIDIA CORPORATION and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA CORPORATION is strictly prohibited.


# Legacy makefile enabling building a Client App
#
#   Rebuild app:	make -f build_ca.makefile [all]
#   Remove files: 	make -f build_ca.makefile clean
#
# After the build, pls copy the client app to the non-secure OS to run it.
#

TOPDIR = .

TMPDIR = ./build-ca

include ../../../make/nvdefs.mk

BINDIR = $(TOPDIR)/bin

CFLAGS = -I$(TOPDIR)/include -I$(TOPDIR)/include/lib/ote/tee_fwk

CFLAGS +=	-Wimplicit -Wmissing-prototypes -Wpointer-sign -Wstrict-prototypes -march=armv8-a \
			-fno-exceptions -funwind-tables -Os -O2  -Wall -Werror -Wformat -Wchar-subscripts \
			-Wparentheses -Wtrigraphs -Wpointer-arith -Wmissing-declarations -Wredundant-decls \
			-Wmain -Wreturn-type -Wmultichar -Wunused -Wmissing-braces -Wstrict-aliasing \
			-Wsign-compare -Waddress  -Wno-unused-local-typedefs -DNV_IS_AVP=0 -DWIN_INTERFACE_CUSTOM \
			-fPIC -DPIC -UDEBUG -U_DEBUG -DNDEBUG -DNV_DEBUG=0 \
			-finline-functions -finline-limit=300 -fomit-frame-pointer  -fgcse-after-reload

LFLAGS = -Wl,--as-needed  -Wl,--dynamic-linker,/lib/ld-linux-aarch64.so.1

CA_LIBS =  	$(BINDIR)/libtlk_client_ote_command.o $(BINDIR)/libtlk_client_ote_command_tipc.o \
			$(BINDIR)/libtlk_client_ote_command_legacy.o $(BINDIR)/libtlk_common_ote_operation.o \
			$(BINDIR)/libtlk_common_ote_common_utils.o $(BINDIR)/libtrusty_trusty.o \
			$(BINDIR)/libserialization_flatten_ote.o $(BINDIR)/libserialization_context.o \
			$(BINDIR)/libtlk_client_tipc_comm.o $(BINDIR)/libtlk_client_ote_tos_type.o

CA_LIBS2 = -lgcov

CA_LIBS3 = $(BINDIR)/libgp_client_ote_wrapper_client.o $(BINDIR)/libgp_client_ote_wrapper_utils.o $(CA_LIBS)

SRCNAME1 = sample_client1
SRCNAME2 = sample_client1_gp

all: $(SRCNAME1) $(SRCNAME2)
	@echo "Client applications built successfully"

$(SRCNAME1): $(SRCNAME1).a $(SRCNAME1).o
	$(CC) $(LFLAGS) -o $(TMPDIR)/$(SRCNAME1) $(TMPDIR)/$(SRCNAME1).o $(TMPDIR)/$(SRCNAME1)_as_needed.a \
	-L $(BINDIR) $(CA_LIBS2)

$(SRCNAME1).a: $(TMPDIR)
	$(AR) rcs $(TMPDIR)/$(SRCNAME1)_as_needed.a $(CA_LIBS)

$(SRCNAME1).o: $(TMPDIR)
	$(CC) $(CFLAGS) -MMD -MF $(TMPDIR)/$(SRCNAME1).o.d -MP -o $(TMPDIR)/$(SRCNAME1).o -c src/$(SRCNAME1).c
	echo -e  "\nsrc/$(SRCNAME1).c:" >>$(TMPDIR)/$(SRCNAME1).o.d

$(TMPDIR):
	mkdir -p $(TMPDIR)

$(SRCNAME2): $(SRCNAME2).a $(SRCNAME2).o
	$(CC) $(LFLAGS) -o $(TMPDIR)/$(SRCNAME2) $(TMPDIR)/$(SRCNAME2).o $(TMPDIR)/$(SRCNAME2)_as_needed.a \
	-L $(BINDIR) $(CA_LIBS2)

$(SRCNAME2).a: $(TMPDIR)
	$(AR) rcs $(TMPDIR)/$(SRCNAME2)_as_needed.a $(CA_LIBS3)

$(SRCNAME2).o: $(TMPDIR)
	$(CC) $(CFLAGS) -MMD -MF $(TMPDIR)/$(SRCNAME2).o.d -MP -o $(TMPDIR)/$(SRCNAME2).o -c src/$(SRCNAME2).c
	echo -e  "\nsrc/$(SRCNAME2).c:" >>$(TMPDIR)/$(SRCNAME2).o.d

$(TMPDIR):
	mkdir -p $(TMPDIR)

clean:
	@echo "Cleaning built files and directories"
	rm -rf $(TMPDIR)
	rm -f *.bin *.elf *.sys *.o *.img *.a
