// src/engine/model/render/text_render.c

#include "text_renderer.h"
#include <stdlib.h>
#include <stdio.h>

#define MAX_ATLAS_WIDTH 1024
#define MAX_ATLAS_HEIGHT 128

TextRenderer* text_renderer_create() {
    TextRenderer* tr = malloc(sizeof(TextRenderer));
    memset(tr, 0, sizeof(TextRenderer));
    
    if (FT_Init_FreeType(&tr->ft)) {
        fprintf(stderr, "ERROR::FREETYPE: Could not init FreeType Library\n");
        return NULL;
    }
    
    // 创建 Atlas 纹理 (GL_RED 单通道)
    glGenTextures(1, &tr->atlas_id);
    glBindTexture(GL_TEXTURE_2D, tr->atlas_id);
    // 初始分配内存
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, MAX_ATLAS_WIDTH, MAX_ATLAS_HEIGHT, 
                 0, GL_RED, GL_UNSIGNED_BYTE, NULL);
                 
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    
    tr->atlas_width = MAX_ATLAS_WIDTH;
    tr->atlas_height = MAX_ATLAS_HEIGHT;
    
    return tr;
}

void text_renderer_free(TextRenderer* tr) {
    if (!tr) return;
    if (tr->face) FT_Done_Face(tr->face);
    FT_Done_FreeType(tr->ft);
    glDeleteTextures(1, &tr->atlas_id);
    if (tr->loaded_font_path) free(tr->loaded_font_path);
    free(tr);
}

static void load_glyph_to_atlas(TextRenderer* tr, u32 char_code) {
    if (char_code >= 128 || tr->glyph_loaded[char_code]) return;
    
    if (FT_Load_Char(tr->face, char_code, FT_LOAD_RENDER)) return;
    
    FT_Bitmap* bitmap = &tr->face->glyph->bitmap;
    int w = bitmap->width;
    int h = bitmap->rows;
    
    // 简单检查是否填满 (仅支持单行打包，作为第一步实现)
    if (tr->atlas_x_offset + w > tr->atlas_width) {
        fprintf(stderr, "TextRenderer: Atlas full!\n");
        return;
    }
    
    // 上传到纹理
    glBindTexture(GL_TEXTURE_2D, tr->atlas_id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, tr->atlas_x_offset, 0, w, h, 
                    GL_RED, GL_UNSIGNED_BYTE, bitmap->buffer);
    
    // 记录元数据
    GlyphInfo* g = &tr->glyphs[char_code];
    g->codepoint = char_code;
    g->width = (float)w;
    g->height = (float)h;
    g->bearing_x = (float)tr->face->glyph->bitmap_left;
    g->bearing_y = (float)tr->face->glyph->bitmap_top;
    g->advance = (float)(tr->face->glyph->advance.x >> 6);
    
    // 计算 UV
    g->u0 = (float)tr->atlas_x_offset / MAX_ATLAS_WIDTH;
    g->v0 = 0.0f;
    g->u1 = (float)(tr->atlas_x_offset + w) / MAX_ATLAS_WIDTH;
    g->v1 = (float)h / MAX_ATLAS_HEIGHT;
    
    tr->atlas_x_offset += w + 1; // 1px padding
    tr->glyph_loaded[char_code] = true;
}

bool text_renderer_update(TextRenderer* tr, Clip* clip) {
    if (clip->type != CLIP_TYPE_TEXT) return false;
    bool dirty = false;
    
    // 1. 检查字体/字号变更
    if (!tr->loaded_font_path || strcmp(tr->loaded_font_path, clip->text.font_path) != 0) {
        if (tr->face) FT_Done_Face(tr->face);
        if (FT_New_Face(tr->ft, clip->text.font_path, 0, &tr->face)) {
            fprintf(stderr, "ERROR::FREETYPE: Failed to load font %s\n", clip->text.font_path);
            return false;
        }
        FT_Set_Pixel_Sizes(tr->face, 0, clip->text.font_size);
        
        if (tr->loaded_font_path) free(tr->loaded_font_path);
        tr->loaded_font_path = strdup(clip->text.font_path);
        
        // 清空 Atlas
        memset(tr->glyph_loaded, 0, sizeof(tr->glyph_loaded));
        tr->atlas_x_offset = 0;
        dirty = true;
    }
    
    // 2. 加载所需字符
    const char* p = clip->text.content;
    while (*p) {
        load_glyph_to_atlas(tr, (u32)*p);
        p++;
    }
    
    // 3. 计算文本整体尺寸 (Layout)
    float x = 0;
    float max_h = 0;
    p = clip->text.content;
    while (*p) {
        GlyphInfo* g = &tr->glyphs[(u32)*p];
        x += g->advance;
        if (g->height > max_h) max_h = g->height;
        p++;
    }
    
    if (clip->width != (u32)x || clip->height != (u32)max_h) {
        clip->width = (u32)x;
        clip->height = (u32)max_h;
        dirty = true;
    }
    
    return dirty;
}

GlyphInfo* text_renderer_get_glyph(TextRenderer* tr, char c) {
    return &tr->glyphs[(u32)c];
}

GLuint text_renderer_get_texture(TextRenderer* tr) {
    return tr->atlas_id;
}