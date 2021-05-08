CC = gcc
CFLAGS  = -Wall -lm 

TARGET = oss
TARGET1 = child

all: $(TARGET) $(TARGET1)

$(TARGET): $(TARGET).c
	$(CC) $(CFLAGS) -o $(TARGET) $(TARGET).c
$(TARGET1): $(TARGET1).c
	$(CC) $(CFLAGS) -o $(TARGET1) $(TARGET1).c
clean:
	$(RM) $(TARGET) $(TARGET1)
