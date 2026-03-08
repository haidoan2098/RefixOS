# boot/Makefile — Boot build dispatcher
#
# Delegates to subdirectory Makefiles based on PLATFORM:
#   PLATFORM=qemu → boot/kernel/ only
#   PLATFORM=bbb  → boot/spl/ + boot/kernel/

.PHONY: all clean

ifeq ($(PLATFORM), bbb)
all:
	$(MAKE) -C spl
	$(MAKE) -C kernel
clean:
	$(MAKE) -C spl clean
	$(MAKE) -C kernel clean
else
# Default: qemu
all:
	$(MAKE) -C kernel
clean:
	$(MAKE) -C kernel clean
endif
