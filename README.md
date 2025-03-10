Be sure to edit the FNCONFIG.xml file to specify your options.

=======================
CAVEAT

The browse buttons have not yet been implemented. The only way to change the images being used is to exit the FLEXWire program, edit the FNCONFIG.xml file manually, then restart the program. This is still a work in progress.
=======================

There were two pico uf2 files placed in separate directories into the folder that you installed FLEXWire into. One is for the pico W and the other is for the pico 2W. Copy the appropriate file to you pico with the pico in boot load mode. Both of these emulators use the GPIO pins 4 and 5 for uart 1 output which is used in the emulator as the ACIA at $8004 (SWTBUG console).

Connect GIO4 (pin6 on the pico) to the RXD wire on the FTDI, GPIO5 (pin 7 on the pico) to the TXD wire on the FTDI and the GND (I use pin 8 on the pico) to GND on the FTDI. I use the FT232 adapter available from several sources. Connect the USB end of the FT232 to your PC and note the COM port it is assigned. Use this as the COM port on your terminal emulator. I use TV950.exe whic is included in the installer. You can use any other terminal emulator you like (TeraTerm, putty, etc). The baud rate should be set to 115200 with 8 data bits, no parity and 1 start and stop bits. The installer also includes sample diskette and SD Card images that get placed in the DefaultPath folder in the directory that the executable is installed to. I highly recommend that you either move or copy that folder to someplace that has write access by your user, This will also be the directory you specify in the FNCONFIG.xml file as the default directory.

When this installer is finished, run the FloppyMaintenance and TV950 installers that were copied to your FLEXWire execution folder.

When editing the FNCONFIG.xml file, you may have start your editor using the administrator mode.
