Current logging application
===========================

#### Overview
A native application that reads current measurements using sysfs and logs them to a file.


#### Usage
Compile using the NDK tool "ndk-build" in the directory, you can easily create a APK by importing the project into Eclipse and selecting Run As > Android application.
Another option to compile the APK is to run
$ ant debug
which can then be installed by running
$ ant installd
