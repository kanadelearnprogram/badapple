# 配置参数（与代码一致）
VIDEO_ROW       = 60
VIDEO_COL       = 80
AUDIO_FREQ      = 44100
AUDIO_CHANNEL   = 1
VIDEO_SRC       = bad-apple.mp4
BUILD_DIR       = build
VIDEO_BARE      = $(BUILD_DIR)/video.frame
AUDIO_BARE      = $(BUILD_DIR)/audio.pcm
TARGET          = bad-apple-sync  # 同步版可执行文件
C_SRCS          = bad-apple.c  # 上面的同步版代码文件名
ASM_SRCS        = resources.S
C_OBJS          = $(C_SRCS:.c=.o)
ASM_OBJS        = $(ASM_SRCS:.S=.o)
ALL_OBJS        = $(C_OBJS) $(ASM_OBJS)

# 工具链参数（SDL2 编译/链接）
CC              = gcc
AS              = gcc
CFLAGS          = -Wall -Wextra -O2 `sdl2-config --cflags`
CFLAGS          += -DVIDEO_ROW=$(VIDEO_ROW) -DVIDEO_COL=$(VIDEO_COL)
CFLAGS          += -DAUDIO_FREQ=$(AUDIO_FREQ) -DAUDIO_CHANNEL=$(AUDIO_CHANNEL)
LDFLAGS         = `sdl2-config --libs`  # 仅链接 SDL2 音频库
ASFLAGS         = -c \
                  -DVIDEO_FILE=\"$(abspath $(VIDEO_BARE))\" \
                  -DAUDIO_FILE=\"$(abspath $(AUDIO_BARE))\"

# 伪目标
.PHONY: all clean play

# 默认目标：编译+生成音视频裸流
all: $(BUILD_DIR) $(VIDEO_BARE) $(AUDIO_BARE) $(TARGET)

# 创建 build 目录
$(BUILD_DIR):
	[ -d $(BUILD_DIR) ] || mkdir -p $(BUILD_DIR)

# 视频转换：MP4 → 1位黑白位图裸流
$(VIDEO_BARE): $(VIDEO_SRC) $(BUILD_DIR)
	ffmpeg -i $(VIDEO_SRC) -y \
		-f image2pipe \
		-s $(VIDEO_COL)x$(VIDEO_ROW) \
		-vcodec rawvideo \
		-pix_fmt monow \
		-r 30 \
		$@

# 音频转换：MP4 → 16位单声道 PCM 裸流
$(AUDIO_BARE): $(VIDEO_SRC) $(BUILD_DIR)
	ffmpeg -i $(VIDEO_SRC) -y \
		-vn \
		-acodec pcm_s16le \
		-f s16le \
		-ac $(AUDIO_CHANNEL) \
		-ar $(AUDIO_FREQ) \
		$@

# C 文件编译（无 AM 依赖，仅 SDL）
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# 汇编文件编译（嵌入音视频数据）
$(ASM_OBJS): $(ASM_SRCS) $(VIDEO_BARE) $(AUDIO_BARE)
	$(AS) $(ASFLAGS) $< -o $@

# 链接生成可执行文件
$(TARGET): $(ALL_OBJS)
	$(CC) $(ALL_OBJS) -o $@ $(LDFLAGS)

# 清理
clean:
	rm -rf $(BUILD_DIR) $(ALL_OBJS) $(TARGET)
	@echo "✅ 清理完成！"

# 快捷播放
play: all
	./$(TARGET)