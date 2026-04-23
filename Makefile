CC      = gcc
CFLAGS  = -std=c99 -Wall -Wextra -O2
LDFLAGS = -lraylib -lm -lpthread -ldl
UNAME_S := $(shell uname -s)

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
  else ifneq ($(wildcard /usr/local/lib/libraylib.*),)
    CFLAGS  += -I/usr/local/include
    LDFLAGS  = -L/usr/local/lib -lraylib
    ifeq ($(UNAME_S),Linux)
      LDFLAGS += -lGL -lm -lpthread -ldl -lrt -lX11
    else
      LDFLAGS += -lm -lpthread -ldl
    endif
  endif
endif

TARGET = pxed

all: $(TARGET)

$(TARGET): pxed.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(TARGET)

.PHONY: all clean
