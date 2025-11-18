#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <SDL2/SDL.h>

// 配置参数（与音视频数据一致）
#define VIDEO_ROW 60
#define VIDEO_COL 80
#define AUDIO_FREQ 44100
#define AUDIO_CHANNEL 1
#define FPS 30  // 视频帧率，与音频同步基准

// 字符画定义（用#替代█，避免多字符警告，兼容性更好）
#define CHAR_WHITE '.'
#define CHAR_BLACK '#'

// 嵌入的音视频数据（来自 resources.S）
extern uint8_t video_payload, video_payload_end; // 数据的起始和结束地址
extern uint8_t audio_payload, audio_payload_end;

// 视频帧结构体（1位/像素，黑白位图）
typedef struct {
    uint8_t pixel[VIDEO_ROW * VIDEO_COL / 8];
} frame_t;

// SDL 音频全局变量（修复SDL API大小写错误）
static const uint8_t* sdl_audio_data = NULL;
static size_t sdl_audio_total = 0;  // 音频总字节数
static size_t sdl_audio_played = 0; // 已播放音频字节数
static SDL_mutex* sdl_audio_mutex = NULL;
// 修复：SDL2条件变量类型是 SDL_cond*（小写c），不是 SDL_Cond*
static SDL_cond* sdl_audio_cond = NULL;  
static bool sdl_audio_running = false;   // 音频播放状态

// 在文件全局变量区域添加
static char last_frame[VIDEO_ROW * VIDEO_COL] = {0};  // 存储上一帧的字符状态
static bool is_first_frame = true;  // 标记是否为第一帧（首次需要全量绘制）

// 1. 提取像素位
static uint8_t get_pixel(const uint8_t* p, int idx) {
    int byte_idx = idx / 8;
    int bit_idx = 7 - (idx % 8);  // 位序翻转，匹配 monow 格式
    return (p[byte_idx] >> bit_idx) & 1;
}

// 2. 命令行绘制字符画帧（ANSI 序列，无 AM 依赖）
static void draw_frame(const frame_t* frame) {
    // 第一帧需要全量绘制（初始化 last_frame）
    if (is_first_frame) {
        printf("\033[H\033[J");  // 清屏+光标归位（仅第一帧执行）
        for (int y = 0; y < VIDEO_ROW; y++) {
            for (int x = 0; x < VIDEO_COL; x++) {
                int pixel_idx = y * VIDEO_COL + x;
                uint8_t pixel = get_pixel(frame->pixel, pixel_idx);
                char c = pixel ? CHAR_BLACK : CHAR_WHITE;
                putchar(c);
                last_frame[pixel_idx] = c;  // 初始化上一帧数据
            }
            putchar('\n');
        }
        is_first_frame = false;
    } else {
        // 非第一帧：仅更新差异位置
        for (int y = 0; y < VIDEO_ROW; y++) {
            for (int x = 0; x < VIDEO_COL; x++) {
                int pixel_idx = y * VIDEO_COL + x;
                uint8_t pixel = get_pixel(frame->pixel, pixel_idx);
                char current_c = pixel ? CHAR_BLACK : CHAR_WHITE;
                
                // 对比上一帧，仅差异位置需要重绘
                if (current_c != last_frame[pixel_idx]) {
                    // ANSI 光标定位：\033[行;列H（行/列从1开始）
                    printf("\033[%d;%dH%c", y + 1, x + 1, current_c);
                    last_frame[pixel_idx] = current_c;  // 更新上一帧数据
                }
            }
        }
    }
    fflush(stdout);  // 强制刷新输出缓冲区
}

// 3. SDL 音频回调函数（更新播放进度，用于同步）
void sdl_audio_callback(void* userdata, uint8_t* stream, int len) {
    (void)userdata;
    SDL_memset(stream, 0, len);  // 清空缓冲区
    SDL_LockMutex(sdl_audio_mutex);

    if (!sdl_audio_running || sdl_audio_played >= sdl_audio_total) {
        SDL_UnlockMutex(sdl_audio_mutex);
        return;
    }

    // 计算本次播放长度
    size_t play_len = (size_t)len;
    if (sdl_audio_played + play_len > sdl_audio_total) {
        play_len = sdl_audio_total - sdl_audio_played;
    }

    // 复制音频数据到缓冲区
    SDL_memcpy(stream, sdl_audio_data + sdl_audio_played, play_len);
    sdl_audio_played += play_len;

    // 修复：SDL2条件变量信号函数是 SDL_CondSignal（驼峰命名），不是 SDL_Cond_signal
    SDL_CondSignal(sdl_audio_cond);
    SDL_UnlockMutex(sdl_audio_mutex);
}

// 4. 初始化 SDL 音视频（无 AM 依赖）
static bool init_sdl() {
    // 初始化 SDL 音频模块（字符画无需 SDL 视频）
    if (SDL_Init(SDL_INIT_AUDIO) < 0) {
        fprintf(stderr, "❌ SDL 初始化失败：%s\n", SDL_GetError());
        return false;
    }

    // 绑定音频数据
    sdl_audio_data = &audio_payload;
    sdl_audio_total = &audio_payload_end - &audio_payload;
    if (sdl_audio_total == 0) {
        fprintf(stderr, "❌ 无音频数据！\n");
        SDL_Quit();
        return false;
    }

    // 配置音频参数（匹配 PCM 格式）
    SDL_AudioSpec audio_spec = {0};
    audio_spec.freq = AUDIO_FREQ;
    audio_spec.format = AUDIO_S16SYS;  // 16位 PCM，跨平台兼容
    audio_spec.channels = AUDIO_CHANNEL;
    audio_spec.samples = 4096;  // 缓冲区大小，稳定播放
    audio_spec.callback = sdl_audio_callback;

    // 创建同步用的互斥锁和条件变量
    sdl_audio_mutex = SDL_CreateMutex();
    sdl_audio_cond = SDL_CreateCond();  // 类型正确，无指针不匹配
    if (!sdl_audio_mutex || !sdl_audio_cond) {
        fprintf(stderr, "❌ 创建同步对象失败：%s\n", SDL_GetError());
        SDL_DestroyMutex(sdl_audio_mutex);
        SDL_DestroyCond(sdl_audio_cond);
        SDL_Quit();
        return false;
    }

    // 打开音频设备
    if (SDL_OpenAudio(&audio_spec, NULL) < 0) {
        fprintf(stderr, "❌ 打开音频设备失败：%s\n", SDL_GetError());
        SDL_DestroyMutex(sdl_audio_mutex);
        SDL_DestroyCond(sdl_audio_cond);
        SDL_Quit();
        return false;
    }

    sdl_audio_running = true;
    SDL_PauseAudio(0);  // 启动音频播放
    printf("✅ SDL 音频初始化成功，音画同步模式\n");
    return true;
}

// 5. 清理 SDL 资源
static void clean_sdl() {
    sdl_audio_running = false;
    SDL_PauseAudio(1);
    SDL_CloseAudio();
    SDL_DestroyMutex(sdl_audio_mutex);
    SDL_DestroyCond(sdl_audio_cond);  // 指针类型匹配，无警告
    SDL_Quit();
}

// 6. 计算音频已播放时长（微秒），用于同步
static uint64_t audio_played_us() {
    SDL_LockMutex(sdl_audio_mutex);
    // 16位音频 = 2字节/样本，时长 = 已播放字节数 / 每秒字节数 * 1e6 微秒
    uint64_t played_us = (uint64_t)sdl_audio_played * 1000000 / 
                        (AUDIO_FREQ * sizeof(int16_t) * AUDIO_CHANNEL);
    SDL_UnlockMutex(sdl_audio_mutex);
    return played_us;
}

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;
    printf("🎬 命令行字符画 + SDL 音频（音画同步）\n");
    printf("💡 按 Ctrl+C 退出，字符画用 #/空格显示\n\n");
    sleep(1);

    // 1. 加载视频数据
    frame_t* frame_start = (frame_t*)&video_payload;
    frame_t* frame_end = (frame_t*)&video_payload_end;
    int total_frames = frame_end - frame_start;
    if (total_frames == 0) {
        fprintf(stderr, "❌ 无视频数据！请检查 bad-apple.mp4 是否存在\n");
        return 1;
    }
    printf("✅ 检测到视频：%d 帧，分辨率 %dx%d，帧率 %dFPS\n", 
           total_frames, VIDEO_COL, VIDEO_ROW, FPS);

    // 2. 校验音视频时长匹配（同步前提）
    uint64_t video_total_us = (uint64_t)total_frames * 1000000 / FPS;  // 视频总时长
    uint64_t audio_total_us = (uint64_t)sdl_audio_total * 1000000 / 
                             (AUDIO_FREQ * sizeof(int16_t) * AUDIO_CHANNEL);  // 音频总时长
    if (abs((int)(video_total_us - audio_total_us)) > 1000000) {  // 差值超1秒警告
        fprintf(stderr, "⚠️  警告：音视频时长不匹配！视频%.1fs，音频%.1fs，可能影响同步\n",
                video_total_us/1e6, audio_total_us/1e6);
    }
    sleep(1);

    // 3. 初始化 SDL 音频
    bool has_audio = init_sdl();
    if (!has_audio) {
        printf("⚠️  音频初始化失败，仅播放字符画（无同步）\n");
        sleep(1);
    }

    // 4. 字符画 + 音画同步播放（核心逻辑）
    uint64_t video_start_us = SDL_GetPerformanceCounter() * 1000000 / SDL_GetPerformanceFrequency();
    for (int frame_idx = 0; frame_idx < total_frames; frame_idx++) {
        frame_t* current_frame = frame_start + frame_idx;

        // 绘制当前帧
        draw_frame(current_frame);

        // 核心同步：等待音频追上当前视频进度
        if (has_audio) {
            // 当前帧理论应播放的时长（视频进度）
            uint64_t expected_us = (uint64_t)frame_idx * 1000000 / FPS;
            // 音频实际已播放时长
            uint64_t actual_us = audio_played_us();
            // 音频滞后时，等待音频追上（最多等1帧时长，避免死等）
            if (actual_us < expected_us) {
                uint64_t wait_us = expected_us - actual_us;
                if (wait_us > 1000000/FPS) wait_us = 1000000/FPS;
                usleep(wait_us);  // 等待音频进度
            }
        } else {
            // 无音频时，固定帧率播放
            uint64_t now_us = SDL_GetPerformanceCounter() * 1000000 / SDL_GetPerformanceFrequency();
            uint64_t next_us = video_start_us + (frame_idx + 1) * 1000000 / FPS;
            if (now_us < next_us) {
                usleep(next_us - now_us);
            }
        }
    }

    // 5. 等待音频播放完毕（视频结束后收尾）
    if (has_audio) {
        printf("\033[0;%dH", VIDEO_ROW + 1);  // 光标移到视频下方
        printf("📺 视频播放完毕，等待音频结束...\n");
        while (audio_played_us() < audio_total_us) {
            usleep(100000);  // 等待音频收尾
        }
    }

    // 6. 清理资源
    printf("\033[H\033[J");  // 清屏
    printf("🎉 播放完成！\n");
    if (has_audio) clean_sdl();

    return 0;
}