SRC := src
BUILD := build

CC ?= gcc
CFLAGS := -std=gnu99 -Wall -Wextra -pedantic
LDFLAGS := -fsanitize=undefined,address
LIBS := -lm

SRCS := $(wildcard $(SRC)/*.c)
OBJS := $(patsubst $(SRC)/%.c, $(BUILD)/%.o, $(SRCS))
TARGET := main

.PHONY: all run debug clean

all: CFLAGS+=-fsanitize=undefined,address -O2
all: $(TARGET)

run: $(TARGET)
	./$(TARGET)

debug: CFLAGS+=-g -Og
debug: $(TARGET)

check: $(TARGET)
	valgrind -s ./$<

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(LIBS) $^ -o $@

$(BUILD)/%.o: $(SRC)/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $^ -o $@

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD) $(TARGET)

