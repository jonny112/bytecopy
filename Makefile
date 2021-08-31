
CC = gcc
CC_OPTS = -Wall

BIN = bin
OUT = bytecopy
IN = src/bytecopy.c

all: $(BIN) $(BIN)/$(OUT)

$(BIN):
	mkdir $(BIN)

$(BIN)/$(OUT): $(IN)
	$(CC) $(CC_OPTS) -o $@ $^

.PHONY: clean

clean:
	rm -f $(BIN)/*
