This kernel module implements a char-device driver. 
The registered char device enables reading current measurements using the Qualcomm current ADC embedded in mobile devices. <br>
The module can be installed using <br>
\# insmod battstats.ko <br>
and a char device node under /dev can be created by running <br>
\# install_adc.sh
