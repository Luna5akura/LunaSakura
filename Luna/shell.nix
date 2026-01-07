{ pkgs ? import <nixpkgs> {} }:

pkgs.mkShell {
  # 1. 编译时需要的工具
  nativeBuildInputs = with pkgs; [
    gcc
    gnumake
    pkg-config
  ];

  # 2. 链接时需要的库
  buildInputs = with pkgs; [
    ffmpeg      # 包含了 libavcodec, libavformat 等
    SDL2        # 窗口和上下文管理
    libGL       # OpenGL 核心库 (Me<tab>a)
    nodejs_20 
  ];

  shellHook = ''
    echo "进入 Luna 开发环境"
  '';
}