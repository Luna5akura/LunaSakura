// src/engine/render/text_renderer.h

#pragma once
#include "common.h"
#include "engine/model/clip.h"
#include <glad/glad.h>
#include <ft2build.h>
#include FT_FREETYPE_H

typedef struct {
    u32 codepoint;
    float u0, v0, u1, v1; // Atlas UV
    float width, height;  // 像素尺寸
    float bearing_x, bearing_y;
    float advance;
    
    // 渲染时的动态计算值
    float q_x0, q_y0, q_x1, q_y1; 
} GlyphInfo;

typedef struct TextRenderer {
    FT_Library ft;
    FT_Face face;
    char* loaded_font_path;
    
    // Texture Atlas
    GLuint atlas_id;
    int atlas_width;
    int atlas_height;
    int atlas_x_offset; // 当前填入的位置（简单横向打包）
    
    // 缓存的字形 (简单实现：最大 128 个 ASCII 字符，实际项目可用 Hashmap)
    GlyphInfo glyphs[128]; 
    bool glyph_loaded[128];

    // 当前排版结果
    float total_width;
    float total_height;
} TextRenderer;

TextRenderer* text_renderer_create();
void text_renderer_free(TextRenderer* tr);

// 如果字体或内容变化，重新生成 Atlas 和布局
// 返回 true 表示纹理或尺寸发生了变化
bool text_renderer_update(TextRenderer* tr, Clip* clip);

GLuint text_renderer_get_texture(TextRenderer* tr);