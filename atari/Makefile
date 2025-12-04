# Source Files
SOURCES = src/ai-sam.c

# Program Name
PROGRAM = ai-sam

# cc65 Target System
CC65_TARGET = atari
#CC65_HOME = /usr/share/cc65

export CC65_HOME = /usr/share/cc65

# FujiNet Library
FUJINET_LIB_VERSION = 4.8.1
FUJINET_LIB_DIR = ../fujinet-lib/$(CC65_TARGET)
FUJINET_LIB = $(FUJINET_LIB_DIR)/fujinet-atari-$(FUJINET_LIB_VERSION).lib
FUJINET_INCLUDES = -I$(FUJINET_LIB_DIR)

# Compiler & Linker Settings
#CC = strace -f -e openat $(CC65_HOME)/bin/cl65
CC = cl65
CFLAGS = -t $(CC65_TARGET) -O $(FUJINET_INCLUDES) #-I$(CC65_HOME)/share/cc65/include -I$(CC65_HOME)/share/cc65/asminc
LDFLAGS = -t $(CC65_TARGET) -m src/$(PROGRAM).map -L $(CC65_HOME)/lib
#LDFLAGS = -C src/atari-fixed.cfg -m src/$(PROGRAM).map -L $(CC65_HOME)/share/cc65/lib

.SUFFIXES:
.PHONY: all clean

# Default Build Target
all: $(PROGRAM).xex

# Compile C source files to object files
%.o: %.c
	$(CC) -c $(CFLAGS) -o $@ $<

# Link Everything into Final XEX Binary
$(PROGRAM).xex: $(SOURCES:.c=.o)
	$(CC) $(LDFLAGS) -o $@ $^ $(FUJINET_LIB)

# Clean up build files
clean:
	rm -f $(SOURCES:.c=.o) $(PROGRAM).xex src/$(PROGRAM).map
