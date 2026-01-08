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
    ffmpeg      
    SDL2        
    libGL       
    nodejs_20
    xorg.libX11
    libva       
    libdrm      # <--- 【这里是新增的】提供 drm_fourcc.h 和 libdrm 库
  ];

  shellHook = ''
    echo "进入 Luna 开发环境"
    # 验证 pkg-config 能否找到 libdrm
    if pkg-config --exists libdrm; then
        echo "✅ libdrm found: $(pkg-config --modversion libdrm)"
    else
        echo "❌ libdrm not found by pkg-config"
    fi
  '';
}