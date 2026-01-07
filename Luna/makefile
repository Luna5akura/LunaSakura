# ==========================================
# Luna Video Language - Makefile (FFmpeg Enabled)
# ==========================================

CC = gcc

# 1. 基础编译选项
# -g: 生成调试信息 (GDB调试必须)
# -O2: 开启优化 (关键！否则 static inline 不会生效，VM 会很慢)
# -DDEBUG_TRACE_EXECUTION: 如果你想看 VM 指令执行过程，可以取消注释下面这行
# DEBUG_FLAGS = -DDEBUG_TRACE_EXECUTION
CFLAGS = -g -O3 -Wall -Wextra -std=c11 -Iinclude -Isrc -MMD -MP 

# 2. 引入 FFmpeg 和 SDL2 配置 (自动查找路径)
FFMPEG_LIBS = libavcodec libavformat libavutil libswscale sdl2
CFLAGS += $(shell pkg-config --cflags $(FFMPEG_LIBS))

# 3. 链接选项
# 注意：新增了 -lm (Math Library)，这是必须的
LDFLAGS += $(shell pkg-config --libs $(FFMPEG_LIBS)) -lm

# 4. 项目配置
TARGET = luna
BUILD_DIR = build
# 递归查找 src 下所有 .c 文件，以及根目录下的 main.c
SRCS := main.c $(shell find src -name '*.c')
# 将 .c 替换为 build/xxx.o
OBJS := $(SRCS:%.c=$(BUILD_DIR)/%.o)
# 依赖文件 (.d)
DEPS := $(OBJS:.o=.d)

# 5. 规则
all: $(TARGET)

$(TARGET): $(OBJS)
	@echo "Linking $(TARGET)..."
	@mkdir -p $(dir $@)
	@$(CC) $(OBJS) -o $@ $(LDFLAGS)
	@echo "Build successful! Run with: ./$(TARGET)"

# 编译规则
$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "Compiling $<..."
	@$(CC) $(CFLAGS) -c $< -o $@

# 引入自动生成的依赖关系 (处理头文件修改后的重新编译)
-include $(DEPS)

clean:
	@echo "Cleaning build directory..."
	@rm -rf $(BUILD_DIR) $(TARGET)

# 方便调试的伪目标
run: all
	@./$(TARGET)

.PHONY: all clean run