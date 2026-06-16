CC = gcc
CFLAGS = -Wall -Wextra -O2 -g
TARGET = pipe_chat

all: $(TARGET)

$(TARGET): pipe_chat.c
	$(CC) $(CFLAGS) -o $(TARGET) pipe_chat.c

clean:
	rm -f $(TARGET) *.o

.PHONY: all clean
