# espminitool
A tool that uploads code to an ESP32 microcontroller by talking to its bootloader over a serial port. Written in C, and should be compatible with any operating system that supports termios&mdash;so Linux, macOS, and WSL version 1.

Currently, it uses a hardcoded partition table to know what files to upload at what offsets. You can change the partition table in [main.c, lines 112 to 116](https://github.com/thatoddmailbox/espminitool/blob/master/main.c#L112-L116).

## Building
You should just be able to run `make` to build `espminitool`. You will need `gcc` installed to compile the program.

## Usage
You'll want to make sure that the partition table is set up correctly (see the note above about where it's set in main.c). Once you've done that, make sure that the appropriate .bin files are located in the current working directory, with names matching what you set in `main.c`. Then, you can just do `./espminitool <serial port>`, where `<serial port>` is the path to the serial port of your ESP32.