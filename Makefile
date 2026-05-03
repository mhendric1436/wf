CXX := clang++
CXXFLAGS := -std=c++20 -Wall -Wextra -Wpedantic -Werror -O2 -g
CPPFLAGS := -Iinclude -Ithird_party

FORMAT := clang-format
PLANTUML ?= plantuml

BUILD_DIR := build
OBJ_DIR := $(BUILD_DIR)/obj
BIN_DIR := $(BUILD_DIR)/bin

DOCS_DIR := docs
PUML_FILES := $(wildcard $(DOCS_DIR)/*.puml)
PNG_FILES := $(PUML_FILES:.puml=.png)

LIB_NAME := libwf.a
LIB := $(BUILD_DIR)/$(LIB_NAME)

TEST_BIN := $(BIN_DIR)/wf_tests

SRC := \
	src/json.cpp \
	src/workflow_parser.cpp \
	src/workflow_definition_store.cpp \
	src/workflow_execution_store.cpp \
	src/workflow_orchestrator.cpp \
	src/workflow_service.cpp

OBJ := $(patsubst src/%.cpp,$(OBJ_DIR)/src/%.o,$(SRC))

TEST_SRC := \
	tests/workflow_parser_tests.cpp

TEST_OBJ := $(patsubst tests/%.cpp,$(OBJ_DIR)/tests/%.o,$(TEST_SRC))

CATCH_SRC := third_party/catch2/catch_amalgamated.cpp
CATCH_OBJ := $(OBJ_DIR)/third_party/catch2/catch_amalgamated.o

FORMAT_FILES := \
	include/wf/json.hpp \
	include/wf/workflow_definition.hpp \
	include/wf/workflow_parser.hpp \
	include/wf/workflow_definition_store.hpp \
	include/wf/workflow_execution.hpp \
	include/wf/workflow_execution_store.hpp \
	include/wf/workflow_logic.hpp \
	include/wf/workflow_orchestrator.hpp \
	include/wf/workflow_service.hpp \
	src/json.cpp \
	src/workflow_parser.cpp \
	src/workflow_definition_store.cpp \
	src/workflow_execution_store.cpp \
	src/workflow_orchestrator.cpp \
	src/workflow_service.cpp \
	tests/workflow_parser_tests.cpp

.PHONY: all build test format format-check docs-png clean help

all: test

build: $(LIB)

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

docs-png: $(PNG_FILES)

$(DOCS_DIR)/%.png: $(DOCS_DIR)/%.puml
	$(PLANTUML) -tpng $<

clean:
	rm -rf $(BUILD_DIR)
	rm -f $(DOCS_DIR)/*.png

help:
	@echo "Targets:"
	@echo "  make              Build and run tests"
	@echo "  make build        Build static library only"
	@echo "  make test         Build and run tests"
	@echo "  make format       Format source and header files with clang-format"
	@echo "  make format-check Check formatting without modifying files"
	@echo "  make docs-png     Generate PNG diagrams from docs/*.puml"
	@echo "  make clean        Remove build outputs and generated docs/*.png"
	@echo ""
	@echo "Variables:"
	@echo "  CXX=$(CXX)"
	@echo "  CXXFLAGS=$(CXXFLAGS)"
	@echo "  CPPFLAGS=$(CPPFLAGS)"
	@echo "  PLANTUML=$(PLANTUML)"
