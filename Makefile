CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -O2
CPPFLAGS ?= -Iinclude
TEST_CPPFLAGS := $(CPPFLAGS) -DMT_TESTING
FORCE_POLL_CPPFLAGS := $(CPPFLAGS) -DMT_FORCE_POLL_BACKEND
SAN_CFLAGS := -std=c11 -fsanitize=address,undefined -g -O1
TSAN_CFLAGS := -std=c11 -fsanitize=thread -g -O1

SRC := src/microthread.c src/status.c src/io.c src/io_backend.c src/io_backend_poll.c src/io_backend_epoll.c src/io_backend_kqueue.c
ASM_SRC :=
INTERNAL_SRC := \
	src/runtime_internal.h \
	src/fd_wait_internal.h \
	src/runtime.c \
	src/testing_hooks.c \
	src/stack.c \
	src/timer.c \
	src/run_queue.c \
	src/task_state.c \
	src/wait_queue.c \
	src/select_wait.c \
	src/join.c \
	src/scheduler.c \
	src/task.c \
	src/channel.c \
	src/runtime_lifecycle.c \
	src/io_backend.h \
	src/status_internal.h

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
SAN_CFLAGS += $(THREAD_FLAGS)
TSAN_CFLAGS += $(THREAD_FLAGS)

BUILD_DIR := build
LIB := $(BUILD_DIR)/libmicrothread.a
OBJ := $(SRC:%.c=$(BUILD_DIR)/%.o) $(ASM_SRC:%.S=$(BUILD_DIR)/%.o)
DIRECT_DEPS := $(SRC) $(ASM_SRC) $(INTERNAL_SRC)

POLL_OBJ := $(BUILD_DIR)/src/microthread_poll.o $(BUILD_DIR)/src/status_poll.o $(BUILD_DIR)/src/io_poll.o $(BUILD_DIR)/src/io_backend_poll_main.o $(BUILD_DIR)/src/io_backend_poll_backend.o $(BUILD_DIR)/src/io_backend_epoll_poll.o $(BUILD_DIR)/src/io_backend_kqueue_poll.o
ifeq ($(UNAME_S),Darwin)
POLL_OBJ += $(BUILD_DIR)/src/context_asm_poll.o $(BUILD_DIR)/src/context_asm_macos_poll.o
else ifneq ($(OS),Windows_NT)
POLL_OBJ += $(BUILD_DIR)/src/context_ucontext_poll.o
endif

FAST_TESTS := test_v0_1 test_v0_2 test_v0_3 test_v0_4 test_v0_5 test_v0_6 test_v0_7 test_v0_7_poll test_public_api test_public_io test_public_runtime test_guard_disabled
STRESS_TESTS := test_v0_1_full test_v0_2_full test_v0_3_full test_v0_4_full
IO_STRESS_TESTS := test_v0_7_full test_v0_7_poll_full
SAN_TESTS := test_v0_1_asan test_v0_2_asan test_v0_3_asan test_v0_4_asan test_v0_5_asan test_v0_6_asan test_v0_7_asan test_v0_7_poll_asan
VALGRIND_TESTS := test_v0_1 test_v0_2 test_v0_3 test_v0_4 test_v0_5 test_v0_6 test_v0_7 test_v0_7_poll
EXAMPLES := basic sleep channels handles select try_nonblocking select_advanced

FAST_BINS := $(foreach t,$(FAST_TESTS),$(BUILD_DIR)/$(t)$(EXE))
STRESS_BINS := $(foreach t,$(STRESS_TESTS),$(BUILD_DIR)/$(t)$(EXE))
IO_STRESS_BINS := $(foreach t,$(IO_STRESS_TESTS),$(BUILD_DIR)/$(t)$(EXE))
SAN_BINS := $(foreach t,$(SAN_TESTS),$(BUILD_DIR)/$(t)$(EXE))
VALGRIND_BINS := $(foreach t,$(VALGRIND_TESTS),$(BUILD_DIR)/$(t)$(EXE))
EXAMPLE_BINS := $(foreach e,$(EXAMPLES),$(BUILD_DIR)/$(e)$(EXE))

.PHONY: all test stress io-stress sanitize tsan io-tsan valgrind guard-test guard-disabled-test force-poll-build example examples sleep-example channels-example handles-example select-example try-example select-advanced-example echo-server-example clean

all: $(LIB)

$(BUILD_DIR)/%.o: %.c $(INTERNAL_SRC)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(LIB): $(OBJ)
	@mkdir -p $(dir $@)
	ar rcs $@ $^

define DIRECT_TEST
$(BUILD_DIR)/$(1)$(EXE): tests/$(2).c $(DIRECT_DEPS)
	@mkdir -p $$(dir $$@)
	$$(CC) $$(TEST_CPPFLAGS) $(3) $$(CFLAGS) $$(SRC) $$(ASM_SRC) $$< $$(LDLIBS) -o $$@
endef

define DIRECT_SAN_TEST
$(BUILD_DIR)/$(1)$(EXE): tests/$(2).c $(DIRECT_DEPS)
	@mkdir -p $$(dir $$@)
	$$(CC) $$(TEST_CPPFLAGS) $(3) $$(SAN_CFLAGS) $$(SRC) $$(ASM_SRC) $$< $$(LDLIBS) -o $$@
endef

define DIRECT_TSAN_TEST
$(BUILD_DIR)/$(1)$(EXE): tests/$(2).c $(DIRECT_DEPS)
	@mkdir -p $$(dir $$@)
	$$(CC) $$(TEST_CPPFLAGS) $(3) $$(TSAN_CFLAGS) $$(SRC) $$(ASM_SRC) $$< $$(LDLIBS) -o $$@
endef

define PUBLIC_TEST
$(BUILD_DIR)/$(1)$(EXE): tests/$(2).c $(LIB)
	@mkdir -p $$(dir $$@)
	$$(CC) $$(CPPFLAGS) $$(CFLAGS) $$< $$(LIB) $$(LDLIBS) -o $$@
endef

define EXAMPLE_TEMPLATE
$(BUILD_DIR)/$(1)$(EXE): examples/$(1).c $(LIB)
	@mkdir -p $$(dir $$@)
	$$(CC) $$(CPPFLAGS) $$(CFLAGS) $$< $$(LIB) $$(LDLIBS) -o $$@
endef

$(eval $(call DIRECT_TEST,test_v0_1,test_v0_1,))
$(eval $(call DIRECT_TEST,test_v0_1_full,test_v0_1,-DMT_FULL_STRESS))
$(eval $(call DIRECT_SAN_TEST,test_v0_1_asan,test_v0_1,))
$(eval $(call DIRECT_TEST,test_v0_2,test_v0_2,))
$(eval $(call DIRECT_TEST,test_v0_2_full,test_v0_2,-DMT_FULL_STRESS))
$(eval $(call DIRECT_SAN_TEST,test_v0_2_asan,test_v0_2,))
$(eval $(call DIRECT_TEST,test_v0_3,test_v0_3,))
$(eval $(call DIRECT_TEST,test_v0_3_full,test_v0_3,-DMT_FULL_STRESS))
$(eval $(call DIRECT_SAN_TEST,test_v0_3_asan,test_v0_3,))
$(eval $(call DIRECT_TEST,test_v0_4,test_v0_4,))
$(eval $(call DIRECT_TEST,test_v0_4_full,test_v0_4,-DMT_FULL_STRESS))
$(eval $(call DIRECT_SAN_TEST,test_v0_4_asan,test_v0_4,))
$(eval $(call DIRECT_TEST,test_v0_5,test_v0_5,))
$(eval $(call DIRECT_SAN_TEST,test_v0_5_asan,test_v0_5,))
$(eval $(call DIRECT_TEST,test_v0_6,test_v0_6,))
$(eval $(call DIRECT_SAN_TEST,test_v0_6_asan,test_v0_6,))
$(eval $(call DIRECT_TSAN_TEST,test_v0_6_tsan,test_v0_6,))
$(eval $(call DIRECT_TEST,test_v0_7,test_v0_7,))
$(eval $(call DIRECT_TEST,test_v0_7_poll,test_v0_7,-DMT_FORCE_POLL_BACKEND))
$(eval $(call DIRECT_TEST,test_v0_7_full,test_v0_7,-DMT_IO_STRESS))
$(eval $(call DIRECT_TEST,test_v0_7_poll_full,test_v0_7,-DMT_FORCE_POLL_BACKEND -DMT_IO_STRESS))
$(eval $(call DIRECT_SAN_TEST,test_v0_7_asan,test_v0_7,))
$(eval $(call DIRECT_SAN_TEST,test_v0_7_poll_asan,test_v0_7,-DMT_FORCE_POLL_BACKEND))
$(eval $(call DIRECT_TSAN_TEST,test_v0_7_tsan,test_v0_7,))
$(eval $(call DIRECT_TSAN_TEST,test_v0_7_poll_tsan,test_v0_7,-DMT_FORCE_POLL_BACKEND))
$(eval $(call DIRECT_TEST,test_guard_overflow,test_guard_overflow,))
$(eval $(call DIRECT_TEST,test_guard_disabled,test_guard_disabled,-DMT_DISABLE_GUARD_PAGES))
$(eval $(call DIRECT_TEST,test_v0_4_guard_disabled,test_v0_4,-DMT_DISABLE_GUARD_PAGES))
$(eval $(call PUBLIC_TEST,test_public_api,test_public_api))
$(eval $(call PUBLIC_TEST,test_public_io,test_public_io))
$(eval $(call PUBLIC_TEST,test_public_runtime,test_public_runtime))
$(foreach e,$(EXAMPLES),$(eval $(call EXAMPLE_TEMPLATE,$(e))))
$(eval $(call EXAMPLE_TEMPLATE,echo_server))

$(BUILD_DIR)/libmicrothread_poll.a: $(DIRECT_DEPS)
	@mkdir -p $(dir $@) $(BUILD_DIR)/src
	$(CC) $(FORCE_POLL_CPPFLAGS) $(CFLAGS) -c src/microthread.c -o $(BUILD_DIR)/src/microthread_poll.o
	$(CC) $(FORCE_POLL_CPPFLAGS) $(CFLAGS) -c src/status.c -o $(BUILD_DIR)/src/status_poll.o
	$(CC) $(FORCE_POLL_CPPFLAGS) $(CFLAGS) -c src/io.c -o $(BUILD_DIR)/src/io_poll.o
	$(CC) $(FORCE_POLL_CPPFLAGS) $(CFLAGS) -c src/io_backend.c -o $(BUILD_DIR)/src/io_backend_poll_main.o
	$(CC) $(FORCE_POLL_CPPFLAGS) $(CFLAGS) -c src/io_backend_poll.c -o $(BUILD_DIR)/src/io_backend_poll_backend.o
	$(CC) $(FORCE_POLL_CPPFLAGS) $(CFLAGS) -c src/io_backend_epoll.c -o $(BUILD_DIR)/src/io_backend_epoll_poll.o
	$(CC) $(FORCE_POLL_CPPFLAGS) $(CFLAGS) -c src/io_backend_kqueue.c -o $(BUILD_DIR)/src/io_backend_kqueue_poll.o
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

test: $(FAST_BINS)
	@for t in $(FAST_TESTS); do $(BUILD_DIR)/$$t$(EXE) || exit 1; done

stress: $(STRESS_BINS)
	@for t in $(STRESS_TESTS); do $(BUILD_DIR)/$$t$(EXE) || exit 1; done

io-stress: $(IO_STRESS_BINS)
	@for t in $(IO_STRESS_TESTS); do $(BUILD_DIR)/$$t$(EXE) || exit 1; done

sanitize: $(SAN_BINS)
	@for t in $(SAN_TESTS); do $(BUILD_DIR)/$$t$(EXE) || exit 1; done

tsan: $(BUILD_DIR)/test_v0_6_tsan$(EXE)
	$(BUILD_DIR)/test_v0_6_tsan$(EXE)

ifeq ($(UNAME_S),Darwin)
io-tsan: $(BUILD_DIR)/test_v0_7_tsan$(EXE)
	$(BUILD_DIR)/test_v0_7_tsan$(EXE)
else
io-tsan:
	@echo "v0.7 ThreadSanitizer runtime is skipped with the ucontext backend; run on Darwin asm backend or use ASan via make sanitize"
endif

valgrind: $(VALGRIND_BINS)
	@if command -v valgrind >/dev/null 2>&1; then \
		for t in $(VALGRIND_TESTS); do \
			valgrind --leak-check=full --show-leak-kinds=all --error-exitcode=1 $(BUILD_DIR)/$$t$(EXE) || exit 1; \
		done; \
	else \
		echo "valgrind not found; skipping valgrind target"; \
	fi

guard-test: $(BUILD_DIR)/test_guard_overflow$(EXE)
	$<

guard-disabled-test: $(BUILD_DIR)/test_guard_disabled$(EXE) $(BUILD_DIR)/test_v0_4_guard_disabled$(EXE)
	$(BUILD_DIR)/test_guard_disabled$(EXE)
	$(BUILD_DIR)/test_v0_4_guard_disabled$(EXE)

force-poll-build: $(BUILD_DIR)/libmicrothread_poll.a

example: $(BUILD_DIR)/basic$(EXE)
	$<

examples: $(EXAMPLE_BINS)
	@for e in $(EXAMPLES); do $(BUILD_DIR)/$$e$(EXE) || exit 1; done

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
