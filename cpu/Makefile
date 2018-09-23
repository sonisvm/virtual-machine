CC = gcc
CFLAGS = -Wall
TARGET = vcpu_scheduler
VFLAGS = -lvirt
all: $(TARGET)
$(TARGET): $(TARGET).c
	$(CC) $(CFLAGS) -o $(TARGET) $(TARGET).c $(VFLAGS)
clean:
	$(RM) $(TARGET)
