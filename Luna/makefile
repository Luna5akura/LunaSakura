# ==========================================
# Luna Video Language - Makefile (FFmpeg Enabled)
# ==========================================
CC = gcc

# 1. 基础编译选项
# -g: 生成调试信息 (GDB调试必须)
# -O3: 开启最高优化
# -DDEBUG_TRACE_EXECUTION: 如果你想看 VM 指令执行过程，可以取消注释下面这行
CFLAGS = -g -O3 -Wall -Wextra -std=c11 -Iinclude -Isrc -MMD -MP \
         -Wno-unused-result -Wno-implicit-function-declaration \
         -Wno-sign-compare -Wno-unused-function

# 2. 引入依赖库
# [修复] 增加了 gl 到列表中，解决 "undefined reference to symbol glEnable"
ALL_LIBS = libavcodec libavformat libavutil libswscale libswresample sdl2 gl egl libva libva-glx libdrm x11

CFLAGS += $(shell pkg-config --cflags $(ALL_LIBS))

# 3. 链接选项
# -lm: 数学库
# -pthread: 线程库 (SDL_Thread 需要)
LDFLAGS += $(shell pkg-config --libs $(ALL_LIBS)) -lm -pthread

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

# 引入自动生成的依赖关系
-include $(DEPS)

clean:
	@echo "Cleaning build directory..."
	@rm -rf $(BUILD_DIR) $(TARGET)

run: all
	@./$(TARGET)

.PHONY: all clean run