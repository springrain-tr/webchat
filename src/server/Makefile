CXX      ?= g++
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra
LDFLAGS  ?=
LDLIBS   ?=

SRC := server.cpp
BIN ?= ../../build/bin/server

# MSVC 靠源文件里的 #pragma comment(lib, "ws2_32.lib") 自动链接 Winsock，
# GCC / MinGW 会忽略这条 pragma，需要显式加 -lws2_32。
ifeq ($(OS),Windows_NT)
LDLIBS += -lws2_32
endif

all: $(BIN)

$(BIN): $(SRC)
	@mkdir -p $(dir $(BIN))
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS) $(LDLIBS)

run: $(BIN)
	./$(BIN)

clean:
	rm -f $(BIN) $(BIN).exe

.PHONY: all run clean
