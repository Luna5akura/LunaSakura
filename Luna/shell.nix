{ pkgs ? import <nixpkgs> {} }:

pkgs.mkShell {
  # 1. 编译时需要的工具 (nativeBuildInputs)
  nativeBuildInputs = with pkgs; [
    gcc
    gnumake
    pkg-config  # 必须！用于让 Makefile 找到库路径
  ];

  # 2. 链接时需要的库 (buildInputs)
  buildInputs = with pkgs; [
    ffmpeg      # 包含了 libavcodec, libavformat 等头文件和动态库
    SDL2        # 如果你后面打算用 SDL2 做预览窗口
  ];

  # 可选：设置环境变量，防止某些工具找不到库
  shellHook = ''
    echo "进入 Luna 开发环境 (NixOS)"
    echo "FFmpeg 路径: ${pkgs.ffmpeg}"
  '';
}