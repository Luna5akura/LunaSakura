# ==========================================
# Luna Video Language - Makefile (FFmpeg Enabled)
# ==========================================

CC = gcc

# 1. 基础编译选项
CFLAGS = -g -Wall -Wextra -std=c11 -Iinclude -Isrc -MMD -MP

# 2. 引入 FFmpeg 配置 (自动查找路径)
# 这一步会自动把 -lavcodec -lavformat 等标志加进来
FFMPEG_LIBS = libavcodec libavformat libavutil libswscale sdl2
CFLAGS += $(shell pkg-config --cflags $(FFMPEG_LIBS))
LDFLAGS += $(shell pkg-config --libs $(FFMPEG_LIBS))

# 3. 项目配置
TARGET = luna
BUILD_DIR = build
SRCS := main.c $(shell find src -name '*.c')
OBJS := $(SRCS:%.c=$(BUILD_DIR)/%.o)
DEPS := $(OBJS:.o=.d)

# 4. 规则
all: $(TARGET)

$(TARGET): $(OBJS)
	@echo "Linking $(TARGET)..."
	@$(CC) $(OBJS) -o $@ $(LDFLAGS)
	@echo "Build successful! Run with: ./$(TARGET)"

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "Compiling $<..."
	@$(CC) $(CFLAGS) -c $< -o $@

-include $(DEPS)

clean:
	@rm -rf $(BUILD_DIR) $(TARGET)

.PHONY: all clean