CFLAGS := -Wall -O
INC := -I./include

SRC := $(wildcard src/*.c)
OBJ := $(patsubst src/%.c, obj/%.o, $(SRC))

CC     := gcc

TARGET := client server

all: obj $(TARGET)

client: client.o filemq.o
	$(CC) -o $@ $^

server: server.o filemq.o
	$(CC) -o $@ $^

$(OBJ): obj/%.o : src/%.c
	$(CC) -c $(CFLAGS) -o $@ $< $(INC)

obj:
	@mkdir -p $@

clean:
	-rm $(OBJ) $(TARGET)
	@rmdir obj

.PHONY: all clean 

vpath %.c  src
vpath %.o  obj
vpath %.h  include
