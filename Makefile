CC      = gcc

GIT_DESCRIBE := $(shell git describe --tags --long --always 2>/dev/null)
GIT_VERSION  := $(shell echo "$(GIT_DESCRIBE)" | sed 's/-g[0-9a-f]*$$//')
ifeq ($(strip $(GIT_VERSION)),)
GIT_VERSION  := dev
endif

CFLAGS  = -Wall -Wextra -O2 -std=c17 -D_POSIX_C_SOURCE=200809L \
          -DGIT_VERSION='"$(GIT_VERSION)"' \
          -Isrc $(shell pkg-config --cflags openssl 2>/dev/null)
LDFLAGS = $(shell pkg-config --libs openssl 2>/dev/null || echo "-lssl -lcrypto") \
          -lpthread

SRCS = src/main.c \
       src/server.c \
       src/conn.c \
       src/protocol.c \
       src/token.c \
       src/ws.c \
       src/random.c

OBJS = $(SRCS:src/%.c=build/%.o)
BIN  = build/rmbtd

all: $(BIN)

$(BIN): $(OBJS) | build
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

build/%.o: src/%.c | build
	$(CC) $(CFLAGS) -c -o $@ $<

build:
	mkdir -p build

random:
	dd if=/dev/urandom of=random bs=1M count=100

clean:
	rm -rf build

install: $(BIN)
	install -m 755 $(BIN) /usr/local/bin/rmbtd

.PHONY: all clean install random
