// src/engine/render/text_renderer.h

#pragma once
#include "common.h"
#include "engine/model/clip.h"
#include <glad/glad.h>
#include <ft2build.h>
#include FT_FREETYPE_H

// 单个字符的布局信息
typedef struct {
    u32 codepoint;
    float u0, v0, u1, v1; // 在 Atlas 中的 UV 坐标
    float width, height;  // 纹理像素大小
    float bearing_x, bearing_y; // 排版偏移
    float advance; // 前进距离
} GlyphInfo;

typedef struct TextRenderer {
    FT_Library ft;
    FT_Face face;
    char* loaded_font_path;
    
    GLuint atlas_id;
    int atlas_width;
    int atlas_height;
    int atlas_x_offset; 
    
    // 简易缓存：支持 ASCII (0-127)。实际生产应使用哈希表支持 Unicode。
    GlyphInfo glyphs[128]; 
    bool glyph_loaded[128];

} TextRenderer;

TextRenderer* text_renderer_create();
void text_renderer_free(TextRenderer* tr);

// 准备渲染数据 (检查字体变更，更新 Atlas，计算宽高)
// 返回 true 表示数据已更新
bool text_renderer_update(TextRenderer* tr, Clip* clip);

// 辅助函数：获取内部信息
GlyphInfo* text_renderer_get_glyph(TextRenderer* tr, char c);
GLuint text_renderer_get_texture(TextRenderer* tr);