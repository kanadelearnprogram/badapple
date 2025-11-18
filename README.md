# Bad Apple 终端字符画 + SDL 音画同步版

一个基于 **SDL2 音频** 和 **终端字符画** 的 Bad Apple 播放器，支持音画严格同步，本地 Linux 环境直接编译运行（需安装 SDL2 核心依赖，确保音频稳定）。

## 环境依赖（必须安装 SDL2，确保音频正常）
需提前安装以下工具（Ubuntu/Debian 系统直接复制执行）：
```bash
# 1. 基础编译工具（gcc/make）+ Git（克隆仓库）
sudo apt update && sudo apt install -y gcc git make

# 2. SDL2 核心依赖（音频播放核心，必须安装）
sudo apt install -y libsdl2-dev libsdl2-alsa-dev libsdl2-2.0-0

# 3. ffmpeg（自动转换音视频为裸流，用于嵌入程序）
sudo apt install -y ffmpeg
```

## 快速开始（3步搞定播放）

### 1. 克隆仓库
```bash
git clone https://github.com/kanadelearnprogram/badapple.git
cd badapple  # 进入项目目录
```

### 2. 编译项目
一键完成音视频转换 + 代码编译，首次运行建议执行 `make clean` 清理残留：
```bash
make        # 编译生成可执行文件 bad-apple-sync
```

### 3. 运行播放
编译完成后，直接执行以下命令启动：
```bash
make play
```
