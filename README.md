Be sure to edit the FNCONFIG.xml file to specify your options.

CAVEAT

The browse buttons have not yet been implemented. The only way to change the images being used is to exit the FLEXWire program, edit the FNCONFIG.xml file manually, then restart the program. This is still a work in progress.

There were two pico uf2 files placed in separate directories into the folder that you installed FLEXWire into. One is for the pico W and the other is for the pico 2W. Copy the appropriate file to you pico with the pico in boot load mode. Both of these emulators use the GPIO pins 4 and 5 for uart 1 output which is used in the emulator as the ACIA at $8004 (SWTBUG console).

Connect GIO4 (pin6 on the pico) to the RXD wire on the FTDI, GPIO5 (pin 7 on the pico) to the TXD wire on the FTDI and the GND (I use pin 8 on the pico) to GND on the FTDI. I use the FT232 adapter available from several sources. Connect the USB end of the FT232 to your PC and note the COM port it is assigned. Use this as the COM port on your terminal emulator. I use TV950.exe whic is included in the installer. You can use any other terminal emulator you like (TeraTerm, putty, etc). The baud rate should be set to 115200 with 8 data bits, no parity and 1 start and stop bits. The installer also includes sample diskette and SD Card images that get placed in the DefaultPath folder in the directory that the executable is installed to. I highly recommend that you either move or copy that folder to someplace that has write access by your user, This will also be the directory you specify in the FNCONFIG.xml file as the default directory.

When this installer is finished, run the FloppyMaintenance and TV950 installers that were copied to your FLEXWire execution folder.

When editing the FNCONFIG.xml file, you may have start your editor using the administrator mode.

I have added the ability to edit the configuration file to the application. You will need adminstrator rights to save the changes to the Program files directory. If you do not want to assign administrator privledges to this application, change the location of the config file in the application's GUI to a file in a directory you do have access to. When you do, this location will be saved to the registery (LOCAL_USER) and that file will be used to load the configuration when the application starts. By default the configuration file starts out in the applications execution directory.

NOTES on differences between the TCPIP and the RS232 connections:

  Since the TCPIP connection is NOT using FLEXNet (FNETDRV on the client end) to communicate, but rather is treating the server as a sector server, the client does not have a way to 'mount' images like the connection on the RS232 ports do. So all mounting is done on the server. When you select a drive to be mounted in the TCPIP connection, it is mounted for ALL clients that connect to it on port 6800.

    Also - for this same reason, the RS232 image mounts are NOT maintained when the program exits. Mounting of the images is per port and is completely controlled at the client end. Once the connection has been broken, all mounts need to be re-established. In the case of the RS232 connections, the mounts are per port.
