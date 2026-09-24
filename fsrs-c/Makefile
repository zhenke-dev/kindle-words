# FSRS-6 in C — 极简构建（无外部依赖，仅 libm）
CC      ?= cc
AR      ?= ar
CFLAGS  ?= -std=c11 -O2 -Wall -Wextra -Wpedantic
CPPFLAGS += -Iinclude
LDLIBS  += -lm

BUILD   := build
LIB     := $(BUILD)/libfsrs.a
SO      := $(BUILD)/libfsrs.so
TEST    := $(BUILD)/test_fsrs
EXAMPLE := $(BUILD)/example
XCHECK  := $(BUILD)/crosscheck
OBJS    := $(BUILD)/fsrs_core.o $(BUILD)/fsrs_ffi.o
PIC_OBJS := $(OBJS:.o=.pic.o)

.PHONY: all clean test example crosscheck shared

all: $(LIB)

shared: $(SO)

$(BUILD):
	@mkdir -p $(BUILD)

$(BUILD)/%.o: src/%.c include/fsrs.h src/fsrs_internal.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD)/%.pic.o: src/%.c include/fsrs.h src/fsrs_internal.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -fPIC -c $< -o $@

$(LIB): $(OBJS)
	$(AR) rcs $@ $^

$(SO): $(PIC_OBJS)
	$(CC) -shared -o $@ $^ $(LDLIBS)

$(TEST): tests/test_fsrs.c $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LIB) -o $@ $(LDLIBS)

$(EXAMPLE): examples/schedule.c $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LIB) -o $@ $(LDLIBS)

test: $(TEST)
	@$(TEST)

example: $(EXAMPLE)
	@$(EXAMPLE)

# C 内核与 scripts/reference.py 的独立实现逐项比对（1124 组状态转移）
$(XCHECK): tests/crosscheck.c $(LIB)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LIB) -o $@ $(LDLIBS)

crosscheck: $(XCHECK)
	@$(XCHECK) > $(BUILD)/crosscheck.csv
	@python3 scripts/crosscheck.py $(BUILD)/crosscheck.csv

clean:
	rm -rf $(BUILD)
