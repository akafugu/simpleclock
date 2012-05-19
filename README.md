
Simpleclock
-----------

Firmware for Simpleclock

Simpleclock is a easy to assemble attractive 4-digit 7-segment LED display clock with temperature and alarm function. It is available in three colors: Red, Blue and White.

See the [product page](http://www.akafugu.jp/posts/products/simpleclock/) for more information.

Programming
----------

To reprogram the device, remove the display board and insert an FTDI adapter
into the header right under the microcontroller (marked GNC, CTS, VCC, TXO, RXI, DTR).

The microcontroller comes with a built-in bootloader for 8MHz internal clock.

If you are using the Akafugu Arduino package, you can select "Akafugu Breadboard Adapter (internal 8MHz board)",
otherwise, open your hardware/arduino/boards.txt file (found inside the directory where you installed
Arduino, or on OS X by selecting "Show Package Contents".

Add the following to the end of boards.txt

##############################################################

simpleclock.name=Akafugu Simpleclock (Internal 8MHz Clock)

simpleclock.upload.protocol=arduino
simpleclock.upload.maximum_size=30720
simpleclock.upload.speed=57600

simpleclock.bootloader.low_fuses=0xe2
simpleclock.bootloader.high_fuses=0xD8
simpleclock.bootloader.extended_fuses=0x05
simpleclock.bootloader.path=atmega
simpleclock.bootloader.file=ATmegaBOOT_168_atmega328_pro_8MHz.hex
simpleclock.bootloader.unlock_bits=0x3F
simpleclock.bootloader.lock_bits=0x0F

simpleclock.build.mcu=atmega328p
simpleclock.build.f_cpu=8000000L
simpleclock.build.core=arduino
simpleclock.build.variant=standard

Now select "Akafugu Simpleclock (Internal 8MHz Clock) in the Boards menu.
