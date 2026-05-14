# ==========================================
# Luna Video Language - Makefile (FFmpeg Enabled)
# ==========================================
CC = gcc

# 1. 基础编译选项
# -DDEBUG_TRACE_EXECUTION: 如果你想看 VM 指令执行过程，可以取消注释下面这行
CFLAGS = -g -O3 -Wall -Wextra -std=c11 -Iinclude -Isrc -MMD -MP \
         -Wno-unused-result -Wno-implicit-function-declaration \
         -Wno-sign-compare -Wno-unused-function

# 2. 引入依赖库
ALL_LIBS = libavcodec libavformat libavutil libswscale libswresample sdl2 gl egl libva libva-glx libdrm x11 freetype2

CFLAGS += $(shell pkg-config --cflags $(ALL_LIBS))

# 3. 链接选项
LDFLAGS += $(shell pkg-config --libs $(ALL_LIBS)) -lm -pthread

# 4. 项目配置
TARGET = luna
TEST_TARGET = luna_tests
PERF_TARGET = luna_perf
BUILD_DIR = build

SRCS := main.c $(shell find src -name '*.c')
OBJS := $(SRCS:%.c=$(BUILD_DIR)/%.o)
DEPS := $(OBJS:.o=.d)
TEST_SRCS := tests/test_runner.c $(shell find src -name '*.c')
TEST_OBJS := $(TEST_SRCS:%.c=$(BUILD_DIR)/%.o)
TEST_DEPS := $(TEST_OBJS:.o=.d)
PERF_SRCS := tests/perf_runner.c $(shell find src -name '*.c')
PERF_OBJS := $(PERF_SRCS:%.c=$(BUILD_DIR)/%.o)
PERF_DEPS := $(PERF_OBJS:.o=.d)

# 5. 规则
all: $(TARGET)

test: $(TEST_TARGET)
	@./$(TEST_TARGET)

perf: $(PERF_TARGET)
	@./$(PERF_TARGET)

$(TARGET): $(OBJS)
	@echo "Linking $(TARGET)..."
	@mkdir -p $(dir $@)
	@$(CC) $(OBJS) -o $@ $(LDFLAGS)
	@echo "Build successful! Run with: ./$(TARGET)"

$(TEST_TARGET): $(TEST_OBJS)
	@echo "Linking $(TEST_TARGET)..."
	@mkdir -p $(dir $@)
	@$(CC) $(TEST_OBJS) -o $@ $(LDFLAGS)
	@echo "Test build successful! Run with: ./$(TEST_TARGET)"

$(PERF_TARGET): $(PERF_OBJS)
	@echo "Linking $(PERF_TARGET)..."
	@mkdir -p $(dir $@)
	@$(CC) $(PERF_OBJS) -o $@ $(LDFLAGS)
	@echo "Performance build successful! Run with: ./$(PERF_TARGET)"

# 编译规则
$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "Compiling $<..."
	@$(CC) $(CFLAGS) -c $< -o $@

# 引入自动生成的依赖关系
-include $(DEPS)
-include $(TEST_DEPS)
-include $(PERF_DEPS)

clean:
	@echo "Cleaning build directory..."
	@rm -rf $(BUILD_DIR) $(TARGET)

run: all
	@./$(TARGET)

.PHONY: all clean run test perf
