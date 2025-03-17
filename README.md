Be sure to edit the FNCONFIG.xml file to specify your options.

![image](https://github.com/user-attachments/assets/b8207502-3caa-4209-9c6f-a040c5d9ce60)

CAVEATA

The TCP IP connections allow for a second SD Card adapter to be attached to the emulated IDE interface. This is only possible on a real system if you have a second SD Card adapter attached to the SD Card addapter cable from the system's IDE port. I do not beleive this is yet possible with the current IDE card designs available for the SS30 bus.

The browse buttons have now been implemented for the TCP IP connected devices. You will notice that there are no browse buttons for the RS232 port drives. This is because the images for these drives MUST be mounted from the connecting client with the FLEXNet FMOUNT command. The text boxes are there just to show you which images have been mounted. When the configuration file is saved, these mounts are saved with it as a reminder the next time the program is loaded, which drives WERE mounted. After restarting the program, they must be remounted by the client to be valid. You may have to use the resync program on the client before you remount the images.

There were two pico uf2 files placed in separate directories into the folder that you installed FLEXWire into. One is for the pico W and the other is for the pico 2W. Copy the appropriate file to you pico with the pico in boot load mode. Both of these emulators use the GPIO pins 4 and 5 for uart 1 output which is used in the emulator as the ACIA at $8004 (SWTBUG console).

Connect GIO4 (pin6 on the pico) to the RXD wire on the FTDI, GPIO5 (pin 7 on the pico) to the TXD wire on the FTDI and the GND (I use pin 8 on the pico) to GND on the FTDI. I use the FT232 adapter available from several sources. Connect the USB end of the FT232 to your PC and note the COM port it is assigned. Use this as the COM port on your terminal emulator. I use TV950.exe whic is included in the installer. You can use any other terminal emulator you like (TeraTerm, putty, etc). The baud rate should be set to 115200 with 8 data bits, no parity and 1 start and stop bits. The installer also includes sample diskette and SD Card images that get placed in the DefaultPath folder in the directory that the executable is installed to. I highly recommend that you either move or copy that folder to someplace that has write access by your user, This will also be the directory you specify in the FNCONFIG.xml file as the default directory.

When this installer is finished, run the FloppyMaintenance and TV950 installers that were copied to your FLEXWire execution folder.

When editing the FNCONFIG.xml file, you may have start your editor using the administrator mode.

I have added the ability to edit the configuration file to the application. You will need adminstrator rights to save the changes to the Program files directory. If you do not want to assign administrator privledges to this application, change the location of the config file in the application's GUI to a file in a directory you do have access to. When you do, this location will be saved to the registery (LOCAL_USER) and that file will be used to load the configuration when the application starts. By default the configuration file starts out in the applications execution directory.

NOTES on differences between the TCPIP and the RS232 connections:

  Since the TCPIP connection is NOT using FLEXNet (FNETDRV on the client end) to communicate, but rather is treating the server as a sector server, the client does not have a way to 'mount' images like the connection on the RS232 ports do. So all mounting is done on the server. When you select a drive to be mounted in the TCPIP connection, it is mounted for ALL clients that connect to it on port 6800.

  Also - for this same reason, the RS232 image mounts are NOT maintained when the program exits. Mounting of the images is per port and is completely controlled at the client end. Once the connection has been broken, all mounts need to be re-established. In the case of the RS232 connections, the mounts are per port.
    
![image](https://github.com/user-attachments/assets/89c73263-9ad1-4f72-a153-557eb9a1da0b)

  In the image below, the top TV950 instance is connected to the pico 2W running emu6800 using the TCP IP connection to FLEXWire and the bottom TV950 instance is connected to my Peripheral Technology PT69-5A connected to FLEXWire through an RS232 port using the COM8 connection.
  
![image](https://github.com/user-attachments/assets/64746afb-cc5d-4159-94b9-2559b154b31b)
