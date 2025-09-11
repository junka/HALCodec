TARGETS = libnvmnand_public.so

CFLAGS   = $(NV_PLATFORM_OPT) $(NV_PLATFORM_CFLAGS)
CPPFLAGS = $(NV_PLATFORM_SDK_INC) $(NV_PLATFORM_CPPFLAGS)
LDFLAGS  = $(NV_PLATFORM_SDK_LIB) $(NV_PLATFORM_LDFLAGS)

OBJS   =  nvmnand_ops.o

include ../../../make/nvdefs.mk

LDLIBS = -lnvmnand_private

$(TARGETS): $(OBJS)
	$(CC) ${LDFLAGS} -shared -o $@ $^

clean clobber:
	rm -rf $(OBJS) $(TARGETS)
