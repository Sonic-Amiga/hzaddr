# Herzborg address programming utility

This program is a little utility to identify and/or change bus addresses for SmartHome
hardware by Herzborg (http://www.herzborg.com/). It was developed and tested using DT300TV-1.2/14
curtain motor.

# Usage

1. Connect your motor to AC power and connect interface cable to your computer's serial port
2. Run the program, for example "hzaddr COM5 0x0101". This particular command line would set address to 0x0101.
3. Press and hold the ID/program button (on the motor it's located on the back under the cover, next to connectors). Release the button after approximately 5 seconds.
4. The device will identify itself and the program will set the new address.

A typical session looks like this:

D:\Projects\hzaddr\hzaddr\x64\Debug>hzaddr.exe COM5 0x0101
Waiting for IDENT packet on COM5...
Found device address 0xFEFE (65278)
Setting new address 0f 0x0101 (257)...
Packet sent, waiting for confirmation...
All done, response is correct

Note that addresses with the first byte of 0 seem illegal, my device didn't accept them.
