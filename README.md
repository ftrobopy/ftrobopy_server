# ftrobopy_server
ftrobopy / ROBOPro server for the fischertechnik TXT 4.0 controller

With the ftrobopy_server app the fischertechnik TXT 4.0 can be controlled in online mode either with ftrobopy or with ROBOPro.

Currently supported functions are:

- inputs I1-I8
- fast input counters C1-C4
- outputs O1-O8 / motors M1-M4 (including distance checking and synchronization)
- online camera streaming (ftrobopy and ROBOPro)

The program is written in C/C++ (std=c++17) and can be compiled with the preinstalled gcc-compiler on the TXT 4.0.

Warning:Please be aware that the code is still unstructured and very experimental.
It should not be used in any production environment.

Future plans:

- servo-output support
- I2C support
- BT-Remote control support
- support to use (old) TXT controllers as Extensions for a (Master)-TXT 4.0 via WiFi network

Installation:

simply copy the provided binary file "ftrobopy_server" to a TXT 4.0 controller:

   scp ftrobopy_server ft@192.168.7.2:workspaces

ssh to the TXT with:

   ssh ft@192.168.7.2

and start the program with:

   ./ftrobopy_server


