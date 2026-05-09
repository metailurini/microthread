CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -O2
CPPFLAGS ?= -Iinclude
TEST_CPPFLAGS := $(CPPFLAGS) -DMT_TESTING
FORCE_POLL_CPPFLAGS := $(CPPFLAGS) -DMT_FORCE_POLL_BACKEND

SRC := src/microthread.c
ASM_SRC :=

ifeq ($(OS),Windows_NT)
SRC += src/context_win_fiber.c
EXE := .exe
THREAD_FLAGS :=
else
UNAME_S := $(shell uname -s 2>/dev/null || echo unknown)
ifeq ($(UNAME_S),Darwin)
SRC += src/context_asm.c
ASM_SRC += src/context_asm_macos.S
else
SRC += src/context_ucontext.c
endif
EXE :=
THREAD_FLAGS := -pthread
endif

LDLIBS += $(THREAD_FLAGS)
CFLAGS += $(THREAD_FLAGS)

BUILD_DIR := build
LIB := $(BUILD_DIR)/libmicrothread.a
OBJ := $(SRC:%.c=$(BUILD_DIR)/%.o) $(ASM_SRC:%.S=$(BUILD_DIR)/%.o)
POLL_OBJ := $(BUILD_DIR)/src/microthread_poll.o
ifeq ($(UNAME_S),Darwin)
POLL_OBJ += $(BUILD_DIR)/src/context_asm_poll.o $(BUILD_DIR)/src/context_asm_macos_poll.o
else ifneq ($(OS),Windows_NT)
POLL_OBJ += $(BUILD_DIR)/src/context_ucontext_poll.o
endif

.PHONY: all test stress io-stress sanitize tsan io-tsan valgrind guard-test guard-disabled-test force-poll-build example examples sleep-example channels-example handles-example select-example try-example select-advanced-example echo-server-example clean

all: $(LIB)

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(LIB): $(OBJ)
	@mkdir -p $(dir $@)
	ar rcs $@ $^

$(BUILD_DIR)/test_v0_1$(EXE): tests/test_v0_1.c $(SRC) $(ASM_SRC)
	@mkdir -p $(dir $@)
	$(CC) $(TEST_CPPFLAGS) $(CFLAGS) $(SRC) $(ASM_SRC) $< $(LDLIBS) -o $@

$(BUILD_DIR)/test_v0_1_full$(EXE): tests/test_v0_1.c $(SRC) $(ASM_SRC)
	@mkdir -p $(dir $@)
	$(CC) $(TEST_CPPFLAGS) -DMT_FULL_STRESS $(CFLAGS) $(SRC) $(ASM_SRC) $< $(LDLIBS) -o $@

$(BUILD_DIR)/test_v0_1_asan$(EXE): tests/test_v0_1.c $(SRC) $(ASM_SRC)
	@mkdir -p $(dir $@)
	$(CC) $(TEST_CPPFLAGS) -fsanitize=address,undefined -g -O1 $(SRC) $(ASM_SRC) $< $(LDLIBS) -o $@

$(BUILD_DIR)/test_v0_2$(EXE): tests/test_v0_2.c $(SRC) $(ASM_SRC)
	@mkdir -p $(dir $@)
	$(CC) $(TEST_CPPFLAGS) $(CFLAGS) $(SRC) $(ASM_SRC) $< $(LDLIBS) -o $@

$(BUILD_DIR)/test_v0_2_full$(EXE): tests/test_v0_2.c $(SRC) $(ASM_SRC)
	@mkdir -p $(dir $@)
	$(CC) $(TEST_CPPFLAGS) -DMT_FULL_STRESS $(CFLAGS) $(SRC) $(ASM_SRC) $< $(LDLIBS) -o $@

$(BUILD_DIR)/test_v0_2_asan$(EXE): tests/test_v0_2.c $(SRC) $(ASM_SRC)
	@mkdir -p $(dir $@)
	$(CC) $(TEST_CPPFLAGS) -fsanitize=address,undefined -g -O1 $(SRC) $(ASM_SRC) $< $(LDLIBS) -o $@

$(BUILD_DIR)/test_v0_3$(EXE): tests/test_v0_3.c $(SRC) $(ASM_SRC)
	@mkdir -p $(dir $@)
	$(CC) $(TEST_CPPFLAGS) $(CFLAGS) $(SRC) $(ASM_SRC) $< $(LDLIBS) -o $@

$(BUILD_DIR)/test_v0_3_full$(EXE): tests/test_v0_3.c $(SRC) $(ASM_SRC)
	@mkdir -p $(dir $@)
	$(CC) $(TEST_CPPFLAGS) -DMT_FULL_STRESS $(CFLAGS) $(SRC) $(ASM_SRC) $< $(LDLIBS) -o $@

$(BUILD_DIR)/test_v0_3_asan$(EXE): tests/test_v0_3.c $(SRC) $(ASM_SRC)
	@mkdir -p $(dir $@)
	$(CC) $(TEST_CPPFLAGS) -fsanitize=address,undefined -g -O1 $(SRC) $(ASM_SRC) $< $(LDLIBS) -o $@

$(BUILD_DIR)/test_v0_4$(EXE): tests/test_v0_4.c $(SRC) $(ASM_SRC)
	@mkdir -p $(dir $@)
	$(CC) $(TEST_CPPFLAGS) $(CFLAGS) $(SRC) $(ASM_SRC) $< $(LDLIBS) -o $@

$(BUILD_DIR)/test_v0_4_full$(EXE): tests/test_v0_4.c $(SRC) $(ASM_SRC)
	@mkdir -p $(dir $@)
	$(CC) $(TEST_CPPFLAGS) -DMT_FULL_STRESS $(CFLAGS) $(SRC) $(ASM_SRC) $< $(LDLIBS) -o $@

$(BUILD_DIR)/test_v0_4_asan$(EXE): tests/test_v0_4.c $(SRC) $(ASM_SRC)
	@mkdir -p $(dir $@)
	$(CC) $(TEST_CPPFLAGS) -fsanitize=address,undefined -g -O1 $(SRC) $(ASM_SRC) $< $(LDLIBS) -o $@

$(BUILD_DIR)/test_v0_5$(EXE): tests/test_v0_5.c $(SRC) $(ASM_SRC)
	@mkdir -p $(dir $@)
	$(CC) $(TEST_CPPFLAGS) $(CFLAGS) $(SRC) $(ASM_SRC) $< $(LDLIBS) -o $@

$(BUILD_DIR)/test_v0_5_asan$(EXE): tests/test_v0_5.c $(SRC) $(ASM_SRC)
	@mkdir -p $(dir $@)
	$(CC) $(TEST_CPPFLAGS) -fsanitize=address,undefined -g -O1 $(SRC) $(ASM_SRC) $< $(LDLIBS) -o $@

$(BUILD_DIR)/test_v0_6$(EXE): tests/test_v0_6.c $(SRC) $(ASM_SRC)
	@mkdir -p $(dir $@)
	$(CC) $(TEST_CPPFLAGS) $(CFLAGS) $(SRC) $(ASM_SRC) $< $(LDLIBS) -o $@

$(BUILD_DIR)/test_v0_6_asan$(EXE): tests/test_v0_6.c $(SRC) $(ASM_SRC)
	@mkdir -p $(dir $@)
	$(CC) $(TEST_CPPFLAGS) -fsanitize=address,undefined -g -O1 $(SRC) $(ASM_SRC) $< $(LDLIBS) -o $@

$(BUILD_DIR)/test_v0_6_tsan$(EXE): tests/test_v0_6.c $(SRC) $(ASM_SRC)
	@mkdir -p $(dir $@)
	$(CC) $(TEST_CPPFLAGS) -fsanitize=thread -g -O1 $(SRC) $(ASM_SRC) $< $(LDLIBS) -o $@

$(BUILD_DIR)/test_v0_7$(EXE): tests/test_v0_7.c $(SRC) $(ASM_SRC)
	@mkdir -p $(dir $@)
	$(CC) $(TEST_CPPFLAGS) $(CFLAGS) $(SRC) $(ASM_SRC) $< $(LDLIBS) -o $@

$(BUILD_DIR)/test_v0_7_poll$(EXE): tests/test_v0_7.c $(SRC) $(ASM_SRC)
	@mkdir -p $(dir $@)
	$(CC) $(TEST_CPPFLAGS) -DMT_FORCE_POLL_BACKEND $(CFLAGS) $(SRC) $(ASM_SRC) $< $(LDLIBS) -o $@

$(BUILD_DIR)/test_v0_7_full$(EXE): tests/test_v0_7.c $(SRC) $(ASM_SRC)
	@mkdir -p $(dir $@)
	$(CC) $(TEST_CPPFLAGS) -DMT_IO_STRESS $(CFLAGS) $(SRC) $(ASM_SRC) $< $(LDLIBS) -o $@

$(BUILD_DIR)/test_v0_7_poll_full$(EXE): tests/test_v0_7.c $(SRC) $(ASM_SRC)
	@mkdir -p $(dir $@)
	$(CC) $(TEST_CPPFLAGS) -DMT_FORCE_POLL_BACKEND -DMT_IO_STRESS $(CFLAGS) $(SRC) $(ASM_SRC) $< $(LDLIBS) -o $@

$(BUILD_DIR)/test_v0_7_asan$(EXE): tests/test_v0_7.c $(SRC) $(ASM_SRC)
	@mkdir -p $(dir $@)
	$(CC) $(TEST_CPPFLAGS) -fsanitize=address,undefined -g -O1 $(SRC) $(ASM_SRC) $< $(LDLIBS) -o $@

$(BUILD_DIR)/test_v0_7_poll_asan$(EXE): tests/test_v0_7.c $(SRC) $(ASM_SRC)
	@mkdir -p $(dir $@)
	$(CC) $(TEST_CPPFLAGS) -DMT_FORCE_POLL_BACKEND -fsanitize=address,undefined -g -O1 $(SRC) $(ASM_SRC) $< $(LDLIBS) -o $@

$(BUILD_DIR)/test_v0_7_tsan$(EXE): tests/test_v0_7.c $(SRC) $(ASM_SRC)
	@mkdir -p $(dir $@)
	$(CC) $(TEST_CPPFLAGS) -fsanitize=thread -g -O1 $(SRC) $(ASM_SRC) $< $(LDLIBS) -o $@

$(BUILD_DIR)/test_v0_7_poll_tsan$(EXE): tests/test_v0_7.c $(SRC) $(ASM_SRC)
	@mkdir -p $(dir $@)
	$(CC) $(TEST_CPPFLAGS) -DMT_FORCE_POLL_BACKEND -fsanitize=thread -g -O1 $(SRC) $(ASM_SRC) $< $(LDLIBS) -o $@
$(BUILD_DIR)/test_public_api$(EXE): tests/test_public_api.c $(LIB)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LIB) $(LDLIBS) -o $@

$(BUILD_DIR)/libmicrothread_poll.a: $(SRC) $(ASM_SRC)
	@mkdir -p $(dir $@) $(BUILD_DIR)/src
	$(CC) $(FORCE_POLL_CPPFLAGS) $(CFLAGS) -c src/microthread.c -o $(BUILD_DIR)/src/microthread_poll.o
	@if [ -n "$(ASM_SRC)" ]; then \
		$(CC) $(FORCE_POLL_CPPFLAGS) $(CFLAGS) -c $(ASM_SRC) -o $(BUILD_DIR)/src/context_asm_macos_poll.o; \
	fi
	@if echo "$(SRC)" | grep -q 'context_ucontext.c'; then \
		$(CC) $(FORCE_POLL_CPPFLAGS) $(CFLAGS) -c src/context_ucontext.c -o $(BUILD_DIR)/src/context_ucontext_poll.o; \
	fi
	@if echo "$(SRC)" | grep -q 'context_asm.c'; then \
		$(CC) $(FORCE_POLL_CPPFLAGS) $(CFLAGS) -c src/context_asm.c -o $(BUILD_DIR)/src/context_asm_poll.o; \
	fi
	ar rcs $@ $(POLL_OBJ)

$(BUILD_DIR)/test_guard_overflow$(EXE): tests/test_guard_overflow.c $(SRC) $(ASM_SRC)
	@mkdir -p $(dir $@)
	$(CC) $(TEST_CPPFLAGS) $(CFLAGS) $(SRC) $(ASM_SRC) $< $(LDLIBS) -o $@

$(BUILD_DIR)/test_guard_disabled$(EXE): tests/test_guard_disabled.c $(SRC) $(ASM_SRC)
	@mkdir -p $(dir $@)
	$(CC) $(TEST_CPPFLAGS) -DMT_DISABLE_GUARD_PAGES $(CFLAGS) $(SRC) $(ASM_SRC) $< $(LDLIBS) -o $@

$(BUILD_DIR)/test_v0_4_guard_disabled$(EXE): tests/test_v0_4.c $(SRC) $(ASM_SRC)
	@mkdir -p $(dir $@)
	$(CC) $(TEST_CPPFLAGS) -DMT_DISABLE_GUARD_PAGES $(CFLAGS) $(SRC) $(ASM_SRC) $< $(LDLIBS) -o $@

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

$(BUILD_DIR)/echo_server$(EXE): examples/echo_server.c $(LIB)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LIB) $(LDLIBS) -o $@

test: $(BUILD_DIR)/test_v0_1$(EXE) $(BUILD_DIR)/test_v0_2$(EXE) $(BUILD_DIR)/test_v0_3$(EXE) $(BUILD_DIR)/test_v0_4$(EXE) $(BUILD_DIR)/test_v0_5$(EXE) $(BUILD_DIR)/test_v0_6$(EXE) $(BUILD_DIR)/test_v0_7$(EXE) $(BUILD_DIR)/test_v0_7_poll$(EXE) $(BUILD_DIR)/test_public_api$(EXE) $(BUILD_DIR)/test_guard_disabled$(EXE)
	$(BUILD_DIR)/test_v0_1$(EXE)
	$(BUILD_DIR)/test_v0_2$(EXE)
	$(BUILD_DIR)/test_v0_3$(EXE)
	$(BUILD_DIR)/test_v0_4$(EXE)
	$(BUILD_DIR)/test_v0_5$(EXE)
	$(BUILD_DIR)/test_v0_6$(EXE)
	$(BUILD_DIR)/test_v0_7$(EXE)
	$(BUILD_DIR)/test_v0_7_poll$(EXE)
	$(BUILD_DIR)/test_public_api$(EXE)
	$(BUILD_DIR)/test_guard_disabled$(EXE)

stress: $(BUILD_DIR)/test_v0_1_full$(EXE) $(BUILD_DIR)/test_v0_2_full$(EXE) $(BUILD_DIR)/test_v0_3_full$(EXE) $(BUILD_DIR)/test_v0_4_full$(EXE)
	$(BUILD_DIR)/test_v0_1_full$(EXE)
	$(BUILD_DIR)/test_v0_2_full$(EXE)
	$(BUILD_DIR)/test_v0_3_full$(EXE)
	$(BUILD_DIR)/test_v0_4_full$(EXE)

io-stress: $(BUILD_DIR)/test_v0_7_full$(EXE) $(BUILD_DIR)/test_v0_7_poll_full$(EXE)
	$(BUILD_DIR)/test_v0_7_full$(EXE)
	$(BUILD_DIR)/test_v0_7_poll_full$(EXE)

sanitize: $(BUILD_DIR)/test_v0_1_asan$(EXE) $(BUILD_DIR)/test_v0_2_asan$(EXE) $(BUILD_DIR)/test_v0_3_asan$(EXE) $(BUILD_DIR)/test_v0_4_asan$(EXE) $(BUILD_DIR)/test_v0_5_asan$(EXE) $(BUILD_DIR)/test_v0_6_asan$(EXE) $(BUILD_DIR)/test_v0_7_asan$(EXE) $(BUILD_DIR)/test_v0_7_poll_asan$(EXE)
	$(BUILD_DIR)/test_v0_1_asan$(EXE)
	$(BUILD_DIR)/test_v0_2_asan$(EXE)
	$(BUILD_DIR)/test_v0_3_asan$(EXE)
	$(BUILD_DIR)/test_v0_4_asan$(EXE)
	$(BUILD_DIR)/test_v0_5_asan$(EXE)
	$(BUILD_DIR)/test_v0_6_asan$(EXE)
	$(BUILD_DIR)/test_v0_7_asan$(EXE)
	$(BUILD_DIR)/test_v0_7_poll_asan$(EXE)

tsan: $(BUILD_DIR)/test_v0_6_tsan$(EXE)
	$(BUILD_DIR)/test_v0_6_tsan$(EXE)

ifeq ($(UNAME_S),Darwin)
io-tsan: $(BUILD_DIR)/test_v0_7_tsan$(EXE)
	$(BUILD_DIR)/test_v0_7_tsan$(EXE)
else
io-tsan:
	@echo "v0.7 ThreadSanitizer runtime is skipped with the ucontext backend; run on Darwin asm backend or use ASan via make sanitize"
endif

valgrind: $(BUILD_DIR)/test_v0_1$(EXE) $(BUILD_DIR)/test_v0_2$(EXE) $(BUILD_DIR)/test_v0_3$(EXE) $(BUILD_DIR)/test_v0_4$(EXE) $(BUILD_DIR)/test_v0_5$(EXE) $(BUILD_DIR)/test_v0_6$(EXE) $(BUILD_DIR)/test_v0_7$(EXE) $(BUILD_DIR)/test_v0_7_poll$(EXE)
	@if command -v valgrind >/dev/null 2>&1; then \
		valgrind --leak-check=full --show-leak-kinds=all --error-exitcode=1 $(BUILD_DIR)/test_v0_1$(EXE); \
		valgrind --leak-check=full --show-leak-kinds=all --error-exitcode=1 $(BUILD_DIR)/test_v0_2$(EXE); \
		valgrind --leak-check=full --show-leak-kinds=all --error-exitcode=1 $(BUILD_DIR)/test_v0_3$(EXE); \
		valgrind --leak-check=full --show-leak-kinds=all --error-exitcode=1 $(BUILD_DIR)/test_v0_4$(EXE); \
		valgrind --leak-check=full --show-leak-kinds=all --error-exitcode=1 $(BUILD_DIR)/test_v0_5$(EXE); \
		valgrind --leak-check=full --show-leak-kinds=all --error-exitcode=1 $(BUILD_DIR)/test_v0_6$(EXE); \
		valgrind --leak-check=full --show-leak-kinds=all --error-exitcode=1 $(BUILD_DIR)/test_v0_7$(EXE); \
		valgrind --leak-check=full --show-leak-kinds=all --error-exitcode=1 $(BUILD_DIR)/test_v0_7_poll$(EXE); \
	else \
		echo "valgrind not found; skipping valgrind target"; \
	fi

guard-test: $(BUILD_DIR)/test_guard_overflow$(EXE)
	$<

guard-disabled-test: $(BUILD_DIR)/test_guard_disabled$(EXE) $(BUILD_DIR)/test_v0_4_guard_disabled$(EXE)
	$<
	$(BUILD_DIR)/test_v0_4_guard_disabled$(EXE)

force-poll-build: $(BUILD_DIR)/libmicrothread_poll.a

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

echo-server-example: $(BUILD_DIR)/echo_server$(EXE)
	$<

clean:
	rm -rf $(BUILD_DIR)