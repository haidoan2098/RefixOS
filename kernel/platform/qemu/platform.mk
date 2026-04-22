# kernel/platform/qemu/platform.mk
# Chip drivers used by the QEMU realview-pb-a8 board.

PLATFORM_DRIVERS := \
    kernel/drivers/uart/pl011.c \
    kernel/drivers/timer/sp804.c \
    kernel/drivers/intc/gicv1.c
