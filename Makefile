CXX      ?= clang++
CXXFLAGS ?= -O3 -std=c++17 -Wall -Wextra

SRCS    := $(wildcard src/*.cpp)
BINS    := $(SRCS:src/%.cpp=build/%)
DATA    := $(SRCS:src/%.cpp=data/%.txt)
HEADERS := $(wildcard src/*.h)

.PHONY: all clean test install-graph-deps graphs

install-graph-deps:
	pip install -r scripts/requirements.txt

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

graphs:
	python3 scripts/avalanche.py
	python3 scripts/distribution-graph.py
