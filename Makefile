CC      = gcc
CFLAGS  = -std=c99 -Wall -Wextra -O2
LDFLAGS = -lraylib -lm -lpthread -ldl

# Use pkg-config when available, otherwise rely on system/RAYLIB_PATH
RAYLIB_PATH ?=
ifneq ($(RAYLIB_PATH),)
  CFLAGS  += -I$(RAYLIB_PATH)/include
  LDFLAGS  = -L$(RAYLIB_PATH)/lib -lraylib -lm -lpthread -ldl
else
  PKG := $(shell pkg-config --cflags raylib 2>/dev/null)
  ifneq ($(PKG),)
    CFLAGS  += $(shell pkg-config --cflags raylib)
    LDFLAGS  = $(shell pkg-config --libs   raylib) -lm -lpthread -ldl
  endif
endif

TARGET = pxed

all: $(TARGET)

$(TARGET): pxed.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(TARGET)

.PHONY: all clean
