# espminitool
A tool that uploads code to an ESP32 microcontroller by talking to its bootloader over a serial port. Written in C, and should be compatible with any operating system that supports termios&mdash;so Linux, macOS, and WSL version 1.

Currently, it uses a hardcoded partition table to know what files to upload at what offsets. You can change the partition table in [main.c, lines 112 to 116](https://github.com/thatoddmailbox/espminitool/blob/master/main.c#L112-L116).