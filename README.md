# ftrobopy_server
ftrobopy / ROBOPro / ftScratchTXT server for the fischertechnik TXT 4.0 controller

With the ftrobopy_server app the fischertechnik TXT 4.0 can be controlled in online mode with ftrobopy, ROBOPro or ftScratchTXT/ftscratch3.

Currently supported functions are:

- inputs I1-I8 (including fischertechnik sensors ultrasonic, color sensor, NTC resistor, trail follower and others)
- fast input counters C1-C4
- outputs O1-O8 / motors M1-M4 (including distance checking and synchronization)
- online camera streaming (ftrobopy and ROBOPro)

The program is written in C/C++ (std=c++17) and can be compiled with the preinstalled gcc-compiler on the TXT 4.0.

Warning:Please be aware that the code is still unstructured and very experimental.
It should not be used in any production environment.

Future plans:

- servo-output support (via O1-O3 of first Extension)
- I2C support
- BT-Remote control support (this needs some intrusion on the root level of the TXT 4.0 firmware)
- support to use (old) TXT controllers as Extensions for a (Master)-TXT 4.0 via WiFi network

Installation:

simply copy the provided binary file "ftrobopy_server" to a TXT 4.0 controller:

   scp ftrobopy_server ft@192.168.7.2:workspaces

ssh to the TXT with:

   ssh ft@192.168.7.2 (password is "fischertechnik")

first change the execute flag of the file with:

   chmod a+x ftrobopy_server

then start the program with:

   ./ftrobopy_server

It is also possible to activate (even autostart upon boot)  the ftrobopy_server from the TXT 4.0 GUI, so you don't need to login via ssh to start the server:

- login to the TXT 4.0 with ssh (see above)
- cd to the workspaces folder
- mkdir cpp
- cd cpp
- cp ../ftrobopy_server .
- touch ftrobopy_server.cpp
- chmod a+x ftrobopy_server
- chmod a+x ftrobopy_server.cpp

If a .cpp file exists, the corresponding binary (with the same name but without the .cpp extension) will be started upon starting the .cpp file in the TXT 4.0 GUI.

