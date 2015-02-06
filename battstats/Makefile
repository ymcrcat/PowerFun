# KERNEL_DIR=$(HOME)/android/system/kernel/google/msm
KERNEL_DIR=$(HOME)/android/system/out/target/product/hammerhead/obj/KERNEL_OBJ

VERSION = 3
PATCHLEVEL = 4
SUBLEVEL = 0
EXTRAVERSION = -cyanogenmod-g7ef4f6a

obj-m := battstats.o
# obj-m := qcom-spmi-iadc.o
PWD := $(shell pwd)

default:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) modules

clean:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) clean
