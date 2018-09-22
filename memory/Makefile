CC = gcc
CFLAGS = -g -Wall
TARGET = memory_coordinator
VFLAGS = -lvirt
all: $(TARGET)
$(TARGET): $(TARGET).c
	$(CC) $(CFLAGS) -o $(TARGET) $(TARGET).c $(VFLAGS)
clean:
	$(RM) $(TARGET)
