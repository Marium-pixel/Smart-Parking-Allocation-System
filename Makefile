CC = gcc
CFLAGS = -Wall -pthread
GUI_FLAGS = -lraylib -lGL -lm -lpthread -ldl -lrt -lX11

TARGET = smart_parking

SRCS = src/main.c src/vehicle.c src/semaphore_logic.c src/globals.c

$(TARGET):
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS) $(GUI_FLAGS)

clean:
	rm -f $(TARGET)
