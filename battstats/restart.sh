#!/bin/sh
adb reboot bootloader
fastboot boot ../system/out/target/product/hammerhead/boot.img
