CXX := g++

SRC_DIR := src
FFI_DIR := $(SRC_DIR)/ffi
FFI_INCLUDE_DIR := $(FFI_DIR)/includes
BUILD_DIR := build
TARGET := $(BUILD_DIR)/edon

CXXFLAGS := \
	-std=c++20 \
	-Wall \
	-Wextra \
	-Werror \
	-O2 \
	-I$(SRC_DIR) \
	-I$(FFI_INCLUDE_DIR) \
	$(shell pkg-config --cflags javascriptcoregtk-4.1)

LDFLAGS := \
	$(shell pkg-config --libs javascriptcoregtk-4.1) \
	-ltcc \
	-lffi \
	-ldl \
	-lm

SRC_SRCS := $(wildcard $(SRC_DIR)/*.cpp)
FFI_SRCS := $(wildcard $(FFI_DIR)/*.cpp)
SRCS := $(SRC_SRCS) $(FFI_SRCS)

GIT_HASH := $(shell git rev-parse --short HEAD 2>/dev/null || echo "unknown")

CXXFLAGS += \
	-DEDON_COMMIT_HASH=\"$(GIT_HASH)\"

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(SRCS)
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(SRCS) -o $@ $(LDFLAGS)
	@echo "Build complete: ./$(TARGET) ($(GIT_HASH))"

run: $(TARGET)
	./$(TARGET)

clean:
	rm -rf $(BUILD_DIR)
