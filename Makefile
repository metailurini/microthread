CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -O2
CPPFLAGS ?= -Iinclude
TEST_CPPFLAGS := $(CPPFLAGS) -DGT_TESTING

SRC := src/gt.c

ifeq ($(OS),Windows_NT)
SRC += src/context_win_fiber.c
EXE := .exe
THREAD_FLAGS :=
else
SRC += src/context_ucontext.c
EXE :=
THREAD_FLAGS := -pthread
endif

LDLIBS += $(THREAD_FLAGS)
CFLAGS += $(THREAD_FLAGS)

BUILD_DIR := build
LIB := $(BUILD_DIR)/libgt.a
OBJ := $(SRC:%.c=$(BUILD_DIR)/%.o)

.PHONY: all test stress sanitize tsan valgrind guard-test guard-disabled-test example examples sleep-example channels-example handles-example select-example try-example select-advanced-example clean

all: $(LIB)

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(LIB): $(OBJ)
	@mkdir -p $(dir $@)
	ar rcs $@ $^

$(BUILD_DIR)/test_v0_1$(EXE): tests/test_v0_1.c $(SRC)
	@mkdir -p $(dir $@)
	$(CC) $(TEST_CPPFLAGS) $(CFLAGS) $(SRC) $< $(LDLIBS) -o $@

$(BUILD_DIR)/test_v0_1_full$(EXE): tests/test_v0_1.c $(SRC)
	@mkdir -p $(dir $@)
	$(CC) $(TEST_CPPFLAGS) -DGT_FULL_STRESS $(CFLAGS) $(SRC) $< $(LDLIBS) -o $@

$(BUILD_DIR)/test_v0_1_asan$(EXE): tests/test_v0_1.c $(SRC)
	@mkdir -p $(dir $@)
	$(CC) $(TEST_CPPFLAGS) -fsanitize=address,undefined -g -O1 $(SRC) $< $(LDLIBS) -o $@

$(BUILD_DIR)/test_v0_2$(EXE): tests/test_v0_2.c $(SRC)
	@mkdir -p $(dir $@)
	$(CC) $(TEST_CPPFLAGS) $(CFLAGS) $(SRC) $< $(LDLIBS) -o $@

$(BUILD_DIR)/test_v0_2_full$(EXE): tests/test_v0_2.c $(SRC)
	@mkdir -p $(dir $@)
	$(CC) $(TEST_CPPFLAGS) -DGT_FULL_STRESS $(CFLAGS) $(SRC) $< $(LDLIBS) -o $@

$(BUILD_DIR)/test_v0_2_asan$(EXE): tests/test_v0_2.c $(SRC)
	@mkdir -p $(dir $@)
	$(CC) $(TEST_CPPFLAGS) -fsanitize=address,undefined -g -O1 $(SRC) $< $(LDLIBS) -o $@

$(BUILD_DIR)/test_v0_3$(EXE): tests/test_v0_3.c $(SRC)
	@mkdir -p $(dir $@)
	$(CC) $(TEST_CPPFLAGS) $(CFLAGS) $(SRC) $< $(LDLIBS) -o $@

$(BUILD_DIR)/test_v0_3_full$(EXE): tests/test_v0_3.c $(SRC)
	@mkdir -p $(dir $@)
	$(CC) $(TEST_CPPFLAGS) -DGT_FULL_STRESS $(CFLAGS) $(SRC) $< $(LDLIBS) -o $@

$(BUILD_DIR)/test_v0_3_asan$(EXE): tests/test_v0_3.c $(SRC)
	@mkdir -p $(dir $@)
	$(CC) $(TEST_CPPFLAGS) -fsanitize=address,undefined -g -O1 $(SRC) $< $(LDLIBS) -o $@

$(BUILD_DIR)/test_v0_4$(EXE): tests/test_v0_4.c $(SRC)
	@mkdir -p $(dir $@)
	$(CC) $(TEST_CPPFLAGS) $(CFLAGS) $(SRC) $< $(LDLIBS) -o $@

$(BUILD_DIR)/test_v0_4_full$(EXE): tests/test_v0_4.c $(SRC)
	@mkdir -p $(dir $@)
	$(CC) $(TEST_CPPFLAGS) -DGT_FULL_STRESS $(CFLAGS) $(SRC) $< $(LDLIBS) -o $@

$(BUILD_DIR)/test_v0_4_asan$(EXE): tests/test_v0_4.c $(SRC)
	@mkdir -p $(dir $@)
	$(CC) $(TEST_CPPFLAGS) -fsanitize=address,undefined -g -O1 $(SRC) $< $(LDLIBS) -o $@

$(BUILD_DIR)/test_v0_5$(EXE): tests/test_v0_5.c $(SRC)
	@mkdir -p $(dir $@)
	$(CC) $(TEST_CPPFLAGS) $(CFLAGS) $(SRC) $< $(LDLIBS) -o $@

$(BUILD_DIR)/test_v0_5_asan$(EXE): tests/test_v0_5.c $(SRC)
	@mkdir -p $(dir $@)
	$(CC) $(TEST_CPPFLAGS) -fsanitize=address,undefined -g -O1 $(SRC) $< $(LDLIBS) -o $@

$(BUILD_DIR)/test_v0_6$(EXE): tests/test_v0_6.c $(SRC)
	@mkdir -p $(dir $@)
	$(CC) $(TEST_CPPFLAGS) $(CFLAGS) $(SRC) $< $(LDLIBS) -o $@

$(BUILD_DIR)/test_v0_6_asan$(EXE): tests/test_v0_6.c $(SRC)
	@mkdir -p $(dir $@)
	$(CC) $(TEST_CPPFLAGS) -fsanitize=address,undefined -g -O1 $(SRC) $< $(LDLIBS) -o $@

$(BUILD_DIR)/test_v0_6_tsan$(EXE): tests/test_v0_6.c $(SRC)
	@mkdir -p $(dir $@)
	$(CC) $(TEST_CPPFLAGS) -fsanitize=thread -g -O1 $(SRC) $< $(LDLIBS) -o $@

$(BUILD_DIR)/test_guard_overflow$(EXE): tests/test_guard_overflow.c $(SRC)
	@mkdir -p $(dir $@)
	$(CC) $(TEST_CPPFLAGS) $(CFLAGS) $(SRC) $< $(LDLIBS) -o $@

$(BUILD_DIR)/test_guard_disabled$(EXE): tests/test_guard_disabled.c $(SRC)
	@mkdir -p $(dir $@)
	$(CC) $(TEST_CPPFLAGS) -DGT_DISABLE_GUARD_PAGES $(CFLAGS) $(SRC) $< $(LDLIBS) -o $@

$(BUILD_DIR)/test_v0_4_guard_disabled$(EXE): tests/test_v0_4.c $(SRC)
	@mkdir -p $(dir $@)
	$(CC) $(TEST_CPPFLAGS) -DGT_DISABLE_GUARD_PAGES $(CFLAGS) $(SRC) $< $(LDLIBS) -o $@

$(BUILD_DIR)/basic$(EXE): examples/basic.c $(LIB)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LIB) $(LDLIBS) -o $@

$(BUILD_DIR)/sleep$(EXE): examples/sleep.c $(LIB)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LIB) $(LDLIBS) -o $@

$(BUILD_DIR)/channels$(EXE): examples/channels.c $(LIB)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LIB) $(LDLIBS) -o $@

$(BUILD_DIR)/handles$(EXE): examples/handles.c $(LIB)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LIB) $(LDLIBS) -o $@

$(BUILD_DIR)/select$(EXE): examples/select.c $(LIB)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LIB) $(LDLIBS) -o $@

$(BUILD_DIR)/try_nonblocking$(EXE): examples/try_nonblocking.c $(LIB)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LIB) $(LDLIBS) -o $@

$(BUILD_DIR)/select_advanced$(EXE): examples/select_advanced.c $(LIB)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LIB) $(LDLIBS) -o $@

test: $(BUILD_DIR)/test_v0_1$(EXE) $(BUILD_DIR)/test_v0_2$(EXE) $(BUILD_DIR)/test_v0_3$(EXE) $(BUILD_DIR)/test_v0_4$(EXE) $(BUILD_DIR)/test_v0_5$(EXE) $(BUILD_DIR)/test_v0_6$(EXE) $(BUILD_DIR)/test_guard_disabled$(EXE)
	$(BUILD_DIR)/test_v0_1$(EXE)
	$(BUILD_DIR)/test_v0_2$(EXE)
	$(BUILD_DIR)/test_v0_3$(EXE)
	$(BUILD_DIR)/test_v0_4$(EXE)
	$(BUILD_DIR)/test_v0_5$(EXE)
	$(BUILD_DIR)/test_v0_6$(EXE)
	$(BUILD_DIR)/test_guard_disabled$(EXE)

stress: $(BUILD_DIR)/test_v0_1_full$(EXE) $(BUILD_DIR)/test_v0_2_full$(EXE) $(BUILD_DIR)/test_v0_3_full$(EXE) $(BUILD_DIR)/test_v0_4_full$(EXE)
	$(BUILD_DIR)/test_v0_1_full$(EXE)
	$(BUILD_DIR)/test_v0_2_full$(EXE)
	$(BUILD_DIR)/test_v0_3_full$(EXE)
	$(BUILD_DIR)/test_v0_4_full$(EXE)

sanitize: $(BUILD_DIR)/test_v0_1_asan$(EXE) $(BUILD_DIR)/test_v0_2_asan$(EXE) $(BUILD_DIR)/test_v0_3_asan$(EXE) $(BUILD_DIR)/test_v0_4_asan$(EXE) $(BUILD_DIR)/test_v0_5_asan$(EXE) $(BUILD_DIR)/test_v0_6_asan$(EXE)
	$(BUILD_DIR)/test_v0_1_asan$(EXE)
	$(BUILD_DIR)/test_v0_2_asan$(EXE)
	$(BUILD_DIR)/test_v0_3_asan$(EXE)
	$(BUILD_DIR)/test_v0_4_asan$(EXE)
	$(BUILD_DIR)/test_v0_5_asan$(EXE)
	$(BUILD_DIR)/test_v0_6_asan$(EXE)

tsan: $(BUILD_DIR)/test_v0_6_tsan$(EXE)
	$(BUILD_DIR)/test_v0_6_tsan$(EXE)

valgrind: $(BUILD_DIR)/test_v0_1$(EXE) $(BUILD_DIR)/test_v0_2$(EXE) $(BUILD_DIR)/test_v0_3$(EXE) $(BUILD_DIR)/test_v0_4$(EXE) $(BUILD_DIR)/test_v0_5$(EXE) $(BUILD_DIR)/test_v0_6$(EXE)
	@if command -v valgrind >/dev/null 2>&1; then \
		valgrind --leak-check=full --show-leak-kinds=all --error-exitcode=1 $(BUILD_DIR)/test_v0_1$(EXE); \
		valgrind --leak-check=full --show-leak-kinds=all --error-exitcode=1 $(BUILD_DIR)/test_v0_2$(EXE); \
		valgrind --leak-check=full --show-leak-kinds=all --error-exitcode=1 $(BUILD_DIR)/test_v0_3$(EXE); \
		valgrind --leak-check=full --show-leak-kinds=all --error-exitcode=1 $(BUILD_DIR)/test_v0_4$(EXE); \
		valgrind --leak-check=full --show-leak-kinds=all --error-exitcode=1 $(BUILD_DIR)/test_v0_5$(EXE); \
		valgrind --leak-check=full --show-leak-kinds=all --error-exitcode=1 $(BUILD_DIR)/test_v0_6$(EXE); \
	else \
		echo "valgrind not found; skipping valgrind target"; \
	fi

guard-test: $(BUILD_DIR)/test_guard_overflow$(EXE)
	$<

guard-disabled-test: $(BUILD_DIR)/test_guard_disabled$(EXE) $(BUILD_DIR)/test_v0_4_guard_disabled$(EXE)
	$<
	$(BUILD_DIR)/test_v0_4_guard_disabled$(EXE)

example: $(BUILD_DIR)/basic$(EXE)
	$<

examples: $(BUILD_DIR)/basic$(EXE) $(BUILD_DIR)/sleep$(EXE) $(BUILD_DIR)/channels$(EXE) $(BUILD_DIR)/handles$(EXE) $(BUILD_DIR)/select$(EXE) $(BUILD_DIR)/try_nonblocking$(EXE) $(BUILD_DIR)/select_advanced$(EXE)
	$(BUILD_DIR)/basic$(EXE)
	$(BUILD_DIR)/sleep$(EXE)
	$(BUILD_DIR)/channels$(EXE)
	$(BUILD_DIR)/handles$(EXE)
	$(BUILD_DIR)/select$(EXE)
	$(BUILD_DIR)/try_nonblocking$(EXE)
	$(BUILD_DIR)/select_advanced$(EXE)

sleep-example: $(BUILD_DIR)/sleep$(EXE)
	$<

channels-example: $(BUILD_DIR)/channels$(EXE)
	$<

handles-example: $(BUILD_DIR)/handles$(EXE)
	$<

select-example: $(BUILD_DIR)/select$(EXE)
	$<

try-example: $(BUILD_DIR)/try_nonblocking$(EXE)
	$<

select-advanced-example: $(BUILD_DIR)/select_advanced$(EXE)
	$<

clean:
	rm -rf $(BUILD_DIR)