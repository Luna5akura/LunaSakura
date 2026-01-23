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
    
    // 创建 Atlas 纹理 (单通道 RED)
    glGenTextures(1, &tr->atlas_id);
    glBindTexture(GL_TEXTURE_2D, tr->atlas_id);
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

// 简单的 Atlas 打包：只支持一行，填满为止
static void load_glyph_to_atlas(TextRenderer* tr, u32 char_code) {
    if (char_code >= 128 || tr->glyph_loaded[char_code]) return;
    
    if (FT_Load_Char(tr->face, char_code, FT_LOAD_RENDER)) return;
    
    FT_Bitmap* bitmap = &tr->face->glyph->bitmap;
    int w = bitmap->width;
    int h = bitmap->rows;
    
    if (tr->atlas_x_offset + w > tr->atlas_width) {
        fprintf(stderr, "TextRenderer: Atlas full!\n");
        return;
    }
    
    // 上传位图到 Atlas
    glBindTexture(GL_TEXTURE_2D, tr->atlas_id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, tr->atlas_x_offset, 0, w, h, 
                    GL_RED, GL_UNSIGNED_BYTE, bitmap->buffer);
                    
    // 记录 Glyph 信息
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
    
    tr->atlas_x_offset += w + 1; // +1 padding
    tr->glyph_loaded[char_code] = true;
}

bool text_renderer_update(TextRenderer* tr, Clip* clip) {
    if (clip->type != CLIP_TYPE_TEXT) return false;
    
    bool dirty = false;
    
    // 1. 检查字体是否变更
    if (!tr->loaded_font_path || strcmp(tr->loaded_font_path, clip->text.font_path) != 0) {
        if (tr->face) FT_Done_Face(tr->face);
        if (FT_New_Face(tr->ft, clip->text.font_path, 0, &tr->face)) {
            fprintf(stderr, "ERROR::FREETYPE: Failed to load font %s\n", clip->text.font_path);
            return false;
        }
        FT_Set_Pixel_Sizes(tr->face, 0, clip->text.font_size);
        
        if (tr->loaded_font_path) free(tr->loaded_font_path);
        tr->loaded_font_path = strdup(clip->text.font_path);
        
        // 重置 Atlas
        memset(tr->glyph_loaded, 0, sizeof(tr->glyph_loaded));
        tr->atlas_x_offset = 0;
        dirty = true;
    }
    
    // 2. 确保所有字符都在 Atlas 中
    const char* p = clip->text.content;
    while (*p) {
        // 这里简化处理，只处理 ASCII。实际应解码 UTF-8
        load_glyph_to_atlas(tr, (u32)*p);
        p++;
    }
    
    // 3. 计算文本整体尺寸 (Layout)
    float x = 0;
    float max_h = 0;
    float min_y = 0; // 用于计算 baseline 下方的部分
    
    p = clip->text.content;
    while (*p) {
        GlyphInfo* g = &tr->glyphs[(u32)*p];
        x += g->advance;
        if (g->height > max_h) max_h = g->height;
        p++;
    }
    
    float new_w = x;
    float new_h = max_h; // 简化高度计算
    
    // 更新 Clip 的尺寸信息
    if (clip->width != (u32)new_w || clip->height != (u32)new_h) {
        clip->width = (u32)new_w;
        clip->height = (u32)new_h;
        dirty = true;
    }
    
    return dirty;
}

GLuint text_renderer_get_texture(TextRenderer* tr) {
    return tr->atlas_id;
}