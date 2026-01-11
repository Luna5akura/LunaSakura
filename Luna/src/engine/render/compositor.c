// src/engine/render/compositor.c

#include "compositor.h"
#include "core/memory.h"
#include "core/vm/vm.h"

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
static Decoder* get_decoder_safe(Compositor* comp, ObjClip* clip) {
    for(int i=0; i<comp->decoder_count; i++) {
        if (decoder_get_clip_ref(comp->decoders[i]) == clip) return comp->decoders[i];
    }
    Decoder* dec = decoder_create(clip);
    if (comp->decoder_count >= comp->decoder_capacity) {
        int old = comp->decoder_capacity;
        comp->decoder_capacity = old < 8 ? 8 : old * 2;
        comp->decoders = GROW_ARRAY(comp->vm, Decoder*, comp->decoders, old, comp->decoder_capacity);
    }
    comp->decoders[comp->decoder_count++] = dec;
    return dec;
}

// Helper: Draw
static void draw_clip_rect(Compositor* comp, Decoder* dec, TimelineClip* tc) {
    GLuint ty = decoder_get_texture_y(dec);
    GLuint tu = decoder_get_texture_u(dec);
    GLuint tv = decoder_get_texture_v(dec);
    
    if (ty == 0) return;
    
    glUseProgram(comp->shader_program);
    glUniform1i(glGetUniformLocation(comp->shader_program, "tex_y"), 0);
    glUniform1i(glGetUniformLocation(comp->shader_program, "tex_u"), 1);
    glUniform1i(glGetUniformLocation(comp->shader_program, "tex_v"), 2);
    
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, ty);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, tu);
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, tv);
    
    float scale_x = tc->transform.scale_x;
    float scale_y = tc->transform.scale_y;
    float opacity = tc->transform.opacity;
    if (fabsf(scale_x) < 0.001f) scale_x = 1.0f;
    if (fabsf(scale_y) < 0.001f) scale_y = 1.0f;
    
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
    for(int i=0; i<comp->decoder_count; i++) decoder_destroy(comp->decoders[i]);
    if(comp->decoders) FREE_ARRAY(vm, Decoder*, comp->decoders, comp->decoder_capacity);
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
        if (!(track->flags & 1)) continue; // Check visible bit

        TimelineClip* tc = timeline_get_clip_at(track, time);
        if (!tc) continue;
        
        Decoder* dec = get_decoder_safe(comp, tc->media);
        double clip_time = (time - tc->timeline_start) + tc->source_in;
        
        if (decoder_update_video(dec, clip_time)) {
             draw_clip_rect(comp, dec, tc);
        }
        if (comp->mixer) {
            mixer_add_source(comp->mixer, dec, (float)tc->media->volume);
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