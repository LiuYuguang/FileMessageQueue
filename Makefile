CFLAGS := -Wall -O
INC := -I./include

SRC := $(wildcard src/*.c)
OBJ := $(patsubst src/%.c, obj/%.o, $(SRC))

CC     := gcc

TARGET := file_que_inotify_bench file_que_sem_bench file_que_fifo_bench 

all: $(TARGET)

file_que_inotify_bench: file_que_inotify_bench.o filemq.o
	$(CC) -o $@ $^

file_que_sem_bench: file_que_sem_bench.o filemq.o
	$(CC) -o $@ $^

file_que_fifo_bench: file_que_fifo_bench.o filemq.o
	$(CC) -o $@ $^

$(OBJ): obj/%.o : src/%.c obj
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
