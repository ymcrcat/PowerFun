#!/system/bin/sh
insmod battstats.ko
DEVICE_NAME=iadc_dev
major=`grep $DEVICE_NAME /proc/devices | cut -f 1 -d " "`
mknod /dev/$DEVICE_NAME c $major 0
