CXX      ?= clang++
CXXFLAGS ?= -O2 -std=c++17 -Wall -Wextra

SRCS    := $(wildcard src/*.cpp)
BINS    := $(SRCS:src/%.cpp=build/%)
DATA    := $(SRCS:src/%.cpp=data/%.txt)
HEADERS := $(wildcard src/*.h)

.PHONY: all clean test

all: $(BINS)

build/%: src/%.cpp $(HEADERS) | build
	$(CXX) $(CXXFLAGS) -o $@ $<

build data:
	mkdir -p $@

data/%.txt: build/% | data
	$< > $@

test: $(DATA)

clean:
	rm -rf build data
