# NYANLIN Binary AI v3.0 - Pure C GGUF Inference Engine
# No llama.cpp dependency - runs GGUF models directly

CC = gcc
CFLAGS = -O3 -Wall
LDFLAGS = -lm

TARGET = nyanlin
SRC = nyanlin.c

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LDFLAGS)

clean:
	rm -f $(TARGET)

run: $(TARGET)
	./$(TARGET)

# Cross-compile for Android ARM64 (from Linux host)
android-arm64:
	aarch64-linux-android-gcc $(CFLAGS) -o $(TARGET) $(SRC) $(LDFLAGS)

# Debug build
debug:
	$(CC) -g -O0 -Wall -o $(TARGET)_debug $(SRC) $(LDFLAGS)
