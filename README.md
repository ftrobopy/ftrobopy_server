# ftrobopy_server
ftrobopy / ROBOPro / ftScratchTXT server for the fischertechnik TXT 4.0 controller

With the ftrobopy_server app the fischertechnik TXT 4.0 can be controlled in online mode with ftrobopy, ROBOPro or ftScratchTXT/ftscratch3.

Currently supported functions are:

- inputs I1-I8 (including fischertechnik sensors ultrasonic, color sensor, NTC resistor, trail follower and others)
- fast input counters C1-C4
- outputs O1-O8 / motors M1-M4 (including distance checking and synchronization)
- online camera streaming (ftrobopy and ROBOPro)
- servo-output support (via O1-O3 of first Extension, initialize ftrobopy with extension support "use_extension=True" to get this feature)
- extended synchronization support for 4 motors in ROBOPro and ftrobopy

The program is written in C/C++ (std=c++17) and can be compiled with the preinstalled gcc-compiler on the TXT 4.0.

Future plans:
- I2C support
- BT-Remote control support (this needs some intrusion on the root level of the TXT 4.0 firmware)
- support to use (old) TXT controllers as Extensions for a (Master)-TXT 4.0 via WiFi network

Installation:

simply copy the provided binary file "ftrobopy_server" to a TXT 4.0 controller:

   scp ftrobopy_server ft@192.168.7.2:workspaces

ssh to the TXT with:

   ssh ft@192.168.7.2 (password is "fischertechnik")

set the execute flag of the file:

   chmod a+x ftrobopy_server

and start the program with:

   ./ftrobopy_server

It is also possible to activate (even autostart upon boot)  the ftrobopy_server from the TXT 4.0 GUI, so you don't need to login via ssh to start the server:

- login to the TXT 4.0 with ssh (see above)
- cd to the workspaces folder
- mkdir cpp
- cd cpp
- cp ../ftrobopy_server .
- chmod a+x ftrobopy_server
- touch ftrobopy_server.cpp

If a .cpp file exists, the corresponding binary (with the same name but without the .cpp extension) will be started upon starting the .cpp file in the TXT 4.0 GUI.

Servo support in ROBOPro and ftrobopy:

- use outputs O1-O3 of EM1 (first extension) in ROBOPro to use Servos S1-S3 of the TXT 4.0
- 1 ... 8 (or 0-512) control the position of the servo (4 or 256 corresponds to the middle position)
(in order to use this in ftrobopy, initialize ftrobopy with use_extension=True)

Synchronisation support for all 4 motors:

- use output O4 of EM1 to switch between synchronization of all 4 motors On (8 or 512) and Off (1 or 0) 
(in order to use this in ftrobopy, initialize ftrobopy with use_extension=True)
 