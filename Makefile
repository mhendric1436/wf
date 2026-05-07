CXX := clang++
CXXFLAGS := -std=c++20 -Wall -Wextra -Wpedantic -Werror -O2 -g
MT_INCLUDE := $(HOME)/repos/mt/include
MT_SRC_DIR := $(HOME)/repos/mt/src
MT_CODEGEN := $(HOME)/repos/mt/tools/mt_codegen.py
PYTHON ?= python3
CPPFLAGS   := -Iinclude -Isrc -Ithird_party -I$(MT_INCLUDE)

FORMAT := clang-format
PLANTUML ?= plantuml
PLANTUML_FLAGS ?= -DPLANTUML_LIMIT_SIZE=16384 -Xmx512m
LDFLAGS ?= -lsqlite3

BUILD_DIR := build
OBJ_DIR := $(BUILD_DIR)/obj
BIN_DIR := $(BUILD_DIR)/bin

DOCS_DIR := docs
PUML_FILES := $(wildcard $(DOCS_DIR)/*.puml)
PNG_FILES := $(PUML_FILES:.puml=.png)

LIB_NAME := libwf.a
LIB := $(BUILD_DIR)/$(LIB_NAME)

TEST_BIN := $(BIN_DIR)/wf_tests
WF_BIN := $(BIN_DIR)/wf

SRC := $(shell find src -name '*.cpp' | sort)
TEST_SRC := $(shell find tests -name '*.cpp' | sort)
CMD_SRC := $(shell find cmd -name '*.cpp' | sort)
HEADER_FILES := $(shell find include -name '*.hpp' | sort)
PRIVATE_HEADER_FILES := $(shell find src/tables -name '*.hpp' | sort)
TABLE_SCHEMA_FILES := $(shell find src/tables/schemas -name '*.mt.json' | sort)
GENERATED_TABLE_HEADERS := \
	src/tables/generated/workflow_definition_row.hpp \
	src/tables/generated/workflow_execution_row.hpp \
	src/tables/generated/workflow_step_execution_row.hpp
CODEGEN_CHECK_DIR := $(BUILD_DIR)/codegen-check
MT_SQLITE_SRC := \
	$(MT_SRC_DIR)/backends/common/schema_codec.cpp \
	$(MT_SRC_DIR)/backends/sqlite/sqlite_backend.cpp \
	$(MT_SRC_DIR)/backends/sqlite/sqlite_constraints.cpp \
	$(MT_SRC_DIR)/backends/sqlite/sqlite_document.cpp \
	$(MT_SRC_DIR)/backends/sqlite/sqlite_schema.cpp \
	$(MT_SRC_DIR)/backends/sqlite/sqlite_session.cpp \
	$(MT_SRC_DIR)/backends/sqlite/sqlite_state.cpp

CATCH_SRC := third_party/catch2/catch_amalgamated.cpp

OBJ := $(patsubst %.cpp,$(OBJ_DIR)/%.o,$(SRC))
TEST_OBJ := $(patsubst %.cpp,$(OBJ_DIR)/%.o,$(TEST_SRC))
CMD_OBJ := $(patsubst %.cpp,$(OBJ_DIR)/%.o,$(CMD_SRC))
MT_SQLITE_OBJ := $(patsubst %.cpp,$(OBJ_DIR)/%.o,$(MT_SQLITE_SRC))
CATCH_OBJ := $(patsubst %.cpp,$(OBJ_DIR)/%.o,$(CATCH_SRC))

FORMAT_FILES := \
	$(HEADER_FILES) \
	$(PRIVATE_HEADER_FILES) \
	$(SRC) \
	$(TEST_SRC) \
	$(CMD_SRC)

.PHONY: all build test cli codegen codegen-check format format-check docs-png clean help print-files

all: test cli

build: $(LIB)

cli: $(WF_BIN)

codegen: $(GENERATED_TABLE_HEADERS)

codegen-check:
	@mkdir -p $(CODEGEN_CHECK_DIR)
	$(PYTHON) $(MT_CODEGEN) src/tables/schemas/workflow_definition.mt.json -o $(CODEGEN_CHECK_DIR)/workflow_definition_row.hpp
	$(FORMAT) -i $(CODEGEN_CHECK_DIR)/workflow_definition_row.hpp
	diff -u src/tables/generated/workflow_definition_row.hpp $(CODEGEN_CHECK_DIR)/workflow_definition_row.hpp
	$(PYTHON) $(MT_CODEGEN) src/tables/schemas/workflow_execution.mt.json -o $(CODEGEN_CHECK_DIR)/workflow_execution_row.hpp
	$(FORMAT) -i $(CODEGEN_CHECK_DIR)/workflow_execution_row.hpp
	diff -u src/tables/generated/workflow_execution_row.hpp $(CODEGEN_CHECK_DIR)/workflow_execution_row.hpp
	$(PYTHON) $(MT_CODEGEN) src/tables/schemas/workflow_step_execution.mt.json -o $(CODEGEN_CHECK_DIR)/workflow_step_execution_row.hpp
	$(FORMAT) -i $(CODEGEN_CHECK_DIR)/workflow_step_execution_row.hpp
	diff -u src/tables/generated/workflow_step_execution_row.hpp $(CODEGEN_CHECK_DIR)/workflow_step_execution_row.hpp

src/tables/generated/workflow_definition_row.hpp: src/tables/schemas/workflow_definition.mt.json
	$(PYTHON) $(MT_CODEGEN) $< -o $@
	$(FORMAT) -i $@

src/tables/generated/workflow_execution_row.hpp: src/tables/schemas/workflow_execution.mt.json
	$(PYTHON) $(MT_CODEGEN) $< -o $@
	$(FORMAT) -i $@

src/tables/generated/workflow_step_execution_row.hpp: src/tables/schemas/workflow_step_execution.mt.json
	$(PYTHON) $(MT_CODEGEN) $< -o $@
	$(FORMAT) -i $@

$(LIB): codegen format $(OBJ)
	@mkdir -p $(dir $@)
	rm -f $@
	ar rcs $@ $(OBJ)

$(TEST_BIN): $(LIB) $(TEST_OBJ) $(CATCH_OBJ)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -o $@ $(TEST_OBJ) $(CATCH_OBJ) $(LIB) $(LDFLAGS)

$(WF_BIN): $(LIB) $(CMD_OBJ) $(MT_SQLITE_OBJ)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -o $@ $(CMD_OBJ) $(LIB) $(MT_SQLITE_OBJ) $(LDFLAGS)

$(OBJ_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -MMD -MP -c $< -o $@

test: $(TEST_BIN)
	$(TEST_BIN)

format:
	$(FORMAT) -i $(FORMAT_FILES)

format-check:
	$(FORMAT) --dry-run --Werror $(FORMAT_FILES)

docs-png: $(PNG_FILES)

$(DOCS_DIR)/%.png: $(DOCS_DIR)/%.puml
	$(PLANTUML) $(PLANTUML_FLAGS) -tpng $<

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
	@echo "CMD_SRC:"
	@printf '  %s\n' $(CMD_SRC)
	@echo ""
	@echo "HEADER_FILES:"
	@printf '  %s\n' $(HEADER_FILES)

help:
	@echo "Targets:"
	@echo "  make              Build and run tests, then build CLI"
	@echo "  make build        Build static library only"
	@echo "  make test         Build and run tests"
	@echo "  make cli          Build the wf CLI binary"
	@echo "  make codegen      Generate private mt row and mapping headers"
	@echo "  make codegen-check Verify generated mt row headers are current"
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
	@echo "  PLANTUML_FLAGS=$(PLANTUML_FLAGS)"
	@echo "  LDFLAGS=$(LDFLAGS)"

DEP_FILES := $(OBJ:.o=.d) $(TEST_OBJ:.o=.d) $(CMD_OBJ:.o=.d) $(MT_SQLITE_OBJ:.o=.d) $(CATCH_OBJ:.o=.d)
-include $(DEP_FILES)
