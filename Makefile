CC=gcc 
CFLAGS=-Wall

HEADERS = commands.c debug.c protocol.c reader.c main.c
OBJECTS = commands.o debug.o protocol.o reader.o main.o

default: espminitool

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

espminitool: $(OBJECTS)
	$(CC) $(OBJECTS) -o $@

clean:
	-rm -f $(OBJECTS)
	-rm -f espminitool