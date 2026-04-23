CC = gcc
CFLAGS = -Wall -g -Isrc
LIBS = -lpthread -lm

SRC = src/main.c src/vehicle.c src/semaphore_logic.c
OUT = smart_parking

all:
	$(CC) $(CFLAGS) $(SRC) -o $(OUT) $(LIBS)

clean:
	rm -f $(OUT)
