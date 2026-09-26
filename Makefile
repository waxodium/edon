CXX       := g++
CXXFLAGS  := -std=c++20 -Wall -Wextra -O2
TARGET    := build/edon
SRC_DIR   := src
BUILD_DIR := build

SRCS     := $(wildcard $(SRC_DIR)/*.cpp)
GIT_HASH := $(shell git rev-parse --short HEAD 2>/dev/null || echo "unknown")
CXXFLAGS += -DEDON_COMMIT_HASH="\"$(GIT_HASH)\""

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(SRCS)
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(SRCS) -o $@
	@echo "Build complete: ./$(TARGET) ($(GIT_HASH))"

run: $(TARGET)
	./$(TARGET)

clean:
	rm -rf $(BUILD_DIR)
