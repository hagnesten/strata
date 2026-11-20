ifeq ($(origin CXX),default)
CXX := clang++
endif

BUILD := build/linux
TARGET := --target=x86_64-linux-gnu

CXXFLAGS := $(TARGET) -std=c++20 -O2 -g -ffreestanding -nostdlib -fno-exceptions -fno-rtti -fno-threadsafe-statics \
	-fno-stack-protector -fPIC -fvisibility=hidden -fasynchronous-unwind-tables -ffunction-sections -fdata-sections \
	-Wall -Wno-unused-parameter -Wno-missing-braces -DSTRATA_BUILDING -Isrc -Iinclude
LINKFLAGS := $(TARGET) -fuse-ld=lld -nostdlib -static -Wl,-e,_start -Wl,--gc-sections
LIBRARYFLAGS := $(TARGET) -fuse-ld=lld -nostdlib -shared -Wl,--gc-sections -Wl,-z,defs -Wl,-soname,libstrata.so

BASE := \
	src/base/runtime/memory.cc \
	src/base/runtime/wide.cc \
	src/base/console/console.cc \
	src/base/text/text.cc \
	src/base/file/file.cc \
	src/base/file/folder.cc \
	src/base/hash/crc32c.cc \
	src/base/hash/sha256.cc \
	src/base/hash/scram.cc \
	src/base/text/base64.cc \
	src/base/arena/arena.cc \
	src/base/sort/sort.cc \
	src/base/math/math.cc \
	src/base/clock/clock.cc \
	src/base/text/unicode.cc \
	src/base/platform/linux/system.cc \
	src/base/platform/linux/network.cc \
	src/base/platform/linux/heap.cc \
	src/base/platform/linux/zone.cc \
	src/base/platform/linux/coroutine.cc \
	src/base/platform/linux/runtime.cc

START := \
	src/base/platform/linux/start.cc

API := \
	src/api/connection.cc \
	src/api/statement.cc \
	src/api/stepping.cc \
	src/api/columns.cc \
	src/api/batch.cc

STORAGE := \
	src/storage/pager/pager.cc \
	src/storage/pager/wal.cc \
	src/storage/pager/marks.cc \
	src/storage/shared/index.cc \
	src/storage/record/record.cc \
	src/storage/btree/btree.cc \
	src/storage/btree/estimate.cc \
	src/storage/btree/copy.cc \
	src/storage/btree/build.cc

SQL := \
	src/sql/lexer/lexer.cc \
	src/sql/parser/parse_expression.cc \
	src/sql/parser/parse_statement.cc \
	src/sql/print/print.cc

ENGINE := \
	src/engine/value/value.cc \
	src/engine/value/decimal.cc \
	src/engine/value/moment.cc \
	src/engine/catalog/catalog.cc \
	src/engine/catalog/statistics.cc \
	src/engine/catalog/views.cc \
	src/engine/run/evaluate.cc \
	src/engine/plan/prepare.cc \
	src/engine/plan/explain.cc \
	src/engine/plan/join_order.cc \
	src/engine/search/tokens.cc \
	src/engine/search/postings.cc \
	src/engine/search/query.cc \
	src/engine/search/snippets.cc \
	src/engine/query/select.cc \
	src/engine/query/semi.cc \
	src/engine/query/fold.cc \
	src/engine/query/source.cc \
	src/engine/query/aggregate.cc \
	src/engine/query/window.cc \
	src/engine/functions/functions.cc \
	src/engine/functions/dates.cc \
	src/engine/functions/json.cc \
	src/engine/functions/arrays.cc \
	src/engine/write/changes.cc \
	src/engine/write/generated.cc \
	src/engine/write/returning.cc \
	src/engine/write/sequence.cc \
	src/engine/schema/schema.cc \
	src/engine/schema/index_build.cc \
	src/engine/schema/alter.cc \
	src/engine/schema/analyze.cc \
	src/engine/database/database.cc \
	src/engine/database/prepared.cc \
	src/engine/database/transaction.cc \
	src/engine/database/vacuum.cc \
	src/engine/check/check.cc

TESTS := \
	src/test/harness/harness.cc \
	src/test/base/hash_test.cc \
	src/test/storage/pager_test.cc \
	src/test/storage/btree_test.cc \
	src/test/storage/crash_test.cc \
	src/test/sql/parser_test.cc \
	src/test/engine/engine_test.cc \
	src/test/engine/checks.cc \
	src/test/engine/savepoint_test.cc \
	src/test/engine/join_test.cc \
	src/test/engine/vacuum_test.cc \
	src/test/engine/search_test.cc \
	src/test/engine/check_test.cc \
	src/test/engine/prepared_test.cc \
	src/test/engine/limit_test.cc \
	src/test/engine/update_test.cc \
	src/test/engine/semi_test.cc \
	src/test/engine/range_test.cc \
	src/test/engine/fold_test.cc \
	src/test/engine/dialect_test.cc \
	src/test/engine/concurrency_test.cc \
	src/test/api/api_test.cc

APP := \
	src/app/main/main.cc \
	src/app/shell/shell.cc \
	src/server/loop/loop.cc \
	src/server/users/users.cc \
	src/server/postgres/startup.cc \
	src/server/postgres/query.cc \
	src/server/postgres/types.cc \
	src/server/postgres/procedural.cc \
	src/server/postgres/notify.cc

PROGRAM := $(BASE) $(START) $(STORAGE) $(SQL) $(ENGINE) $(API) $(TESTS) $(APP)
PROGRAM_OBJECTS := $(patsubst src/%.cc,$(BUILD)/objects/%.o,$(PROGRAM))
LIBRARY := $(BASE) $(STORAGE) $(SQL) $(ENGINE) $(API)
LIBRARY_OBJECTS := $(patsubst src/%.cc,$(BUILD)/objects/%.o,$(LIBRARY))

.PHONY: all test clean

all: $(BUILD)/strata $(BUILD)/libstrata.so

$(BUILD)/strata: $(PROGRAM_OBJECTS)
	$(CXX) $(LINKFLAGS) -o $@ $^

$(BUILD)/libstrata.so: $(LIBRARY_OBJECTS)
	$(CXX) $(LIBRARYFLAGS) -o $@ $^

$(BUILD)/objects/%.o: src/%.cc
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

test: $(BUILD)/strata
	$(BUILD)/strata --test

clean:
	rm -rf $(BUILD)

-include $(PROGRAM_OBJECTS:.o=.d)
