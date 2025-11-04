// 嵌入视频和音频裸流到程序数据段
.section .data
.global video_payload, video_payload_end
.p2align 3  // 8字节对齐（提升内存访问效率）
video_payload:
.incbin VIDEO_FILE  // 嵌入 Makefile 传递的 VIDEO_FILE（build/video.frame）
video_payload_end:

.global audio_payload, audio_payload_end
.p2align 3
audio_payload:
.incbin AUDIO_FILE  // 嵌入 Makefile 传递的 AUDIO_FILE（build/audio.pcm）
audio_payload_end: