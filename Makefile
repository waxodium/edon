CXX := g++

SRC_DIR := src
CORE_DIR := $(SRC_DIR)/core
CLI_DIR := $(SRC_DIR)/cli
FFI_DIR := $(SRC_DIR)/ffi
PARSER_DIR := $(FFI_DIR)/parser
BUILD_DIR := build
TARGET := $(BUILD_DIR)/edon

CXXFLAGS := \
	-std=c++20 \
	-Wall \
	-Wextra \
	-Werror \
	-O2 \
	-I$(SRC_DIR) \
	-Iincludes \
	-I$(CORE_DIR) \
	-I$(CLI_DIR) \
	-I$(PARSER_DIR) \
	-I$(BUILD_DIR) \
	$(shell pkg-config --cflags javascriptcoregtk-4.1)

LDFLAGS := \
	$(shell pkg-config --libs javascriptcoregtk-4.1) \
	-ltcc \
	-lffi \
	-ldl \
	-lm

CORE_SRCS := $(wildcard $(CORE_DIR)/*.cpp)
CLI_SRCS := $(wildcard $(CLI_DIR)/*.cpp)
FFI_SRCS := \
	$(wildcard $(FFI_DIR)/*.cpp) \
	$(wildcard $(PARSER_DIR)/*.cpp)

SRCS := $(CORE_SRCS) $(CLI_SRCS) $(FFI_SRCS)

GIT_HASH := $(shell git rev-parse --short HEAD 2>/dev/null || echo "unknown")

CXXFLAGS += \
	-DEDON_COMMIT_HASH=\"$(GIT_HASH)\"

CORE_HEADER := $(BUILD_DIR)/embedjs.hpp

.PHONY: all clean run
.ONESHELL:

all: $(TARGET)

$(CORE_HEADER): $(CORE_DIR)/util.js $(CORE_DIR)/console.js $(CORE_DIR)/embed_js.py
	python3 $(CORE_DIR)/embed_js.py $@ $(CORE_DIR)/util.js $(CORE_DIR)/console.js

$(TARGET): $(SRCS) $(CORE_HEADER)
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(SRCS) -o $@ $(LDFLAGS)
	@echo "Build complete: ./$(TARGET) ($(GIT_HASH))"

run: $(TARGET)
	./$(TARGET)

clean:
	rm -rf $(BUILD_DIR)

