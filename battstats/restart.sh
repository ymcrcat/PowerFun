#!/bin/sh
adb reboot bootloader
fastboot boot $HOME/android/system/out/target/product/hammerhead/boot.img
