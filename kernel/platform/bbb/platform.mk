# kernel/platform/bbb/platform.mk
# Chip drivers used by the BeagleBone Black (AM335x) board.

PLATFORM_DRIVERS := \
    kernel/drivers/uart/ns16550.c \
    kernel/drivers/timer/dmtimer.c \
    kernel/drivers/intc/am335x_intc.c
