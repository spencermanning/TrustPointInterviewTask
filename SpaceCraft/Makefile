
CC = gcc
CFLAGS = -Wall -Wextra -O0 -pthread
LDFLAGS = -lz # zlib for crc32
# LDFLAGS += -lrt # for queues

TARGET = spacecraft
SRCS = spacecraft.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(TARGET) $(OBJS)

.PHONY: all clean