// src/engine/model/render/text_render.c

#include "text_renderer.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define MAX_ATLAS_WIDTH 1024
#define MAX_ATLAS_HEIGHT 128
// 限制为 ASCII + Basic Latin 范围，防止数组越界
#define MAX_GLYPHS 128 

TextRenderer* text_renderer_create() {
    TextRenderer* tr = malloc(sizeof(TextRenderer));
    memset(tr, 0, sizeof(TextRenderer));
    
    if (FT_Init_FreeType(&tr->ft)) {
        fprintf(stderr, "ERROR::FREETYPE: Init failed\n");
        free(tr);
        return NULL;
    }
    
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

static void load_glyph_to_atlas(TextRenderer* tr, u32 char_code) {
    // 越界保护
    if (char_code >= MAX_GLYPHS || tr->glyph_loaded[char_code]) return;
    
    if (FT_Load_Char(tr->face, char_code, FT_LOAD_RENDER)) return;
    
    FT_Bitmap* bitmap = &tr->face->glyph->bitmap;
    int w = bitmap->width;
    int h = bitmap->rows;
    
    if (tr->atlas_x_offset + w >= tr->atlas_width) {
        // Atlas full - 实际应用中这里应该换行或者重建 Atlas
        return; 
    }
    
    glBindTexture(GL_TEXTURE_2D, tr->atlas_id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, tr->atlas_x_offset, 0, w, h, 
                    GL_RED, GL_UNSIGNED_BYTE, bitmap->buffer);
    
    GlyphInfo* g = &tr->glyphs[char_code];
    g->codepoint = char_code;
    g->width = (float)w;
    g->height = (float)h;
    g->bearing_x = (float)tr->face->glyph->bitmap_left;
    g->bearing_y = (float)tr->face->glyph->bitmap_top;
    g->advance = (float)(tr->face->glyph->advance.x >> 6);
    
    g->u0 = (float)tr->atlas_x_offset / MAX_ATLAS_WIDTH;
    g->v0 = 0.0f;
    g->u1 = (float)(tr->atlas_x_offset + w) / MAX_ATLAS_WIDTH;
    g->v1 = (float)h / MAX_ATLAS_HEIGHT;
    
    tr->atlas_x_offset += w + 1; 
    tr->glyph_loaded[char_code] = true;
}

bool text_renderer_update(TextRenderer* tr, Clip* clip) {
    if (clip->type != CLIP_TYPE_TEXT) return false;
    bool dirty = false;
    
    // Check Font change
    if (!tr->loaded_font_path || strcmp(tr->loaded_font_path, clip->text.font_path) != 0) {
        if (tr->face) FT_Done_Face(tr->face);
        
        // 尝试加载新字体
        if (FT_New_Face(tr->ft, clip->text.font_path, 0, &tr->face)) {
            fprintf(stderr, "ERROR::FREETYPE: Failed to load %s\n", clip->text.font_path);
            // 保持旧路径为空或之前状态，避免野指针
            if (tr->loaded_font_path) {
                free(tr->loaded_font_path);
                tr->loaded_font_path = NULL;
            }
            return false; 
        }
        
        // 加载成功，更新路径
        if (tr->loaded_font_path) free(tr->loaded_font_path);
        tr->loaded_font_path = strdup(clip->text.font_path);
        
        // Reset Atlas
        memset(tr->glyph_loaded, 0, sizeof(tr->glyph_loaded));
        tr->atlas_x_offset = 0;
        dirty = true;
    }
    
    // 只有当字体有效时才设置大小和加载字形
    if (tr->face) {
        FT_Set_Pixel_Sizes(tr->face, 0, clip->text.font_size);

        text_renderer_layout_text(tr, clip, &clip->text.cached_width, &clip->text.cached_height);
        dirty = true;
        
        const char* p = clip->text.content;
        while (*p) {
            load_glyph_to_atlas(tr, (u32)*p);
            p++;
        }
        
        // Measure
        float x = 0;
        float max_h = 0;
        p = clip->text.content;
        while (*p) {
            if ((u32)*p < MAX_GLYPHS) {
                GlyphInfo* g = &tr->glyphs[(u32)*p];
                x += g->advance;
                if (g->height > max_h) max_h = g->height;
            }
            p++;
        }
        
        if (clip->width != (u32)x || clip->height != (u32)max_h) {
            clip->width = (u32)x;
            clip->height = (u32)max_h;
            dirty = true;
        }
    }
    
    return dirty;
}

GlyphInfo* text_renderer_get_glyph(TextRenderer* tr, char c) {
    if ((u32)c >= MAX_GLYPHS) return &tr->glyphs['?']; // Fallback if possible
    return &tr->glyphs[(u32)c];
}

GLuint text_renderer_get_texture(TextRenderer* tr) {
    return tr->atlas_id;
}
void text_renderer_layout_text(TextRenderer* tr, Clip* clip, float* out_width, float* out_height) {
    if (!tr->face) {
        *out_width = *out_height = 0.0f;
        return;
    }

    float x = 0.0f;
    float max_h = 0.0f;
    const char* p = clip->text.content;

    while (*p) {
        u32 code = (u32)*p;
        load_glyph_to_atlas(tr, code);  // 确保字形已加载

        GlyphInfo* g = text_renderer_get_glyph(tr, *p);
        x += g->advance + clip->text.letter_spacing;  // 新增：应用字符间距
        if (g->height > max_h) max_h = g->height;

        p++;
    }

    *out_width = x;
    *out_height = max_h;
}