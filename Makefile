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

SRC := $(shell find src -name '*.cpp' | sort)
TEST_SRC := $(shell find tests -name '*.cpp' | sort)
HEADER_FILES := $(shell find include -name '*.hpp' | sort)

CATCH_SRC := third_party/catch2/catch_amalgamated.cpp

OBJ := $(patsubst %.cpp,$(OBJ_DIR)/%.o,$(SRC))
TEST_OBJ := $(patsubst %.cpp,$(OBJ_DIR)/%.o,$(TEST_SRC))
CATCH_OBJ := $(patsubst %.cpp,$(OBJ_DIR)/%.o,$(CATCH_SRC))

FORMAT_FILES := \
	$(HEADER_FILES) \
	$(SRC) \
	$(TEST_SRC)

.PHONY: all build test format format-check docs-png clean help print-files

all: test

build: $(LIB)

$(LIB): format $(OBJ)
	@mkdir -p $(dir $@)
	ar rcs $@ $(OBJ)

$(TEST_BIN): $(LIB) $(TEST_OBJ) $(CATCH_OBJ)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -o $@ $(TEST_OBJ) $(CATCH_OBJ) $(LIB)

$(OBJ_DIR)/%.o: %.cpp
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

print-files:
	@echo "SRC:"
	@printf '  %s\n' $(SRC)
	@echo ""
	@echo "TEST_SRC:"
	@printf '  %s\n' $(TEST_SRC)
	@echo ""
	@echo "HEADER_FILES:"
	@printf '  %s\n' $(HEADER_FILES)

help:
	@echo "Targets:"
	@echo "  make              Build and run tests"
	@echo "  make build        Build static library only"
	@echo "  make test         Build and run tests"
	@echo "  make format       Format source and header files with clang-format"
	@echo "  make format-check Check formatting without modifying files"
	@echo "  make docs-png     Generate PNG diagrams from docs/*.puml"
	@echo "  make print-files  Show discovered source, test, and header files"
	@echo "  make clean        Remove build outputs and generated docs/*.png"
	@echo ""
	@echo "Variables:"
	@echo "  CXX=$(CXX)"
	@echo "  CXXFLAGS=$(CXXFLAGS)"
	@echo "  CPPFLAGS=$(CPPFLAGS)"
	@echo "  PLANTUML=$(PLANTUML)"
