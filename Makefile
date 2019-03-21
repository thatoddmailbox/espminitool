CC=gcc 
CFLAGS=-Wall

HEADERS = main.c
OBJECTS = main.o

default: espminitool

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

espminitool: $(OBJECTS)
	$(CC) $(OBJECTS) -o $@

clean:
	-rm -f $(OBJECTS)
	-rm -f espminitool