CXX = g++
# Add sanitizer flags
ASAN_FLAGS = 
CXXFLAGS = -std=c++11 -g -O3 $(ASAN_FLAGS)
LDFLAGS = $(ASAN_FLAGS)

SRCS = $(wildcard src/*.cc)
OBJS = $(patsubst src/%.cc,obj/%.o,$(SRCS))

TARGET = garnet_standalone

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(LDFLAGS) -o $@ $^

obj/%.o: src/%.cc
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -rf obj $(TARGET)

# Add back debugging and compare latencies