// src/engine/render/compositor.c

#include "compositor.h"
#include "engine/media/codec/decoder.h"
#include "core/memory.h"
#include "core/vm/vm.h"

typedef struct {
    Decoder* decoder;
    GLuint tex_y;
    GLuint tex_u;
    GLuint tex_v;
    // 记录纹理尺寸，如果视频分辨率改变需重新分配
    int width, height; 
} RenderSource;

// --- Shader Sources ---

// 1. Scene Shader (YUV -> RGB, Transforms)
const char* VS_SOURCE = "#version 330 core\n"
    "layout (location = 0) in vec2 aPos;\n"
    "layout (location = 1) in vec2 aTexCoord;\n"
    "out vec2 TexCoord;\n"
    "uniform mat4 u_projection;\n"
    "uniform mat4 u_model;\n"
    "void main() {\n"
    "   gl_Position = u_projection * u_model * vec4(aPos, 0.0, 1.0);\n"
    "   TexCoord = aTexCoord;\n"
    "}\n";

const char* FS_SOURCE_YUV = "#version 330 core\n"
    "out vec4 FragColor;\n"
    "in vec2 TexCoord;\n"
    "uniform sampler2D tex_y;\n"
    "uniform sampler2D tex_u;\n"
    "uniform sampler2D tex_v;\n"
    "uniform float u_opacity;\n"
    "void main() {\n"
    "   float y = texture(tex_y, TexCoord).r;\n"
    "   float u = texture(tex_u, TexCoord).r - 0.5;\n"
    "   float v = texture(tex_v, TexCoord).r - 0.5;\n"
    "   float r = y + 1.402 * v;\n"
    "   float g = y - 0.344136 * u - 0.714136 * v;\n"
    "   float b = y + 1.772 * u;\n"
    "   FragColor = vec4(r, g, b, u_opacity);\n"
    "}\n";

// 2. Screen Blit Shader (Texture -> Screen)
// [新增] 这是缺失的部分，用于 blit_to_screen
const char* VS_SCREEN = "#version 330 core\n"
    "layout (location = 0) in vec2 aPos;\n"
    "layout (location = 1) in vec2 aTexCoord;\n"
    "out vec2 TexCoord;\n"
    "void main() {\n"
    // 这里做简单的坐标映射，覆盖全屏
    "   gl_Position = vec4(aPos.x * 2.0 - 1.0, 1.0 - aPos.y * 2.0, 0.0, 1.0);\n"
    "   TexCoord = vec2(aTexCoord.x, 1.0 - aTexCoord.y);\n"
    "}\n";

const char* FS_SCREEN = "#version 330 core\n"
    "out vec4 FragColor;\n"
    "in vec2 TexCoord;\n"
    "uniform sampler2D screenTexture;\n"
    "void main() {\n"
    "   FragColor = texture(screenTexture, TexCoord);\n"
    "}\n";


// --- Math Helpers ---
typedef struct { float m[16]; } mat4;

static mat4 mat4_ortho(float left, float right, float bottom, float top, float near, float far) {
    mat4 res = {0}; 
    res.m[0]=2.0f/(right-left); 
    res.m[5]=2.0f/(top-bottom); 
    res.m[10]=-2.0f/(far-near); 
    res.m[12]=-(right+left)/(right-left); 
    res.m[13]=-(top+bottom)/(top-bottom); 
    res.m[14]=-(far+near)/(far-near); 
    res.m[15]=1.0f; 
    return res;
}

static mat4 mat4_translate_scale(float x, float y, float sx, float sy) {
    mat4 res = {0}; 
    res.m[0]=sx; res.m[5]=sy; res.m[10]=1.0f; res.m[15]=1.0f; 
    res.m[12]=x; res.m[13]=y; 
    return res;
}

static GLuint compile_shader(const char* src, GLenum type) {
    GLuint shader=glCreateShader(type); 
    glShaderSource(shader, 1, &src, NULL); 
    glCompileShader(shader); 
    
    // Check compilation errors (Optional but recommended)
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(shader, 512, NULL, infoLog);
        fprintf(stderr, "Shader Compilation Error: %s\n", infoLog);
    }
    return shader;
}

// Helper: Get Decoder
static RenderSource* get_source_safe(Compositor* comp, Clip* clip) {
    RenderSource* sources = (RenderSource*)comp->render_sources;
    
    // 1. 查找现有
    for(int i=0; i<comp->source_count; i++) {
        if (decoder_get_clip_ref(sources[i].decoder) == clip) {
            return &sources[i];
        }
    }
    
    // 2. 创建新的
    if (comp->source_count >= comp->source_capacity) {
        int old = comp->source_capacity;
        comp->source_capacity = old < 8 ? 8 : old * 2;
        // 注意：这里要按照 RenderSource 结构体大小扩容
        comp->render_sources = reallocate(comp->vm, comp->render_sources, 
            sizeof(RenderSource) * old, sizeof(RenderSource) * comp->source_capacity);
        sources = (RenderSource*)comp->render_sources;
    }
    
    RenderSource* src = &sources[comp->source_count++];
    memset(src, 0, sizeof(RenderSource));
    
    src->decoder = decoder_create(clip);
    
    // 3. 在 Compositor 侧创建 GL 纹理
    glGenTextures(1, &src->tex_y);
    glGenTextures(1, &src->tex_u);
    glGenTextures(1, &src->tex_v);
    
    // 配置纹理参数
    GLuint texs[] = {src->tex_y, src->tex_u, src->tex_v};
    for(int i=0; i<3; i++) {
        glBindTexture(GL_TEXTURE_2D, texs[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    
    return src;
}

// static Decoder* get_decoder_safe(Compositor* comp, Clip* clip) {
//     for(int i=0; i<comp->decoder_count; i++) {
//         if (decoder_get_clip_ref(comp->decoders[i]) == clip) return comp->decoders[i];
//     }
//     Decoder* dec = decoder_create(clip);
//     if (comp->decoder_count >= comp->decoder_capacity) {
//         int old = comp->decoder_capacity;
//         comp->decoder_capacity = old < 8 ? 8 : old * 2;
//         comp->decoders = GROW_ARRAY(comp->vm, Decoder*, comp->decoders, old, comp->decoder_capacity);
//     }
//     comp->decoders[comp->decoder_count++] = dec;
//     return dec;
// }

// Helper: Draw
static void draw_clip_rect(Compositor* comp, RenderSource* src, TimelineClip* tc) {
    if (src->tex_y == 0) return;
    
    glUseProgram(comp->shader_program);
    
    // 绑定纹理
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, src->tex_y);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, src->tex_u);
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, src->tex_v);
    
    glUniform1i(glGetUniformLocation(comp->shader_program, "tex_y"), 0);
    glUniform1i(glGetUniformLocation(comp->shader_program, "tex_u"), 1);
    glUniform1i(glGetUniformLocation(comp->shader_program, "tex_v"), 2);
    
    // ... (矩阵计算和绘制逻辑保持不变) ...
    float scale_x = tc->transform.scale_x;
    float scale_y = tc->transform.scale_y;
    float opacity = tc->transform.opacity;
    // (防止除零等保护逻辑)...
    float w = (float)tc->media->width * scale_x;
    float h = (float)tc->media->height * scale_y;
    
    mat4 model = mat4_translate_scale(tc->transform.x, tc->transform.y, w, h);
    glUniformMatrix4fv(glGetUniformLocation(comp->shader_program, "u_model"), 1, GL_FALSE, model.m);
    glUniform1f(glGetUniformLocation(comp->shader_program, "u_opacity"), opacity);
    
    glBindVertexArray(comp->vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

// === API Implementation ===

Compositor* compositor_create(VM* vm, Timeline* timeline) {
    Compositor* comp = ALLOCATE(vm, Compositor, 1);
    memset(comp, 0, sizeof(Compositor));
    comp->vm = vm;
    comp->timeline = timeline;
    comp->mixer = mixer_create(44100);

    GLuint vs = compile_shader(VS_SOURCE, GL_VERTEX_SHADER);
    GLuint fs = compile_shader(FS_SOURCE_YUV, GL_FRAGMENT_SHADER);
    comp->shader_program = glCreateProgram();
    glAttachShader(comp->shader_program, vs);
    glAttachShader(comp->shader_program, fs);
    glLinkProgram(comp->shader_program);
    glDeleteShader(vs); glDeleteShader(fs);
    
    // Quad for rendering (Full Rect)
    float quad[] = { 
        0,0, 0,0, 
        1,0, 1,0, 
        0,1, 0,1, 
        0,1, 0,1, 
        1,0, 1,0, 
        1,1, 1,1 
    };
    
    glGenVertexArrays(1, &comp->vao);
    glGenBuffers(1, &comp->vbo);
    glBindVertexArray(comp->vao);
    glBindBuffer(GL_ARRAY_BUFFER, comp->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    
    // Attr 0: Pos (2 floats), Attr 1: TexCoord (2 floats)
    // Stride = 4 * sizeof(float)
    glEnableVertexAttribArray(0); 
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1); 
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    
    // FBO Setup
    glGenFramebuffers(1, &comp->fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, comp->fbo);
    glGenTextures(1, &comp->output_texture);
    glBindTexture(GL_TEXTURE_2D, comp->output_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, timeline->width, timeline->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, comp->output_texture, 0);
    
    // Check FBO
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "Error: Framebuffer is not complete!\n");
    }
    
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    return comp;
}

void compositor_free(VM* vm, Compositor* comp) {
    if (!comp) return;
    if (comp->mixer) mixer_free(comp->mixer);
    
    // [修改] 释放 RenderSource 资源
    RenderSource* sources = (RenderSource*)comp->render_sources;
    for(int i=0; i<comp->source_count; i++) {
        decoder_destroy(sources[i].decoder);
        glDeleteTextures(1, &sources[i].tex_y);
        glDeleteTextures(1, &sources[i].tex_u);
        glDeleteTextures(1, &sources[i].tex_v);
    }
    if (comp->render_sources) {
        free(comp->render_sources); // 使用 free 或 reallocate(..., 0)
    }

    if(comp->cpu_output_buffer) free(comp->cpu_output_buffer);
    
    glDeleteProgram(comp->shader_program);
    glDeleteFramebuffers(1, &comp->fbo);
    glDeleteTextures(1, &comp->output_texture);
    glDeleteBuffers(1, &comp->vbo);
    glDeleteVertexArrays(1, &comp->vao);
    FREE(vm, Compositor, comp);
}

void compositor_render(Compositor* comp, double time) {
    glBindFramebuffer(GL_FRAMEBUFFER, comp->fbo);
    glViewport(0, 0, comp->timeline->width, comp->timeline->height);
    
    // ... (Clear Color 逻辑) ...
    u8 r = comp->timeline->background_color.r;
    u8 g = comp->timeline->background_color.g;
    u8 b = comp->timeline->background_color.b;
    glClearColor(r/255.f, g/255.f, b/255.f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    mat4 proj = mat4_ortho(0, comp->timeline->width, comp->timeline->height, 0, -1, 1);
    glUseProgram(comp->shader_program);
    glUniformMatrix4fv(glGetUniformLocation(comp->shader_program, "u_projection"), 1, GL_FALSE, proj.m);
    
    if (comp->mixer) mixer_begin_frame(comp->mixer);
    
    for(int i=0; i<comp->timeline->track_count; i++) {
        Track* track = &comp->timeline->tracks[i];
        if (!(track->flags & 1)) continue;

        TimelineClip* tc = timeline_get_clip_at(track, time);
        if (!tc) continue;
        
        // [修改] 获取 RenderSource
        RenderSource* src = get_source_safe(comp, tc->media);
        double clip_time = (time - tc->timeline_start) + tc->source_in;
        
        bool new_frame = decoder_update_video(src->decoder, clip_time);
        
        uint8_t* data[3];
        int linesize[3];
        int fw = 0, fh = 0; // [新增] 用于接收宽高
        
        // [修改] 调用新的 API
        if (decoder_get_video_data(src->decoder, data, linesize, &fw, &fh)) {
            
            if (new_frame) {
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                
                // Y Plane
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, src->tex_y);
                glPixelStorei(GL_UNPACK_ROW_LENGTH, linesize[0]);
                // [修改] 使用 fw 和 fh
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, fw, fh, 
                             0, GL_RED, GL_UNSIGNED_BYTE, data[0]);

                // U Plane
                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, src->tex_u);
                glPixelStorei(GL_UNPACK_ROW_LENGTH, linesize[1]);
                // [修改] 使用 fw/2 和 fh/2
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, fw/2, fh/2, 
                             0, GL_RED, GL_UNSIGNED_BYTE, data[1]);

                // V Plane
                glActiveTexture(GL_TEXTURE2);
                glBindTexture(GL_TEXTURE_2D, src->tex_v);
                glPixelStorei(GL_UNPACK_ROW_LENGTH, linesize[2]);
                // [修改] 使用 fw/2 和 fh/2
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, fw/2, fh/2, 
                             0, GL_RED, GL_UNSIGNED_BYTE, data[2]);
                             
                glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
            }
            
            // 绘制
            draw_clip_rect(comp, src, tc);
        }

        if (comp->mixer) {
            mixer_add_source(comp->mixer, src->decoder, (float)tc->media->volume);
        }
    }
    
    if (comp->mixer) mixer_end_frame(comp->mixer);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    comp->cpu_buffer_stale = true;
}


// [新增] 修复 undefined reference 错误
void compositor_blit_to_screen(Compositor* comp, i32 window_width, i32 window_height) {
    static GLuint blit_program = 0;
    
    // Lazy Compile Screen Shader
    if (blit_program == 0) {
        GLuint vs = compile_shader(VS_SCREEN, GL_VERTEX_SHADER);
        GLuint fs = compile_shader(FS_SCREEN, GL_FRAGMENT_SHADER);
        blit_program = glCreateProgram();
        glAttachShader(blit_program, vs);
        glAttachShader(blit_program, fs);
        glLinkProgram(blit_program);
        glDeleteShader(vs); glDeleteShader(fs);
    }
    
    // Draw to Default Framebuffer (Screen)
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, window_width, window_height);
    
    // Clear Screen (Black bars if aspect ratio differs)
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    
    glUseProgram(blit_program);
    
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, comp->output_texture);
    glUniform1i(glGetUniformLocation(blit_program, "screenTexture"), 0);
    
    // Reuse the full-screen quad VAO
    glBindVertexArray(comp->vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

void compositor_read_pixels(Compositor* comp, u8* out_buffer) {
    if (!out_buffer) return;

    glBindFramebuffer(GL_FRAMEBUFFER, comp->fbo);
    
    i32 width = comp->timeline->width;
    i32 height = comp->timeline->height;
    i32 stride = width * 4;
    
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, out_buffer);
    
    // Vertical Flip (OpenGL Origin Bottom-Left -> Image Origin Top-Left)
    u8* temp_row = malloc(stride);
    for (int y = 0; y < height / 2; y++) {
        u8* top_row = out_buffer + y * stride;
        u8* bot_row = out_buffer + (height - 1 - y) * stride;
        
        memcpy(temp_row, top_row, stride);
        memcpy(top_row, bot_row, stride);
        memcpy(bot_row, temp_row, stride);
    }
    free(temp_row);
    
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

u8* compositor_get_cpu_buffer(Compositor* comp) {
    if (comp->cpu_buffer_stale) {
        size_t size = comp->timeline->width * comp->timeline->height * 4;
        if (!comp->cpu_output_buffer) {
            comp->cpu_output_buffer = reallocate(comp->vm, NULL, 0, size);
        }
        compositor_read_pixels(comp, comp->cpu_output_buffer);
        comp->cpu_buffer_stale = false;
    }
    return comp->cpu_output_buffer;
}