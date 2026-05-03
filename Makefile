CXX := clang++
CXXFLAGS := -std=c++20 -Wall -Wextra -Wpedantic -Werror -O2 -g
CPPFLAGS := -Iinclude -Ithird_party

FORMAT := clang-format

BUILD_DIR := build
OBJ_DIR := $(BUILD_DIR)/obj
BIN_DIR := $(BUILD_DIR)/bin

LIB_NAME := libworkflow_parser.a
TEST_BIN := $(BIN_DIR)/workflow_parser_tests

SRC := \
	src/json.cpp \
	src/workflow_parser.cpp

OBJ := $(patsubst src/%.cpp,$(OBJ_DIR)/src/%.o,$(SRC))

TEST_SRC := tests/workflow_parser_tests.cpp
TEST_OBJ := $(patsubst tests/%.cpp,$(OBJ_DIR)/tests/%.o,$(TEST_SRC))

CATCH_SRC := third_party/catch2/catch_amalgamated.cpp
CATCH_OBJ := $(OBJ_DIR)/third_party/catch2/catch_amalgamated.o

LIB := $(BUILD_DIR)/$(LIB_NAME)

FORMAT_FILES := \
	include/wf/json.hpp \
	include/wf/workflow_definition.hpp \
	include/wf/workflow_parser.hpp \
	src/json.cpp \
	src/workflow_parser.cpp \
	tests/workflow_parser_tests.cpp

.PHONY: all test format format-check clean help

all: test

$(LIB): $(OBJ)
	@mkdir -p $(dir $@)
	ar rcs $@ $^

$(TEST_BIN): $(LIB) $(TEST_OBJ) $(CATCH_OBJ)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -o $@ $(TEST_OBJ) $(CATCH_OBJ) $(LIB)

$(OBJ_DIR)/src/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@

$(OBJ_DIR)/tests/%.o: tests/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@

$(OBJ_DIR)/third_party/catch2/%.o: third_party/catch2/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@

test: $(TEST_BIN)
	$(TEST_BIN)

format:
	$(FORMAT) -i $(FORMAT_FILES)

format-check:
	$(FORMAT) --dry-run --Werror $(FORMAT_FILES)

clean:
	rm -rf $(BUILD_DIR)

help:
	@echo "Targets:"
	@echo "  make              Build and run tests"
	@echo "  make test         Build and run tests"
	@echo "  make format       Format source files with clang-format"
	@echo "  make format-check Check formatting without modifying files"
	@echo "  make clean        Remove build outputs"
	@echo ""
	@echo "Variables:"
	@echo "  CXX=$(CXX)"
	@echo "  CXXFLAGS=$(CXXFLAGS)"
	@echo "  CPPFLAGS=$(CPPFLAGS)"
