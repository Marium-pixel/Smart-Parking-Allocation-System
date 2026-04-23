CC = gcc
CFLAGS = -Wall -pthread
TARGET = smart_parking
SRCS = src/main.c src/vehicle.c src/semaphore_logic.c

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS)

clean:
	rm -f $(TARGET)
