// src/engine/render/compositor.c

#include "compositor.h"
#include "engine/media/codec/decoder.h"
#include "engine/effect/filter_base.h"
#include "engine/media/utils/image_loader.h"
#include "engine/render/gl_utils.h" // 使用新工具库
#include "engine/bridge/object.h"
#include "core/memory.h"
#include "core/vm/vm.h"
#include "engine/model/animation.h"  // 新增
#include <math.h> // For cosf, sinf
#include <stdlib.h>
typedef struct { float m[16]; } mat4;
static mat4 mat4_identity(void) {
    mat4 res = {0};
    res.m[0] = 1.0f; res.m[5] = 1.0f; res.m[10] = 1.0f; res.m[15] = 1.0f;
    return res;
}
static mat4 mat4_mult(mat4 a, mat4 b) {
    mat4 res = {0};
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            for (int k = 0; k < 4; k++) {
                res.m[i * 4 + j] += a.m[i * 4 + k] * b.m[k * 4 + j];
            }
        }
    }
    return res;
}
static mat4 mat4_translate(float x, float y) {
    mat4 res = mat4_identity();
    res.m[12] = x;
    res.m[13] = y;
    return res;
}
static mat4 mat4_scale(float sx, float sy) {
    mat4 res = mat4_identity();
    res.m[0] = sx;
    res.m[5] = sy;
    return res;
}
static mat4 mat4_rotate(float angle_deg) {
    float rad = angle_deg * 3.141592653589793f / 180.0f;
    float c = cosf(rad);
    float s = sinf(rad);
    mat4 res = mat4_identity();
    res.m[0] = c; res.m[1] = -s;
    res.m[4] = s; res.m[5] = c;
    return res;
}
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
const char* VS_SOURCE = "#version 330 core\n"
    "layout (location = 0) in vec2 aPos;\n"
    "layout (location = 1) in vec2 aTexCoord;\n"
    "out vec2 TexCoord;\n"
    "uniform mat4 u_projection;\n"
    "uniform mat4 u_model;\n"
    "void main() {\n"
    " gl_Position = u_projection * u_model * vec4(aPos, 0.0, 1.0);\n"
    " TexCoord = aTexCoord;\n"
    "}\n";
const char* FS_SOURCE_YUV = "#version 330 core\n"
    "out vec4 FragColor;\n"
    "in vec2 TexCoord;\n"
    "uniform sampler2D tex_y;\n"
    "uniform sampler2D tex_u;\n"
    "uniform sampler2D tex_v;\n"
    "uniform float u_opacity;\n"
    "void main() {\n"
    " float y = texture(tex_y, TexCoord).r;\n"
    " float u = texture(tex_u, TexCoord).r - 0.5;\n"
    " float v = texture(tex_v, TexCoord).r - 0.5;\n"
    " float r = y + 1.402 * v;\n"
    " float g = y - 0.344136 * u - 0.714136 * v;\n"
    " float b = y + 1.772 * u;\n"
    " FragColor = vec4(r, g, b, u_opacity);\n"
    "}\n";
const char* FS_SOURCE_RGBA = "#version 330 core\n"
    "out vec4 FragColor;\n"
    "in vec2 TexCoord;\n"
    "uniform sampler2D tex_rgba;\n"
    "uniform float u_opacity;\n"
    "void main() {\n"
    " vec4 color = texture(tex_rgba, TexCoord);\n"
    " FragColor = vec4(color.rgb, color.a * u_opacity);\n"
    "}\n";
const char* FS_SOURCE_COLOR = "#version 330 core\n"
    "out vec4 FragColor;\n"
    "uniform vec4 u_color;\n"
    "void main() {\n"
    " FragColor = u_color;\n"
    "}\n";
const char* FS_SOURCE_COPY = "#version 330 core\n"
    "out vec4 FragColor;\n"
    "in vec2 TexCoord;\n"
    "uniform sampler2D u_texture;\n"
    "uniform float u_opacity;\n"
    "void main() {\n"
    " vec4 color = texture(u_texture, TexCoord);\n"
    " FragColor = vec4(color.rgb, color.a * u_opacity);\n"
    "}\n";
const char* FS_SOURCE_TINT = "#version 330 core\n"
    "out vec4 FragColor;\n"
    "in vec2 TexCoord;\n"
    "uniform sampler2D u_texture;\n"
    "uniform vec4 u_color;\n"
    "uniform float u_amount;\n"
    "void main() {\n"
    " vec4 src = texture(u_texture, TexCoord);\n"
    " float luminance = dot(src.rgb, vec3(0.299, 0.587, 0.114));\n"
    " vec3 tinted = luminance * u_color.rgb;\n"
    " FragColor = vec4(mix(src.rgb, tinted, u_amount), src.a);\n"
    "}\n";
const char* FS_SOURCE_FILL = "#version 330 core\n"
    "out vec4 FragColor;\n"
    "in vec2 TexCoord;\n"
    "uniform sampler2D u_texture;\n"
    "uniform vec4 u_color;\n"
    "uniform float u_amount;\n"
    "void main() {\n"
    " vec4 src = texture(u_texture, TexCoord);\n"
    " vec4 filled = vec4(u_color.rgb, src.a * u_color.a);\n"
    " FragColor = vec4(mix(src.rgb, filled.rgb, u_amount), mix(src.a, filled.a, u_amount));\n"
    "}\n";
const char* FS_SOURCE_GRADIENT_RAMP = "#version 330 core\n"
    "out vec4 FragColor;\n"
    "in vec2 TexCoord;\n"
    "uniform sampler2D u_texture;\n"
    "uniform vec2 u_start;\n"
    "uniform vec2 u_end;\n"
    "uniform vec4 u_start_color;\n"
    "uniform vec4 u_end_color;\n"
    "uniform float u_blend;\n"
    "void main() {\n"
    " vec4 src = texture(u_texture, TexCoord);\n"
    " vec2 dir = u_end - u_start;\n"
    " float len2 = max(dot(dir, dir), 0.00001);\n"
    " float t = clamp(dot(TexCoord - u_start, dir) / len2, 0.0, 1.0);\n"
    " vec4 grad = mix(u_start_color, u_end_color, t);\n"
    " FragColor = vec4(mix(src.rgb, grad.rgb, u_blend * grad.a), max(src.a, grad.a * u_blend));\n"
    "}\n";
const char* FS_SOURCE_GRID = "#version 330 core\n"
    "out vec4 FragColor;\n"
    "in vec2 TexCoord;\n"
    "uniform sampler2D u_texture;\n"
    "uniform vec2 u_size;\n"
    "uniform float u_line_width;\n"
    "uniform vec4 u_color;\n"
    "uniform float u_opacity;\n"
    "void main() {\n"
    " vec4 src = texture(u_texture, TexCoord);\n"
    " vec2 cell = TexCoord * u_size;\n"
    " vec2 frac_part = fract(cell);\n"
    " float line = step(frac_part.x, u_line_width) + step(frac_part.y, u_line_width);\n"
    " line = clamp(line, 0.0, 1.0);\n"
    " vec4 grid = vec4(u_color.rgb, u_color.a * u_opacity * line);\n"
    " FragColor = vec4(mix(src.rgb, grid.rgb, grid.a), max(src.a, grid.a));\n"
    "}\n";
const char* FS_SOURCE_MOSAIC = "#version 330 core\n"
    "out vec4 FragColor;\n"
    "in vec2 TexCoord;\n"
    "uniform sampler2D u_texture;\n"
    "uniform float u_block_size;\n"
    "uniform vec2 u_resolution;\n"
    "uniform bool u_sharp_colors;\n"
    "void main() {\n"
    " float block = max(u_block_size, 1.0);\n"
    " vec2 frag_pixel = gl_FragCoord.xy - vec2(0.5);\n"
    " vec2 block_index = floor(frag_pixel / block);\n"
    " vec2 block_origin = block_index * block;\n"
    " vec2 sample_pixel_f = clamp(floor(block_origin + vec2(block * 0.5)), vec2(0.0), u_resolution - vec2(1.0));\n"
    " ivec2 sample_pixel = ivec2(sample_pixel_f);\n"
    " vec2 sample_uv = (sample_pixel_f + vec2(0.5)) / u_resolution;\n"
    " vec4 color = u_sharp_colors\n"
    "   ? texelFetch(u_texture, sample_pixel, 0)\n"
    "   : texture(u_texture, sample_uv);\n"
    " if (u_sharp_colors) {\n"
    "   color.rgb = floor(color.rgb * 255.0 + 0.5) / 255.0;\n"
    "   color.a = floor(color.a * 255.0 + 0.5) / 255.0;\n"
    " }\n"
    " FragColor = color;\n"
    "}\n";
const char* FS_SOURCE_BRIGHTNESS_CONTRAST = "#version 330 core\n"
    "out vec4 FragColor;\n"
    "in vec2 TexCoord;\n"
    "uniform sampler2D u_texture;\n"
    "uniform float u_brightness;\n"
    "uniform float u_contrast;\n"
    "void main() {\n"
    " vec4 src = texture(u_texture, TexCoord);\n"
    " vec3 color = src.rgb + vec3(u_brightness);\n"
    " color = (color - 0.5) * u_contrast + 0.5;\n"
    " FragColor = vec4(color, src.a);\n"
    "}\n";
const char* FS_SOURCE_BLUR = "#version 330 core\n"
    "out vec4 FragColor;\n"
    "in vec2 TexCoord;\n"
    "uniform sampler2D u_texture;\n"
    "uniform vec2 u_texel_size;\n"
    "uniform float u_radius;\n"
    "void main() {\n"
    " vec4 src = texture(u_texture, TexCoord);\n"
    " if (u_radius <= 0.001) { FragColor = src; return; }\n"
    " vec4 sum = src * 4.0;\n"
    " sum += texture(u_texture, TexCoord + vec2(u_texel_size.x * u_radius, 0.0));\n"
    " sum += texture(u_texture, TexCoord - vec2(u_texel_size.x * u_radius, 0.0));\n"
    " sum += texture(u_texture, TexCoord + vec2(0.0, u_texel_size.y * u_radius));\n"
    " sum += texture(u_texture, TexCoord - vec2(0.0, u_texel_size.y * u_radius));\n"
    " FragColor = sum / 8.0;\n"
    "}\n";
const char* FS_SOURCE_FRACTAL_NOISE = "#version 330 core\n"
    "out vec4 FragColor;\n"
    "in vec2 TexCoord;\n"
    "uniform sampler2D u_texture;\n"
    "uniform vec2 u_resolution;\n"
    "uniform float u_scale;\n"
    "uniform float u_evolution;\n"
    "uniform float u_contrast;\n"
    "uniform float u_brightness;\n"
    "uniform float u_octaves;\n"
    "uniform float u_amount;\n"
    "uniform vec2 u_offset;\n"
    "uniform bool u_invert;\n"
    "mat2 rot2(float a) {\n"
    " float c = cos(a);\n"
    " float s = sin(a);\n"
    " return mat2(c, -s, s, c);\n"
    "}\n"
    "float hash3(vec3 p) {\n"
    " return fract(sin(dot(p, vec3(127.1, 311.7, 191.999))) * 43758.5453123);\n"
    "}\n"
    "float noise3(vec3 p) {\n"
    " vec3 i = floor(p);\n"
    " vec3 f = fract(p);\n"
    " vec3 u = f * f * (3.0 - 2.0 * f);\n"
    " float n000 = hash3(i + vec3(0.0, 0.0, 0.0));\n"
    " float n100 = hash3(i + vec3(1.0, 0.0, 0.0));\n"
    " float n010 = hash3(i + vec3(0.0, 1.0, 0.0));\n"
    " float n110 = hash3(i + vec3(1.0, 1.0, 0.0));\n"
    " float n001 = hash3(i + vec3(0.0, 0.0, 1.0));\n"
    " float n101 = hash3(i + vec3(1.0, 0.0, 1.0));\n"
    " float n011 = hash3(i + vec3(0.0, 1.0, 1.0));\n"
    " float n111 = hash3(i + vec3(1.0, 1.0, 1.0));\n"
    " float nx00 = mix(n000, n100, u.x);\n"
    " float nx10 = mix(n010, n110, u.x);\n"
    " float nx01 = mix(n001, n101, u.x);\n"
    " float nx11 = mix(n011, n111, u.x);\n"
    " float nxy0 = mix(nx00, nx10, u.y);\n"
    " float nxy1 = mix(nx01, nx11, u.y);\n"
    " return mix(nxy0, nxy1, u.z);\n"
    "}\n"
    "float fbm(vec2 p, float octaves, float evolution) {\n"
    " float value = 0.0;\n"
    " float amplitude = 0.5;\n"
    " float total = 0.0;\n"
    " float z = evolution * 0.18;\n"
    " for (int i = 0; i < 6; i++) {\n"
    "   if (float(i) >= octaves) break;\n"
    "   value += noise3(vec3(p, z + float(i) * 3.17)) * amplitude;\n"
    "   total += amplitude;\n"
    "   p = rot2(0.35 + float(i) * 0.17) * (p * 2.0) + vec2(13.1, 7.9);\n"
    "   z = z * 1.91 + 2.37;\n"
    "   amplitude *= 0.5;\n"
    " }\n"
    " return total > 0.0 ? value / total : noise3(vec3(p, z));\n"
    "}\n"
    "void main() {\n"
    " vec4 src = texture(u_texture, TexCoord);\n"
    " float scale = max(u_scale, 1.0);\n"
    " float octaves = clamp(floor(u_octaves + 0.5), 1.0, 6.0);\n"
    " vec2 p = ((TexCoord * u_resolution) + u_offset) / scale;\n"
    " float n = fbm(p, octaves, u_evolution);\n"
    " n = (n - 0.5) * max(u_contrast, 0.0) + 0.5 + u_brightness;\n"
    " n = clamp(n, 0.0, 1.0);\n"
    " if (u_invert) n = 1.0 - n;\n"
    " vec3 noise_color = vec3(n);\n"
    " FragColor = vec4(mix(src.rgb, noise_color, clamp(u_amount, 0.0, 1.0)), src.a);\n"
    "}\n";
const char* FS_SOURCE_DISPLACEMENT_MAP = "#version 330 core\n"
    "out vec4 FragColor;\n"
    "in vec2 TexCoord;\n"
    "uniform sampler2D u_texture;\n"
    "uniform sampler2D u_map_texture;\n"
    "uniform vec2 u_resolution;\n"
    "uniform float u_scale_x;\n"
    "uniform float u_scale_y;\n"
    "uniform float u_amount;\n"
    "uniform vec2 u_offset;\n"
    "uniform bool u_use_luma;\n"
    "void main() {\n"
    " vec2 map_uv = clamp(TexCoord + u_offset / u_resolution, vec2(0.0), vec2(1.0));\n"
    " vec4 map = texture(u_map_texture, map_uv);\n"
    " vec2 disp;\n"
    " if (u_use_luma) {\n"
    "   float l = dot(map.rgb, vec3(0.299, 0.587, 0.114)) - 0.5;\n"
    "   disp = vec2(l * u_scale_x, l * u_scale_y);\n"
    " } else {\n"
    "   disp = vec2(map.r - 0.5, map.g - 0.5) * vec2(u_scale_x, u_scale_y) * 2.0;\n"
    " }\n"
    " vec2 uv = clamp(TexCoord + (disp * u_amount) / u_resolution, vec2(0.0), vec2(1.0));\n"
    " FragColor = texture(u_texture, uv);\n"
    "}\n";
const char* FS_SOURCE_POSTERIZE = "#version 330 core\n"
    "out vec4 FragColor;\n"
    "in vec2 TexCoord;\n"
    "uniform sampler2D u_texture;\n"
    "uniform float u_levels;\n"
    "uniform float u_amount;\n"
    "void main() {\n"
    " vec4 src = texture(u_texture, TexCoord);\n"
    " float levels = max(floor(u_levels + 0.5), 2.0);\n"
    " vec3 post = floor(src.rgb * (levels - 1.0) + 0.5) / (levels - 1.0);\n"
    " FragColor = vec4(mix(src.rgb, post, clamp(u_amount, 0.0, 1.0)), src.a);\n"
    "}\n";
const char* FS_SOURCE_ADJUSTMENT_COMPOSITE = "#version 330 core\n"
    "out vec4 FragColor;\n"
    "in vec2 TexCoord;\n"
    "uniform sampler2D u_base_texture;\n"
    "uniform sampler2D u_adjusted_texture;\n"
    "uniform sampler2D u_mask_texture;\n"
    "uniform vec2 u_region_center;\n"
    "uniform vec2 u_region_half_size;\n"
    "uniform vec2 u_canvas_size;\n"
    "uniform float u_opacity;\n"
    "uniform float u_feather;\n"
    "uniform float u_region_rotation;\n"
    "uniform int u_blend_mode;\n"
    "uniform int u_mask_mode;\n"
    "uniform bool u_whole_frame;\n"
    "uniform bool u_has_mask;\n"
    "uniform bool u_mask_invert;\n"
    "float overlay_channel(float b, float s) {\n"
    " return b < 0.5 ? (2.0 * b * s) : (1.0 - 2.0 * (1.0 - b) * (1.0 - s));\n"
    "}\n"
    "vec3 blend_color(vec3 base, vec3 src, int mode) {\n"
    " if (mode == 1) return base + src;\n"
    " if (mode == 2) return base * src;\n"
    " if (mode == 3) return 1.0 - (1.0 - base) * (1.0 - src);\n"
    " if (mode == 4) return vec3(overlay_channel(base.r, src.r), overlay_channel(base.g, src.g), overlay_channel(base.b, src.b));\n"
    " return src;\n"
    "}\n"
    "void main() {\n"
    " vec4 base = texture(u_base_texture, TexCoord);\n"
    " vec4 adjusted = texture(u_adjusted_texture, TexCoord);\n"
    " float region_alpha = 1.0;\n"
    " if (!u_whole_frame) {\n"
    "   vec2 pixel = TexCoord * u_canvas_size;\n"
    "   float rad = radians(-u_region_rotation);\n"
    "   mat2 rot = mat2(cos(rad), -sin(rad), sin(rad), cos(rad));\n"
    "   vec2 local = rot * (pixel - u_region_center);\n"
    "   vec2 edge = u_region_half_size - abs(local);\n"
    "   float min_edge = min(edge.x, edge.y);\n"
    "   if (u_feather > 0.001) {\n"
    "     region_alpha = clamp(min_edge / u_feather, 0.0, 1.0);\n"
    "   } else {\n"
    "     region_alpha = min_edge >= 0.0 ? 1.0 : 0.0;\n"
    "   }\n"
    " }\n"
    " float mask_alpha = 1.0;\n"
    " if (u_has_mask) {\n"
    "   vec4 mask = texture(u_mask_texture, TexCoord);\n"
    "   mask_alpha = u_mask_mode == 1 ? dot(mask.rgb, vec3(0.299, 0.587, 0.114)) : mask.a;\n"
    "   if (u_mask_invert) mask_alpha = 1.0 - mask_alpha;\n"
    " }\n"
    " float alpha = clamp(adjusted.a * u_opacity * region_alpha * mask_alpha, 0.0, 1.0);\n"
    " vec3 blended = blend_color(base.rgb, adjusted.rgb, u_blend_mode);\n"
    " vec3 out_rgb = mix(base.rgb, clamp(blended, 0.0, 1.0), alpha);\n"
    " FragColor = vec4(out_rgb, max(base.a, alpha));\n"
    "}\n";
const char* VS_SCREEN = "#version 330 core\n"
    "layout (location = 0) in vec2 aPos;\n"
    "layout (location = 1) in vec2 aTexCoord;\n"
    "out vec2 TexCoord;\n"
    "void main() {\n"
    " gl_Position = vec4(aPos.x * 2.0 - 1.0, 1.0 - aPos.y * 2.0, 0.0, 1.0);\n"
    " TexCoord = vec2(aTexCoord.x, 1.0 - aTexCoord.y);\n"
    "}\n";
const char* FS_SCREEN = "#version 330 core\n"
    "out vec4 FragColor;\n"
    "in vec2 TexCoord;\n"
    "uniform sampler2D screenTexture;\n"
    "void main() {\n"
    " FragColor = texture(screenTexture, TexCoord);\n"
    "}\n";
const char* FS_SOURCE_TEXT = "#version 330 core\n"
    "in vec2 TexCoord;\n"
    "out vec4 FragColor;\n"
    "uniform sampler2D textAtlas;\n"
    "uniform vec4 u_color;\n"
    "uniform vec4 u_uv_rect;\n" // xy=top-left, zw=bottom-right
    "void main() {\n"
    " vec2 uv = mix(u_uv_rect.xy, u_uv_rect.zw, TexCoord);\n"
    " float alpha = texture(textAtlas, uv).r;\n"
    " FragColor = vec4(u_color.rgb, alpha * u_color.a);\n"
    "}\n";
typedef enum {
    RENDER_SOURCE_MEDIA,
    RENDER_SOURCE_IMAGE,
    RENDER_SOURCE_NESTED
} RenderSourceType;

typedef struct {
    RenderSourceType type;
    Clip* clip_ref;
    Timeline* nested_timeline;
    Compositor* nested_compositor;
    Decoder* decoder;
    GLuint tex_y;
    GLuint tex_u;
    GLuint tex_v;
    GLuint tex_rgba;
    int width, height;
    bool image_loaded;
} RenderSource;

static void init_shader_uniform_cache(ShaderUniformCache* cache) {
    memset(cache, -1, sizeof(*cache));
}

static void init_compositor_uniform_caches(Compositor* comp) {
    init_shader_uniform_cache(&comp->yuv_uniforms);
    init_shader_uniform_cache(&comp->image_uniforms);
    init_shader_uniform_cache(&comp->color_uniforms);
    init_shader_uniform_cache(&comp->text_uniforms);
    init_shader_uniform_cache(&comp->copy_uniforms);
    init_shader_uniform_cache(&comp->tint_uniforms);
    init_shader_uniform_cache(&comp->fill_uniforms);
    init_shader_uniform_cache(&comp->gradient_uniforms);
    init_shader_uniform_cache(&comp->grid_uniforms);
    init_shader_uniform_cache(&comp->mosaic_uniforms);
    init_shader_uniform_cache(&comp->brightness_uniforms);
    init_shader_uniform_cache(&comp->blur_uniforms);
    init_shader_uniform_cache(&comp->fractal_uniforms);
    init_shader_uniform_cache(&comp->displacement_uniforms);
    init_shader_uniform_cache(&comp->posterize_uniforms);
    comp->adjustment_u_base_texture = -1;
    comp->fractal_u_amount = -1;
    comp->fractal_u_offset = -1;
    comp->fractal_u_invert = -1;
    comp->displacement_u_offset = -1;
    comp->displacement_u_use_luma = -1;
    comp->adjustment_u_adjusted_texture = -1;
    comp->adjustment_u_mask_texture = -1;
    comp->adjustment_u_region_center = -1;
    comp->adjustment_u_region_half_size = -1;
    comp->adjustment_u_canvas_size = -1;
    comp->adjustment_u_opacity = -1;
    comp->adjustment_u_feather = -1;
    comp->adjustment_u_region_rotation = -1;
    comp->adjustment_u_blend_mode = -1;
    comp->adjustment_u_mask_mode = -1;
    comp->adjustment_u_whole_frame = -1;
    comp->adjustment_u_has_mask = -1;
    comp->adjustment_u_mask_invert = -1;
}

static void cache_uniform_locations(Compositor* comp) {
    comp->yuv_uniforms.u_projection = glGetUniformLocation(comp->shader_program, "u_projection");
    comp->yuv_uniforms.u_model = glGetUniformLocation(comp->shader_program, "u_model");
    comp->yuv_uniforms.u_opacity = glGetUniformLocation(comp->shader_program, "u_opacity");
    comp->yuv_uniforms.sampler0 = glGetUniformLocation(comp->shader_program, "tex_y");
    comp->yuv_uniforms.sampler1 = glGetUniformLocation(comp->shader_program, "tex_u");
    comp->yuv_uniforms.sampler2 = glGetUniformLocation(comp->shader_program, "tex_v");

    comp->image_uniforms.u_projection = glGetUniformLocation(comp->image_shader_program, "u_projection");
    comp->image_uniforms.u_model = glGetUniformLocation(comp->image_shader_program, "u_model");
    comp->image_uniforms.u_opacity = glGetUniformLocation(comp->image_shader_program, "u_opacity");
    comp->image_uniforms.sampler0 = glGetUniformLocation(comp->image_shader_program, "tex_rgba");

    comp->color_uniforms.u_projection = glGetUniformLocation(comp->color_shader_program, "u_projection");
    comp->color_uniforms.u_model = glGetUniformLocation(comp->color_shader_program, "u_model");
    comp->color_uniforms.u_color = glGetUniformLocation(comp->color_shader_program, "u_color");

    comp->text_uniforms.u_projection = glGetUniformLocation(comp->text_shader_program, "u_projection");
    comp->text_uniforms.u_model = glGetUniformLocation(comp->text_shader_program, "u_model");
    comp->text_uniforms.u_uv_rect = glGetUniformLocation(comp->text_shader_program, "u_uv_rect");
    comp->text_uniforms.u_color = glGetUniformLocation(comp->text_shader_program, "u_color");
    comp->text_uniforms.sampler0 = glGetUniformLocation(comp->text_shader_program, "textAtlas");

    comp->copy_uniforms.sampler0 = glGetUniformLocation(comp->copy_shader_program, "u_texture");
    comp->copy_uniforms.u_opacity = glGetUniformLocation(comp->copy_shader_program, "u_opacity");
    comp->tint_uniforms.sampler0 = glGetUniformLocation(comp->tint_shader_program, "u_texture");
    comp->tint_uniforms.u_opacity = glGetUniformLocation(comp->tint_shader_program, "u_amount");
    comp->tint_uniforms.u_color = glGetUniformLocation(comp->tint_shader_program, "u_color");
    comp->fill_uniforms.sampler0 = glGetUniformLocation(comp->fill_shader_program, "u_texture");
    comp->fill_uniforms.u_opacity = glGetUniformLocation(comp->fill_shader_program, "u_amount");
    comp->fill_uniforms.u_color = glGetUniformLocation(comp->fill_shader_program, "u_color");
    comp->brightness_uniforms.sampler0 = glGetUniformLocation(comp->brightness_contrast_shader_program, "u_texture");
    comp->brightness_uniforms.u_projection = glGetUniformLocation(comp->brightness_contrast_shader_program, "u_brightness");
    comp->brightness_uniforms.u_model = glGetUniformLocation(comp->brightness_contrast_shader_program, "u_contrast");
    comp->blur_uniforms.sampler0 = glGetUniformLocation(comp->blur_shader_program, "u_texture");
    comp->blur_uniforms.u_projection = glGetUniformLocation(comp->blur_shader_program, "u_texel_size");
    comp->blur_uniforms.u_opacity = glGetUniformLocation(comp->blur_shader_program, "u_radius");
    comp->mosaic_uniforms.sampler0 = glGetUniformLocation(comp->mosaic_shader_program, "u_texture");
    comp->mosaic_uniforms.u_projection = glGetUniformLocation(comp->mosaic_shader_program, "u_block_size");
    comp->mosaic_uniforms.u_model = glGetUniformLocation(comp->mosaic_shader_program, "u_resolution");
    comp->mosaic_uniforms.u_opacity = glGetUniformLocation(comp->mosaic_shader_program, "u_sharp_colors");
    comp->grid_uniforms.sampler0 = glGetUniformLocation(comp->grid_shader_program, "u_texture");
    comp->grid_uniforms.u_projection = glGetUniformLocation(comp->grid_shader_program, "u_size");
    comp->grid_uniforms.u_model = glGetUniformLocation(comp->grid_shader_program, "u_line_width");
    comp->grid_uniforms.u_opacity = glGetUniformLocation(comp->grid_shader_program, "u_opacity");
    comp->grid_uniforms.u_color = glGetUniformLocation(comp->grid_shader_program, "u_color");
    comp->gradient_uniforms.sampler0 = glGetUniformLocation(comp->gradient_ramp_shader_program, "u_texture");
    comp->gradient_uniforms.u_projection = glGetUniformLocation(comp->gradient_ramp_shader_program, "u_start");
    comp->gradient_uniforms.u_model = glGetUniformLocation(comp->gradient_ramp_shader_program, "u_end");
    comp->gradient_uniforms.u_color = glGetUniformLocation(comp->gradient_ramp_shader_program, "u_start_color");
    comp->gradient_uniforms.sampler1 = glGetUniformLocation(comp->gradient_ramp_shader_program, "u_end_color");
    comp->gradient_uniforms.u_opacity = glGetUniformLocation(comp->gradient_ramp_shader_program, "u_blend");
    comp->fractal_uniforms.sampler0 = glGetUniformLocation(comp->fractal_noise_shader_program, "u_texture");
    comp->fractal_uniforms.u_projection = glGetUniformLocation(comp->fractal_noise_shader_program, "u_resolution");
    comp->fractal_uniforms.u_model = glGetUniformLocation(comp->fractal_noise_shader_program, "u_scale");
    comp->fractal_uniforms.u_opacity = glGetUniformLocation(comp->fractal_noise_shader_program, "u_evolution");
    comp->fractal_uniforms.u_color = glGetUniformLocation(comp->fractal_noise_shader_program, "u_contrast");
    comp->fractal_uniforms.sampler1 = glGetUniformLocation(comp->fractal_noise_shader_program, "u_brightness");
    comp->fractal_uniforms.sampler2 = glGetUniformLocation(comp->fractal_noise_shader_program, "u_octaves");
    comp->fractal_u_amount = glGetUniformLocation(comp->fractal_noise_shader_program, "u_amount");
    comp->fractal_u_offset = glGetUniformLocation(comp->fractal_noise_shader_program, "u_offset");
    comp->fractal_u_invert = glGetUniformLocation(comp->fractal_noise_shader_program, "u_invert");
    comp->displacement_uniforms.sampler0 = glGetUniformLocation(comp->displacement_map_shader_program, "u_texture");
    comp->displacement_uniforms.sampler1 = glGetUniformLocation(comp->displacement_map_shader_program, "u_map_texture");
    comp->displacement_uniforms.u_projection = glGetUniformLocation(comp->displacement_map_shader_program, "u_resolution");
    comp->displacement_uniforms.u_model = glGetUniformLocation(comp->displacement_map_shader_program, "u_scale_x");
    comp->displacement_uniforms.u_opacity = glGetUniformLocation(comp->displacement_map_shader_program, "u_scale_y");
    comp->displacement_uniforms.u_color = glGetUniformLocation(comp->displacement_map_shader_program, "u_amount");
    comp->displacement_u_offset = glGetUniformLocation(comp->displacement_map_shader_program, "u_offset");
    comp->displacement_u_use_luma = glGetUniformLocation(comp->displacement_map_shader_program, "u_use_luma");
    comp->posterize_uniforms.sampler0 = glGetUniformLocation(comp->posterize_shader_program, "u_texture");
    comp->posterize_uniforms.u_projection = glGetUniformLocation(comp->posterize_shader_program, "u_levels");
    comp->posterize_uniforms.u_opacity = glGetUniformLocation(comp->posterize_shader_program, "u_amount");

    comp->adjustment_u_base_texture = glGetUniformLocation(comp->adjustment_composite_shader_program, "u_base_texture");
    comp->adjustment_u_adjusted_texture = glGetUniformLocation(comp->adjustment_composite_shader_program, "u_adjusted_texture");
    comp->adjustment_u_mask_texture = glGetUniformLocation(comp->adjustment_composite_shader_program, "u_mask_texture");
    comp->adjustment_u_region_center = glGetUniformLocation(comp->adjustment_composite_shader_program, "u_region_center");
    comp->adjustment_u_region_half_size = glGetUniformLocation(comp->adjustment_composite_shader_program, "u_region_half_size");
    comp->adjustment_u_canvas_size = glGetUniformLocation(comp->adjustment_composite_shader_program, "u_canvas_size");
    comp->adjustment_u_opacity = glGetUniformLocation(comp->adjustment_composite_shader_program, "u_opacity");
    comp->adjustment_u_feather = glGetUniformLocation(comp->adjustment_composite_shader_program, "u_feather");
    comp->adjustment_u_region_rotation = glGetUniformLocation(comp->adjustment_composite_shader_program, "u_region_rotation");
    comp->adjustment_u_blend_mode = glGetUniformLocation(comp->adjustment_composite_shader_program, "u_blend_mode");
    comp->adjustment_u_mask_mode = glGetUniformLocation(comp->adjustment_composite_shader_program, "u_mask_mode");
    comp->adjustment_u_whole_frame = glGetUniformLocation(comp->adjustment_composite_shader_program, "u_whole_frame");
    comp->adjustment_u_has_mask = glGetUniformLocation(comp->adjustment_composite_shader_program, "u_has_mask");
    comp->adjustment_u_mask_invert = glGetUniformLocation(comp->adjustment_composite_shader_program, "u_mask_invert");
}
static void setup_texture_params(GLuint tex) {
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}
static RenderSource* get_source_safe(Compositor* comp, Clip* clip) {
    RenderSource* sources = (RenderSource*)comp->render_sources;
    for(int i=0; i<comp->source_count; i++) {
        if (sources[i].type == RENDER_SOURCE_MEDIA &&
            sources[i].decoder &&
            decoder_get_clip_ref(sources[i].decoder) == clip) {
            return &sources[i];
        }
        if (sources[i].type == RENDER_SOURCE_IMAGE && sources[i].clip_ref == clip) {
            return &sources[i];
        }
    }
    if (comp->source_count >= comp->source_capacity) {
        int old_capacity = comp->source_capacity;
        comp->source_capacity = MEM_GROW_CAPACITY(old_capacity);
        comp->render_sources = GROW_ARRAY(comp->vm, RenderSource,
            comp->render_sources, old_capacity, comp->source_capacity);
        sources = (RenderSource*)comp->render_sources;
    }
    RenderSource* src = &sources[comp->source_count++];
    memset(src, 0, sizeof(RenderSource));
    src->clip_ref = clip;
    if (clip->type == CLIP_TYPE_IMAGE) {
        uint8_t* pixels = NULL;
        int width = 0;
        int height = 0;

        src->type = RENDER_SOURCE_IMAGE;
        glGenTextures(1, &src->tex_rgba);
        setup_texture_params(src->tex_rgba);
        if (image_load_rgba(clip->path, &pixels, &width, &height)) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, src->tex_rgba);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
            src->width = width;
            src->height = height;
            src->image_loaded = true;
            free(pixels);
        }
        return src;
    }
    src->type = RENDER_SOURCE_MEDIA;
    src->decoder = decoder_create(clip);
    glGenTextures(1, &src->tex_y);
    glGenTextures(1, &src->tex_u);
    glGenTextures(1, &src->tex_v);
    setup_texture_params(src->tex_y);
    setup_texture_params(src->tex_u);
    setup_texture_params(src->tex_v);
    return src;
}

static Compositor* get_nested_compositor_safe(Compositor* comp, Clip* clip, Timeline* nested) {
    RenderSource* sources = (RenderSource*)comp->render_sources;
    for (int i = 0; i < comp->source_count; i++) {
        if (sources[i].type == RENDER_SOURCE_NESTED && sources[i].clip_ref == clip) {
            if (sources[i].nested_timeline != nested) {
                if (sources[i].nested_compositor) {
                    compositor_free(comp->vm, sources[i].nested_compositor);
                }
                sources[i].nested_timeline = nested;
                sources[i].nested_compositor = nested ? compositor_create(comp->vm, nested) : NULL;
                if (sources[i].nested_compositor && sources[i].nested_compositor->mixer) {
                    mixer_free(sources[i].nested_compositor->mixer);
                    sources[i].nested_compositor->mixer = NULL;
                }
            }
            return sources[i].nested_compositor;
        }
    }

    if (comp->source_count >= comp->source_capacity) {
        int old_capacity = comp->source_capacity;
        comp->source_capacity = MEM_GROW_CAPACITY(old_capacity);
        comp->render_sources = GROW_ARRAY(comp->vm, RenderSource,
            comp->render_sources, old_capacity, comp->source_capacity);
        sources = (RenderSource*)comp->render_sources;
    }

    RenderSource* src = &sources[comp->source_count++];
    memset(src, 0, sizeof(RenderSource));
    src->type = RENDER_SOURCE_NESTED;
    src->clip_ref = clip;
    src->nested_timeline = nested;
    src->nested_compositor = nested ? compositor_create(comp->vm, nested) : NULL;
    if (src->nested_compositor && src->nested_compositor->mixer) {
        mixer_free(src->nested_compositor->mixer);
        src->nested_compositor->mixer = NULL;
    }
    return src->nested_compositor;
}
static void bind_yuv_textures(RenderSource* src) {
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, src->tex_y);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, src->tex_u);
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, src->tex_v);
}
static void layout_and_draw_text(Compositor* comp, TimelineClip* tc, GLint loc_model, GLint loc_uv, mat4 group_model, float scale_x, float scale_y) {
    Clip* clip = tc->media;
    float x_cursor = 0;
    const char* p = clip->text.content;
    while (*p) {
        GlyphInfo* glyph = text_renderer_get_glyph(comp->text_renderer, *p);
        float rel_x = x_cursor + glyph->bearing_x;
        float rel_y = clip->text.font_size - glyph->bearing_y;
        float w = glyph->width;
        float h = glyph->height;
        mat4 local_model = mat4_identity();
        local_model = mat4_mult(mat4_translate(rel_x, rel_y), local_model);
        local_model = mat4_mult(mat4_scale(w * scale_x, h * scale_y), local_model);
        mat4 model = mat4_mult(local_model, group_model);
        glUniformMatrix4fv(loc_model, 1, GL_FALSE, model.m);
        glUniform4f(loc_uv, glyph->u0, glyph->v0, glyph->u1, glyph->v1);
        glBindVertexArray(comp->vao);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        x_cursor += glyph->advance;
        p++;
    }
}
static void draw_clip_text(Compositor* comp, TimelineClip* tc) {
    Clip* clip = tc->media;
    TextRenderer* tr = comp->text_renderer;
    text_renderer_update(tr, clip);

    glUseProgram(comp->text_shader_program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, text_renderer_get_texture(tr));
    glUniform1i(comp->text_uniforms.sampler0, 0);

    GLint loc_model = comp->text_uniforms.u_model;
    GLint loc_uv    = comp->text_uniforms.u_uv_rect;
    GLint loc_color = comp->text_uniforms.u_color;

    float start_x = tc->transform.x;
    float start_y = tc->transform.y;
    float rotation = tc->transform.rotation;
    float scale_x = tc->transform.scale_x;
    float scale_y = tc->transform.scale_y;

    float scaled_w = clip->text.cached_width * scale_x;
    float scaled_h = clip->text.cached_height * scale_y;
    float center_x = scaled_w / 2.0f;
    float center_y = scaled_h / 2.0f;

    mat4 group_model = mat4_identity();
    group_model = mat4_mult(mat4_translate(start_x + center_x, start_y + center_y), group_model);
    group_model = mat4_mult(mat4_rotate(rotation), group_model);
    group_model = mat4_mult(mat4_translate(-center_x, -center_y), group_model);

    // === 先绘制描边（如果启用）===
    if (clip->text.stroke_enabled && clip->text.stroke_width > 0.0f) {
        float stroke_r = clip->text.stroke_color.r / 255.0f;
        float stroke_g = clip->text.stroke_color.g / 255.0f;
        float stroke_b = clip->text.stroke_color.b / 255.0f;
        float stroke_a = (clip->text.stroke_color.a / 255.0f) * tc->transform.opacity;

        glUniform4f(loc_color, stroke_r, stroke_g, stroke_b, stroke_a);

        float offset = clip->text.stroke_width * 0.5f;
        for (int dx = -1; dx <= 1; dx++) {
            for (int dy = -1; dy <= 1; dy++) {
                if (dx == 0 && dy == 0) continue;
                mat4 offset_model = mat4_mult(mat4_translate((float)dx * offset, (float)dy * offset), group_model);
                layout_and_draw_text(comp, tc, loc_model, loc_uv, offset_model, scale_x, scale_y);
            }
        }
    }

    // === 再绘制填充文字（覆盖在描边上方）===
    float fill_r = clip->text.color.r / 255.0f;
    float fill_g = clip->text.color.g / 255.0f;
    float fill_b = clip->text.color.b / 255.0f;
    float fill_a = (clip->text.color.a / 255.0f) * tc->transform.opacity;

    glUniform4f(loc_color, fill_r, fill_g, fill_b, fill_a);
    layout_and_draw_text(comp, tc, loc_model, loc_uv, group_model, scale_x, scale_y);
}

static void draw_clip_rect(Compositor* comp, RenderSource* src, TimelineClip* tc) {
    if (src->tex_y == 0) return;
    glUseProgram(comp->shader_program);
    bind_yuv_textures(src);
    glUniform1i(comp->yuv_uniforms.sampler0, 0);
    glUniform1i(comp->yuv_uniforms.sampler1, 1);
    glUniform1i(comp->yuv_uniforms.sampler2, 2);
    float scale_x = tc->transform.scale_x;
    float scale_y = tc->transform.scale_y;
    float rotation = tc->transform.rotation;
    float opacity = tc->transform.opacity;
    float w = (float)tc->media->width * scale_x;
    float h = (float)tc->media->height * scale_y;
    float center_x = w / 2.0f;
    float center_y = h / 2.0f;
    mat4 model = mat4_identity();
    model = mat4_mult(mat4_translate(tc->transform.x + center_x, tc->transform.y + center_y), model);
    model = mat4_mult(mat4_rotate(rotation), model);
    model = mat4_mult(mat4_translate(-center_x, -center_y), model);
    model = mat4_mult(mat4_scale(w, h), model);
    glUniformMatrix4fv(comp->yuv_uniforms.u_model, 1, GL_FALSE, model.m);
    glUniform1f(comp->yuv_uniforms.u_opacity, opacity);
    glBindVertexArray(comp->vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}
static void draw_clip_rgba(Compositor* comp, RenderSource* src, TimelineClip* tc) {
    float scale_x;
    float scale_y;
    float rotation;
    float opacity;
    float w;
    float h;
    float center_x;
    float center_y;
    mat4 model;

    if (!src || !src->image_loaded || src->tex_rgba == 0) return;

    glUseProgram(comp->image_shader_program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, src->tex_rgba);
    glUniform1i(comp->image_uniforms.sampler0, 0);

    scale_x = tc->transform.scale_x;
    scale_y = tc->transform.scale_y;
    rotation = tc->transform.rotation;
    opacity = tc->transform.opacity;
    w = (float)tc->media->width * scale_x;
    h = (float)tc->media->height * scale_y;
    center_x = w / 2.0f;
    center_y = h / 2.0f;
    model = mat4_identity();
    model = mat4_mult(mat4_translate(tc->transform.x + center_x, tc->transform.y + center_y), model);
    model = mat4_mult(mat4_rotate(rotation), model);
    model = mat4_mult(mat4_translate(-center_x, -center_y), model);
    model = mat4_mult(mat4_scale(w, h), model);
    glUniformMatrix4fv(comp->image_uniforms.u_model, 1, GL_FALSE, model.m);
    glUniform1f(comp->image_uniforms.u_opacity, opacity);
    glBindVertexArray(comp->vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}
static void draw_clip_solid(Compositor* comp, TimelineClip* tc) {
    float scale_x = tc->transform.scale_x;
    float scale_y = tc->transform.scale_y;
    float rotation = tc->transform.rotation;
    float opacity = tc->transform.opacity;
    float w = (float)tc->media->width * scale_x;
    float h = (float)tc->media->height * scale_y;
    float center_x = w / 2.0f;
    float center_y = h / 2.0f;
    mat4 model = mat4_identity();

    glUseProgram(comp->color_shader_program);
    model = mat4_mult(mat4_translate(tc->transform.x + center_x, tc->transform.y + center_y), model);
    model = mat4_mult(mat4_rotate(rotation), model);
    model = mat4_mult(mat4_translate(-center_x, -center_y), model);
    model = mat4_mult(mat4_scale(w, h), model);
    glUniformMatrix4fv(comp->color_uniforms.u_model, 1, GL_FALSE, model.m);
    glUniform4f(
        comp->color_uniforms.u_color,
        tc->media->solid.color.r / 255.0f,
        tc->media->solid.color.g / 255.0f,
        tc->media->solid.color.b / 255.0f,
        (tc->media->solid.color.a / 255.0f) * opacity
    );
    glBindVertexArray(comp->vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

static void ensure_effect_targets(Compositor* comp, int width, int height) {
    if (width < 1) width = 1;
    if (height < 1) height = 1;
    if (comp->effect_width == width && comp->effect_height == height &&
        comp->effect_source_texture != 0 && comp->effect_ping_texture != 0 &&
        comp->effect_aux_texture != 0) {
        return;
    }

    comp->effect_width = width;
    comp->effect_height = height;

    if (comp->effect_source_texture == 0) glGenTextures(1, &comp->effect_source_texture);
    if (comp->effect_source_fbo == 0) glGenFramebuffers(1, &comp->effect_source_fbo);
    if (comp->effect_ping_texture == 0) glGenTextures(1, &comp->effect_ping_texture);
    if (comp->effect_ping_fbo == 0) glGenFramebuffers(1, &comp->effect_ping_fbo);
    if (comp->effect_base_texture == 0) glGenTextures(1, &comp->effect_base_texture);
    if (comp->effect_base_fbo == 0) glGenFramebuffers(1, &comp->effect_base_fbo);
    if (comp->effect_aux_texture == 0) glGenTextures(1, &comp->effect_aux_texture);
    if (comp->effect_aux_fbo == 0) glGenFramebuffers(1, &comp->effect_aux_fbo);

    glBindTexture(GL_TEXTURE_2D, comp->effect_source_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, comp->effect_source_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, comp->effect_source_texture, 0);

    glBindTexture(GL_TEXTURE_2D, comp->effect_ping_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, comp->effect_ping_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, comp->effect_ping_texture, 0);

    glBindTexture(GL_TEXTURE_2D, comp->effect_base_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, comp->effect_base_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, comp->effect_base_texture, 0);

    glBindTexture(GL_TEXTURE_2D, comp->effect_aux_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, comp->effect_aux_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, comp->effect_aux_texture, 0);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

static void set_texture_sampling(GLuint texture, bool nearest) {
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, nearest ? GL_NEAREST : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, nearest ? GL_NEAREST : GL_LINEAR);
}

static void draw_texture_transformed(Compositor* comp, GLuint texture, TimelineClip* tc, float width, float height,
                                     bool nearest_sampling) {
    float scale_x = tc->transform.scale_x;
    float scale_y = tc->transform.scale_y;
    float rotation = tc->transform.rotation;
    float opacity = tc->transform.opacity;
    float w = width * scale_x;
    float h = height * scale_y;
    float center_x = w / 2.0f;
    float center_y = h / 2.0f;
    mat4 model = mat4_identity();

    glUseProgram(comp->image_shader_program);
    glActiveTexture(GL_TEXTURE0);
    set_texture_sampling(texture, nearest_sampling);
    glBindTexture(GL_TEXTURE_2D, texture);
    glUniform1i(comp->image_uniforms.sampler0, 0);
    model = mat4_mult(mat4_translate(tc->transform.x + center_x, tc->transform.y + center_y), model);
    model = mat4_mult(mat4_rotate(rotation), model);
    model = mat4_mult(mat4_translate(-center_x, -center_y), model);
    model = mat4_mult(mat4_scale(w, h), model);
    glUniformMatrix4fv(comp->image_uniforms.u_model, 1, GL_FALSE, model.m);
    glUniform1f(comp->image_uniforms.u_opacity, opacity);
    glBindVertexArray(comp->vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    if (nearest_sampling) {
        set_texture_sampling(texture, false);
    }
}

static void draw_texture_fullframe(Compositor* comp, GLuint texture, bool nearest_sampling, float opacity) {
    glUseProgram(comp->copy_shader_program);
    glActiveTexture(GL_TEXTURE0);
    set_texture_sampling(texture, nearest_sampling);
    glBindTexture(GL_TEXTURE_2D, texture);
    glUniform1i(comp->copy_uniforms.sampler0, 0);
    glUniform1f(comp->copy_uniforms.u_opacity, opacity);
    glBindVertexArray(comp->vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    if (nearest_sampling) {
        set_texture_sampling(texture, false);
    }
}

static bool is_nested_timeline_clip(const Clip* clip) {
    return clip && (clip->type == CLIP_TYPE_GROUP || clip->type == CLIP_TYPE_PRECOMP);
}

static void render_nested_timeline_to_target(Compositor* comp, TimelineClip* tc, double clip_time, GLuint target_fbo) {
    ObjClip* owner;
    Timeline* nested;
    Compositor* sub;

    if (!tc || !tc->media || !is_nested_timeline_clip(tc->media)) return;
    owner = tc->media->user_data ? (ObjClip*)tc->media->user_data : NULL;
    nested = (owner && owner->timelineObj) ? owner->timelineObj->timeline : tc->media->nested_timeline.timeline;
    if (!nested) return;

    sub = get_nested_compositor_safe(comp, tc->media, nested);
    if (!sub) return;
    compositor_render(sub, clip_time);

    glBindFramebuffer(GL_FRAMEBUFFER, target_fbo);
    glViewport(0, 0, nested->width, nested->height);
    glDisable(GL_BLEND);
    draw_texture_fullframe(comp, sub->output_texture, false, 1.0f);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

static void compute_adjustment_region_params(TimelineClip* tc,
                                             float* out_center_x, float* out_center_y,
                                             float* out_half_w, float* out_half_h);

static void composite_adjustment_layer(Compositor* comp, TimelineClip* tc, GLuint base_texture, GLuint adjusted_texture,
                                       GLuint mask_texture, bool has_mask, bool nearest_sampling) {
    float region_center_x = 0.0f;
    float region_center_y = 0.0f;
    float region_half_w = 0.0f;
    float region_half_h = 0.0f;
    glUseProgram(comp->adjustment_composite_shader_program);
    glActiveTexture(GL_TEXTURE0);
    set_texture_sampling(base_texture, nearest_sampling);
    glBindTexture(GL_TEXTURE_2D, base_texture);
    glUniform1i(comp->adjustment_u_base_texture, 0);
    glActiveTexture(GL_TEXTURE1);
    set_texture_sampling(adjusted_texture, nearest_sampling);
    glBindTexture(GL_TEXTURE_2D, adjusted_texture);
    glUniform1i(comp->adjustment_u_adjusted_texture, 1);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, has_mask ? mask_texture : base_texture);
    glUniform1i(comp->adjustment_u_mask_texture, 2);
    if (!tc->media->adjustment.affects_whole_frame) {
        compute_adjustment_region_params(tc, &region_center_x, &region_center_y, &region_half_w, &region_half_h);
    }
    glUniform2f(comp->adjustment_u_region_center,
                region_center_x, region_center_y);
    glUniform2f(comp->adjustment_u_region_half_size,
                region_half_w, region_half_h);
    glUniform2f(comp->adjustment_u_canvas_size,
                (float)comp->timeline->width, (float)comp->timeline->height);
    glUniform1f(comp->adjustment_u_opacity, tc->transform.opacity);
    glUniform1f(comp->adjustment_u_feather,
                (float)tc->media->adjustment.feather);
    glUniform1f(comp->adjustment_u_region_rotation,
                tc->transform.rotation);
    glUniform1i(comp->adjustment_u_blend_mode,
                (int)tc->media->adjustment.blend_mode);
    glUniform1i(comp->adjustment_u_mask_mode,
                (int)tc->media->adjustment.mask_mode);
    glUniform1i(comp->adjustment_u_whole_frame,
                tc->media->adjustment.affects_whole_frame ? 1 : 0);
    glUniform1i(comp->adjustment_u_has_mask, has_mask ? 1 : 0);
    glUniform1i(comp->adjustment_u_mask_invert,
                tc->media->adjustment.mask_invert ? 1 : 0);
    glBindVertexArray(comp->vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    if (nearest_sampling) {
        set_texture_sampling(base_texture, false);
        set_texture_sampling(adjusted_texture, false);
    }
}

static void compute_adjustment_region_params(TimelineClip* tc,
                                             float* out_center_x, float* out_center_y,
                                             float* out_half_w, float* out_half_h) {
    float x = tc->transform.x;
    float y = tc->transform.y;
    float w = (float)tc->media->width * tc->transform.scale_x;
    float h = (float)tc->media->height * tc->transform.scale_y;
    if (w < 0.0f) {
        x += w;
        w = -w;
    }
    if (h < 0.0f) {
        y += h;
        h = -h;
    }
    if (out_center_x) *out_center_x = x + w * 0.5f;
    if (out_center_y) *out_center_y = y + h * 0.5f;
    if (out_half_w) *out_half_w = w * 0.5f;
    if (out_half_h) *out_half_h = h * 0.5f;
}

static void capture_framebuffer_to_target(Compositor* comp, GLuint target_fbo) {
    glBindFramebuffer(GL_READ_FRAMEBUFFER, comp->fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, target_fbo);
    glBlitFramebuffer(
        0, 0, comp->timeline->width, comp->timeline->height,
        0, 0, comp->effect_width, comp->effect_height,
        GL_COLOR_BUFFER_BIT, GL_LINEAR
    );
    glBindFramebuffer(GL_FRAMEBUFFER, target_fbo);
}

static void capture_composited_frame_for_adjustment(Compositor* comp) {
    capture_framebuffer_to_target(comp, comp->effect_source_fbo);
}

static void render_clip_source_to_target(Compositor* comp, TimelineClip* tc, double clip_time, GLuint target_fbo) {
    TimelineClip local = *tc;
    mat4 proj;

    local.transform.x = 0.0f;
    local.transform.y = 0.0f;
    local.transform.scale_x = 1.0f;
    local.transform.scale_y = 1.0f;
    local.transform.rotation = 0.0f;
    local.transform.opacity = 1.0f;

    glBindFramebuffer(GL_FRAMEBUFFER, target_fbo);
    glViewport(0, 0, comp->effect_width, comp->effect_height);
    glClearColor(0.f, 0.f, 0.f, 0.f);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    proj = mat4_ortho(0, comp->effect_width, comp->effect_height, 0, -1, 1);
    glUseProgram(comp->shader_program);
    glUniformMatrix4fv(comp->yuv_uniforms.u_projection, 1, GL_FALSE, proj.m);
    glUseProgram(comp->image_shader_program);
    glUniformMatrix4fv(comp->image_uniforms.u_projection, 1, GL_FALSE, proj.m);
    glUseProgram(comp->color_shader_program);
    glUniformMatrix4fv(comp->color_uniforms.u_projection, 1, GL_FALSE, proj.m);
    glUseProgram(comp->text_shader_program);
    glUniformMatrix4fv(comp->text_uniforms.u_projection, 1, GL_FALSE, proj.m);

    if (local.media->type == CLIP_TYPE_TEXT) {
        draw_clip_text(comp, &local);
    } else if (local.media->type == CLIP_TYPE_IMAGE) {
        RenderSource* src = get_source_safe(comp, local.media);
        draw_clip_rgba(comp, src, &local);
    } else if (local.media->type == CLIP_TYPE_SOLID) {
        draw_clip_solid(comp, &local);
    } else if (is_nested_timeline_clip(local.media)) {
        render_nested_timeline_to_target(comp, &local, clip_time, target_fbo);
    } else if (local.media->type == CLIP_TYPE_ADJUSTMENT) {
        capture_composited_frame_for_adjustment(comp);
    } else {
        RenderSource* src = get_source_safe(comp, local.media);
        bool new_frame = decoder_update_video(src->decoder, clip_time);
        uint8_t* data[3];
        int linesize[3];
        int fw = 0, fh = 0;
        if (decoder_get_video_data(src->decoder, data, linesize, &fw, &fh)) {
            if (new_frame) {
                bool resize = (src->width != fw || src->height != fh);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, src->tex_y);
                glPixelStorei(GL_UNPACK_ROW_LENGTH, linesize[0]);
                if (resize) {
                    src->width = fw;
                    src->height = fh;
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, fw, fh, 0, GL_RED, GL_UNSIGNED_BYTE, data[0]);
                } else {
                    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, fw, fh, GL_RED, GL_UNSIGNED_BYTE, data[0]);
                }
                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, src->tex_u);
                glPixelStorei(GL_UNPACK_ROW_LENGTH, linesize[1]);
                if (resize) {
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, fw/2, fh/2, 0, GL_RED, GL_UNSIGNED_BYTE, data[1]);
                } else {
                    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, fw/2, fh/2, GL_RED, GL_UNSIGNED_BYTE, data[1]);
                }
                glActiveTexture(GL_TEXTURE2);
                glBindTexture(GL_TEXTURE_2D, src->tex_v);
                glPixelStorei(GL_UNPACK_ROW_LENGTH, linesize[2]);
                if (resize) {
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, fw/2, fh/2, 0, GL_RED, GL_UNSIGNED_BYTE, data[2]);
                } else {
                    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, fw/2, fh/2, GL_RED, GL_UNSIGNED_BYTE, data[2]);
                }
                glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
            }
            draw_clip_rect(comp, src, &local);
        }
    }
}

static void render_clip_source_to_effect_target(Compositor* comp, TimelineClip* tc, double clip_time) {
    render_clip_source_to_target(comp, tc, clip_time, comp->effect_source_fbo);
}

static bool effect_chain_has_external_source_refs(EffectInstance* effect) {
    while (effect) {
        if (effect->processor && effect->processor->get_source_clip &&
            effect->processor->get_source_clip(effect->data) != 0) {
            return true;
        }
        effect = effect->next;
    }
    return false;
}

static void populate_effect_render_context(Compositor* comp, EffectRenderContext* ctx,
                                           GLuint input_texture, GLuint output_fbo) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->input_texture = input_texture;
    ctx->output_fbo = output_fbo;
    ctx->auxiliary_texture = 0;
    ctx->quad_vao = comp->vao;
    ctx->copy_shader_program = comp->copy_shader_program;
    ctx->tint_shader_program = comp->tint_shader_program;
    ctx->fill_shader_program = comp->fill_shader_program;
    ctx->gradient_ramp_shader_program = comp->gradient_ramp_shader_program;
    ctx->grid_shader_program = comp->grid_shader_program;
    ctx->mosaic_shader_program = comp->mosaic_shader_program;
    ctx->brightness_contrast_shader_program = comp->brightness_contrast_shader_program;
    ctx->blur_shader_program = comp->blur_shader_program;
    ctx->fractal_noise_shader_program = comp->fractal_noise_shader_program;
    ctx->displacement_map_shader_program = comp->displacement_map_shader_program;
    ctx->posterize_shader_program = comp->posterize_shader_program;
    ctx->copy_u_texture = comp->copy_uniforms.sampler0;
    ctx->copy_u_opacity = comp->copy_uniforms.u_opacity;
    ctx->tint_u_texture = comp->tint_uniforms.sampler0;
    ctx->tint_u_amount = comp->tint_uniforms.u_opacity;
    ctx->tint_u_color = comp->tint_uniforms.u_color;
    ctx->fill_u_texture = comp->fill_uniforms.sampler0;
    ctx->fill_u_amount = comp->fill_uniforms.u_opacity;
    ctx->fill_u_color = comp->fill_uniforms.u_color;
    ctx->gradient_u_texture = comp->gradient_uniforms.sampler0;
    ctx->gradient_u_start = comp->gradient_uniforms.u_projection;
    ctx->gradient_u_end = comp->gradient_uniforms.u_model;
    ctx->gradient_u_start_color = comp->gradient_uniforms.u_color;
    ctx->gradient_u_end_color = comp->gradient_uniforms.sampler1;
    ctx->gradient_u_blend = comp->gradient_uniforms.u_opacity;
    ctx->grid_u_texture = comp->grid_uniforms.sampler0;
    ctx->grid_u_size = comp->grid_uniforms.u_projection;
    ctx->grid_u_line_width = comp->grid_uniforms.u_model;
    ctx->grid_u_color = comp->grid_uniforms.u_color;
    ctx->grid_u_opacity = comp->grid_uniforms.u_opacity;
    ctx->mosaic_u_texture = comp->mosaic_uniforms.sampler0;
    ctx->mosaic_u_block_size = comp->mosaic_uniforms.u_projection;
    ctx->mosaic_u_resolution = comp->mosaic_uniforms.u_model;
    ctx->mosaic_u_sharp_colors = comp->mosaic_uniforms.u_opacity;
    ctx->brightness_u_texture = comp->brightness_uniforms.sampler0;
    ctx->brightness_u_brightness = comp->brightness_uniforms.u_projection;
    ctx->brightness_u_contrast = comp->brightness_uniforms.u_model;
    ctx->blur_u_texture = comp->blur_uniforms.sampler0;
    ctx->blur_u_texel_size = comp->blur_uniforms.u_projection;
    ctx->blur_u_radius = comp->blur_uniforms.u_opacity;
    ctx->fractal_u_texture = comp->fractal_uniforms.sampler0;
    ctx->fractal_u_resolution = comp->fractal_uniforms.u_projection;
    ctx->fractal_u_scale = comp->fractal_uniforms.u_model;
    ctx->fractal_u_evolution = comp->fractal_uniforms.u_opacity;
    ctx->fractal_u_contrast = comp->fractal_uniforms.u_color;
    ctx->fractal_u_brightness = comp->fractal_uniforms.sampler1;
    ctx->fractal_u_octaves = comp->fractal_uniforms.sampler2;
    ctx->fractal_u_amount = comp->fractal_u_amount;
    ctx->fractal_u_offset = comp->fractal_u_offset;
    ctx->fractal_u_invert = comp->fractal_u_invert;
    ctx->displacement_u_texture = comp->displacement_uniforms.sampler0;
    ctx->displacement_u_map_texture = comp->displacement_uniforms.sampler1;
    ctx->displacement_u_resolution = comp->displacement_uniforms.u_projection;
    ctx->displacement_u_scale_x = comp->displacement_uniforms.u_model;
    ctx->displacement_u_scale_y = comp->displacement_uniforms.u_opacity;
    ctx->displacement_u_amount = comp->displacement_uniforms.u_color;
    ctx->displacement_u_offset = comp->displacement_u_offset;
    ctx->displacement_u_use_luma = comp->displacement_u_use_luma;
    ctx->posterize_u_texture = comp->posterize_uniforms.sampler0;
    ctx->posterize_u_levels = comp->posterize_uniforms.u_projection;
    ctx->posterize_u_amount = comp->posterize_uniforms.u_opacity;
    ctx->width = comp->effect_width;
    ctx->height = comp->effect_height;
}

static bool render_clip_effect_result_to_auxiliary(Compositor* comp, TimelineClip* tc, double clip_time,
                                                   GLuint scratch_texture, GLuint scratch_fbo,
                                                   bool* out_prefer_nearest) {
    EffectInstance* effect;
    GLuint input_texture;
    GLuint output_texture;
    GLuint output_fbo;
    bool prefer_nearest_output = false;

    if (!comp || !tc) return false;
    effect = tc->effectChain;
    if (!effect || effect_chain_has_external_source_refs(effect)) return false;

    render_clip_source_to_target(comp, tc, clip_time, scratch_fbo);
    input_texture = scratch_texture;
    output_texture = comp->effect_aux_texture;
    output_fbo = comp->effect_aux_fbo;

    while (effect) {
        EffectRenderContext ctx;
        populate_effect_render_context(comp, &ctx, input_texture, output_fbo);
        effect_apply_links(comp->timeline, effect, clip_time);
        effect->processor->apply(effect->data, &ctx, clip_time);
        prefer_nearest_output = ctx.prefer_nearest_output;
        input_texture = output_texture;
        if (output_texture == comp->effect_aux_texture) {
            output_texture = scratch_texture;
            output_fbo = scratch_fbo;
        } else {
            output_texture = comp->effect_aux_texture;
            output_fbo = comp->effect_aux_fbo;
        }
        effect = effect->next;
    }

    if (input_texture != comp->effect_aux_texture) {
        glBindFramebuffer(GL_FRAMEBUFFER, comp->effect_aux_fbo);
        glViewport(0, 0, comp->effect_width, comp->effect_height);
        glDisable(GL_BLEND);
        draw_texture_fullframe(comp, input_texture, prefer_nearest_output, 1.0f);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
    if (out_prefer_nearest) *out_prefer_nearest = prefer_nearest_output;
    return true;
}

static void render_clip_with_effects(Compositor* comp, TimelineClip* tc, double clip_time) {
    EffectInstance* effect = tc->effectChain;
    GLuint input_texture;
    GLuint output_texture = comp->effect_ping_texture;
    GLuint output_fbo = comp->effect_ping_fbo;
    EffectRenderContext ctx;
    bool prefer_nearest_output = false;
    bool has_adjustment_mask = false;

    if (tc->media->type == CLIP_TYPE_ADJUSTMENT) {
        ensure_effect_targets(comp, (int)comp->timeline->width, (int)comp->timeline->height);
        capture_framebuffer_to_target(comp, comp->effect_base_fbo);
        capture_framebuffer_to_target(comp, comp->effect_source_fbo);
    } else {
        ensure_effect_targets(comp, (int)tc->media->width, (int)tc->media->height);
        render_clip_source_to_effect_target(comp, tc, clip_time);
    }
    input_texture = comp->effect_source_texture;

    while (effect) {
        effect_apply_links(comp->timeline, effect, clip_time);
        populate_effect_render_context(comp, &ctx, input_texture, output_fbo);
        if (effect->processor && effect->processor->get_source_clip) {
            u32 source_clip_id = effect->processor->get_source_clip(effect->data);
            if (source_clip_id != 0) {
                TimelineClip* source_clip = timeline_find_clip_by_id(comp->timeline, source_clip_id, NULL, NULL);
                if (source_clip) {
                    double timeline_time = tc->timeline_start + clip_time - tc->source_in;
                    double source_time = timeline_time - source_clip->timeline_start + source_clip->source_in;
                    if (!render_clip_effect_result_to_auxiliary(comp, source_clip, source_time,
                                                                output_texture, output_fbo, NULL)) {
                        render_clip_source_to_target(comp, source_clip, source_time, comp->effect_aux_fbo);
                    }
                    ctx.auxiliary_texture = comp->effect_aux_texture;
                    ctx.has_auxiliary_texture = true;
                }
            }
        }
        effect->processor->apply(effect->data, &ctx, clip_time);
        prefer_nearest_output = ctx.prefer_nearest_output;
        input_texture = output_texture;
        if (output_texture == comp->effect_ping_texture) {
            output_texture = comp->effect_source_texture;
            output_fbo = comp->effect_source_fbo;
        } else {
            output_texture = comp->effect_ping_texture;
            output_fbo = comp->effect_ping_fbo;
        }
        effect = effect->next;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, comp->fbo);
    glViewport(0, 0, comp->timeline->width, comp->timeline->height);
    if (tc->media->type == CLIP_TYPE_ADJUSTMENT) {
        if (tc->media->adjustment.mask_source_clip_id != 0) {
            TimelineClip* mask_clip = timeline_find_clip_by_id(comp->timeline, tc->media->adjustment.mask_source_clip_id, NULL, NULL);
            if (mask_clip) {
                double timeline_time = tc->timeline_start + clip_time - tc->source_in;
                double mask_time = timeline_time - mask_clip->timeline_start + mask_clip->source_in;
                render_clip_source_to_target(comp, mask_clip, mask_time, comp->effect_aux_fbo);
                has_adjustment_mask = true;
            }
        }
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        composite_adjustment_layer(comp, tc, comp->effect_base_texture, input_texture,
                                   comp->effect_aux_texture, has_adjustment_mask, prefer_nearest_output);
    } else {
        draw_texture_transformed(comp, input_texture, tc, (float)comp->effect_width, (float)comp->effect_height,
                                 prefer_nearest_output);
    }
    if (prefer_nearest_output) {
        comp->preview_prefers_nearest = true;
    }
}
static void render_setup(Compositor* comp) {
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
    glUniformMatrix4fv(comp->yuv_uniforms.u_projection, 1, GL_FALSE, proj.m);
    glUseProgram(comp->image_shader_program);
    glUniformMatrix4fv(comp->image_uniforms.u_projection, 1, GL_FALSE, proj.m);
    glUseProgram(comp->color_shader_program);
    glUniformMatrix4fv(comp->color_uniforms.u_projection, 1, GL_FALSE, proj.m);
    glUseProgram(comp->text_shader_program);
    glUniformMatrix4fv(comp->text_uniforms.u_projection, 1, GL_FALSE, proj.m);
    if (comp->mixer) mixer_begin_frame(comp->mixer);
    comp->preview_prefers_nearest = false;
}

typedef struct {
    TimelineClip* clip;
    int track_index;
} ActiveClip;

static int compare_active_clips(const void* a, const void* b) {
    const ActiveClip* aa = (const ActiveClip*)a;
    const ActiveClip* bb = (const ActiveClip*)b;
    if (aa->clip->transform.z_index != bb->clip->transform.z_index) {
        return aa->clip->transform.z_index - bb->clip->transform.z_index;
    }
    return aa->track_index - bb->track_index;
}

static bool active_clip_precedes(const ActiveClip* a, const ActiveClip* b) {
    if (a->clip->transform.z_index != b->clip->transform.z_index) {
        return a->clip->transform.z_index < b->clip->transform.z_index;
    }
    return a->track_index <= b->track_index;
}

static void sort_active_clips(ActiveClip* clips, int count) {
    bool sorted = true;

    if (!clips || count < 2) return;
    for (int i = 1; i < count; i++) {
        if (!active_clip_precedes(&clips[i - 1], &clips[i])) {
            sorted = false;
            break;
        }
    }
    if (sorted) return;

    if (count <= 16) {
        for (int i = 1; i < count; i++) {
            ActiveClip current = clips[i];
            int j = i - 1;
            while (j >= 0 && !active_clip_precedes(&clips[j], &current)) {
                clips[j + 1] = clips[j];
                j--;
            }
            clips[j + 1] = current;
        }
        return;
    }

    qsort(clips, (size_t)count, sizeof(ActiveClip), compare_active_clips);
}

static void render_clip(Compositor* comp, TimelineClip* tc, double time) {
    double anim_time = time - tc->timeline_start;  // 动画相对时间
    // 新增：更新属性值基于关键帧
    tc->transform.x = (float)evaluate_animation(&tc->anim.x, anim_time);
    tc->transform.y = (float)evaluate_animation(&tc->anim.y, anim_time);
    tc->transform.scale_x = (float)evaluate_animation(&tc->anim.scale_x, anim_time);
    tc->transform.scale_y = (float)evaluate_animation(&tc->anim.scale_y, anim_time);
    tc->transform.rotation = (float)evaluate_animation(&tc->anim.rotation, anim_time);
    tc->transform.opacity = (float)evaluate_animation(&tc->anim.opacity, anim_time);
    tc->media->volume = evaluate_animation(&tc->anim.volume, anim_time);
    if (tc->media->type == CLIP_TYPE_TEXT) {
        tc->media->text.font_size = (uint32_t)round(evaluate_animation(&tc->anim.font_size, anim_time));
        text_renderer_update(comp->text_renderer, tc->media);
    }
    double clip_time = anim_time + tc->source_in;
    if (tc->effectChain) {
        render_clip_with_effects(comp, tc, clip_time);
        if (comp->mixer && tc->media->type == CLIP_TYPE_MEDIA) {
            RenderSource* src = get_source_safe(comp, tc->media);
            mixer_add_source(comp->mixer, src->decoder, (float)tc->media->volume);
        }
        return;
    }
    if (tc->media->type == CLIP_TYPE_ADJUSTMENT) {
        return;
    }
    if (tc->media->type == CLIP_TYPE_TEXT) {
        draw_clip_text(comp, tc);
    } else if (tc->media->type == CLIP_TYPE_IMAGE) {
        RenderSource* src = get_source_safe(comp, tc->media);
        draw_clip_rgba(comp, src, tc);
    } else if (tc->media->type == CLIP_TYPE_SOLID) {
        draw_clip_solid(comp, tc);
    } else if (is_nested_timeline_clip(tc->media)) {
        ensure_effect_targets(comp, (int)tc->media->width, (int)tc->media->height);
        render_nested_timeline_to_target(comp, tc, clip_time, comp->effect_source_fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, comp->fbo);
        glViewport(0, 0, comp->timeline->width, comp->timeline->height);
        draw_texture_transformed(comp, comp->effect_source_texture, tc,
                                 (float)tc->media->width, (float)tc->media->height, false);
    } else {
        RenderSource* src = get_source_safe(comp, tc->media);
        bool new_frame = decoder_update_video(src->decoder, clip_time);
        uint8_t* data[3];
        int linesize[3];
        int fw = 0, fh = 0;
        if (decoder_get_video_data(src->decoder, data, linesize, &fw, &fh)) {
            if (new_frame) {
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                bool resize = (src->width != fw || src->height != fh);
                if (resize) {
                    src->width = fw;
                    src->height = fh;
                }
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, src->tex_y);
                glPixelStorei(GL_UNPACK_ROW_LENGTH, linesize[0]);
                if (resize) {
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, fw, fh,
                                 0, GL_RED, GL_UNSIGNED_BYTE, data[0]);
                } else {
                    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, fw, fh,
                                    GL_RED, GL_UNSIGNED_BYTE, data[0]);
                }
                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, src->tex_u);
                glPixelStorei(GL_UNPACK_ROW_LENGTH, linesize[1]);
                if (resize) {
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, fw/2, fh/2,
                                 0, GL_RED, GL_UNSIGNED_BYTE, data[1]);
                } else {
                    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, fw/2, fh/2,
                                    GL_RED, GL_UNSIGNED_BYTE, data[1]);
                }
                glActiveTexture(GL_TEXTURE2);
                glBindTexture(GL_TEXTURE_2D, src->tex_v);
                glPixelStorei(GL_UNPACK_ROW_LENGTH, linesize[2]);
                if (resize) {
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, fw/2, fh/2,
                                 0, GL_RED, GL_UNSIGNED_BYTE, data[2]);
                } else {
                    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, fw/2, fh/2,
                                    GL_RED, GL_UNSIGNED_BYTE, data[2]);
                }
                glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
            }
            draw_clip_rect(comp, src, tc);
        }
        if (comp->mixer) {
            mixer_add_source(comp->mixer, src->decoder, (float)tc->media->volume);
        }
    }
}
static void render_cleanup(Compositor* comp) {
    if (comp->mixer) mixer_end_frame(comp->mixer);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    comp->cpu_buffer_stale = true;
}
Compositor* compositor_create(VM* vm, Timeline* timeline) {
    Compositor* comp = ALLOCATE(vm, Compositor, 1);
    memset(comp, 0, sizeof(Compositor));
    comp->vm = vm;
    comp->timeline = timeline;
    comp->mixer = mixer_create(44100);
    comp->shader_program = build_shader_program(VS_SOURCE, FS_SOURCE_YUV);
    comp->image_shader_program = build_shader_program(VS_SOURCE, FS_SOURCE_RGBA);
    comp->color_shader_program = build_shader_program(VS_SOURCE, FS_SOURCE_COLOR);
    comp->text_shader_program = build_shader_program(VS_SOURCE, FS_SOURCE_TEXT);
    comp->copy_shader_program = build_shader_program(VS_SCREEN, FS_SOURCE_COPY);
    comp->tint_shader_program = build_shader_program(VS_SCREEN, FS_SOURCE_TINT);
    comp->fill_shader_program = build_shader_program(VS_SCREEN, FS_SOURCE_FILL);
    comp->gradient_ramp_shader_program = build_shader_program(VS_SCREEN, FS_SOURCE_GRADIENT_RAMP);
    comp->grid_shader_program = build_shader_program(VS_SCREEN, FS_SOURCE_GRID);
    comp->mosaic_shader_program = build_shader_program(VS_SCREEN, FS_SOURCE_MOSAIC);
    comp->brightness_contrast_shader_program = build_shader_program(VS_SCREEN, FS_SOURCE_BRIGHTNESS_CONTRAST);
    comp->blur_shader_program = build_shader_program(VS_SCREEN, FS_SOURCE_BLUR);
    comp->fractal_noise_shader_program = build_shader_program(VS_SCREEN, FS_SOURCE_FRACTAL_NOISE);
    comp->displacement_map_shader_program = build_shader_program(VS_SCREEN, FS_SOURCE_DISPLACEMENT_MAP);
    comp->posterize_shader_program = build_shader_program(VS_SCREEN, FS_SOURCE_POSTERIZE);
    comp->adjustment_composite_shader_program = build_shader_program(VS_SCREEN, FS_SOURCE_ADJUSTMENT_COMPOSITE);
    comp->text_renderer = text_renderer_create();
    init_compositor_uniform_caches(comp);
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
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glGenFramebuffers(1, &comp->fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, comp->fbo);
    glGenTextures(1, &comp->output_texture);
    glBindTexture(GL_TEXTURE_2D, comp->output_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, timeline->width, timeline->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, comp->output_texture, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "Error: Framebuffer is not complete!\n");
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    cache_uniform_locations(comp);
    return comp;
}
void compositor_free(VM* vm, Compositor* comp) {
    if (!comp) return;
    if (comp->text_renderer) text_renderer_free(comp->text_renderer);
    glDeleteProgram(comp->text_shader_program);
    glDeleteProgram(comp->image_shader_program);
    glDeleteProgram(comp->color_shader_program);
    glDeleteProgram(comp->copy_shader_program);
    glDeleteProgram(comp->tint_shader_program);
    glDeleteProgram(comp->fill_shader_program);
    glDeleteProgram(comp->gradient_ramp_shader_program);
    glDeleteProgram(comp->grid_shader_program);
    glDeleteProgram(comp->mosaic_shader_program);
    glDeleteProgram(comp->brightness_contrast_shader_program);
    glDeleteProgram(comp->blur_shader_program);
    glDeleteProgram(comp->fractal_noise_shader_program);
    glDeleteProgram(comp->displacement_map_shader_program);
    glDeleteProgram(comp->posterize_shader_program);
    glDeleteProgram(comp->adjustment_composite_shader_program);
    if (comp->mixer) mixer_free(comp->mixer);
    RenderSource* sources = (RenderSource*)comp->render_sources;
    for(int i=0; i<comp->source_count; i++) {
        if (sources[i].type == RENDER_SOURCE_MEDIA) {
            decoder_destroy(sources[i].decoder);
            glDeleteTextures(1, &sources[i].tex_y);
            glDeleteTextures(1, &sources[i].tex_u);
            glDeleteTextures(1, &sources[i].tex_v);
        } else if (sources[i].type == RENDER_SOURCE_IMAGE) {
            glDeleteTextures(1, &sources[i].tex_rgba);
        } else if (sources[i].type == RENDER_SOURCE_NESTED && sources[i].nested_compositor) {
            compositor_free(vm, sources[i].nested_compositor);
        }
    }
    if (comp->render_sources) {
        free(comp->render_sources);
    }
    if(comp->cpu_output_buffer) free(comp->cpu_output_buffer);
    if(comp->cpu_flip_row) free(comp->cpu_flip_row);
    if(comp->active_clips_buffer) free(comp->active_clips_buffer);
    glDeleteProgram(comp->shader_program);
    if (comp->effect_source_fbo) glDeleteFramebuffers(1, &comp->effect_source_fbo);
    if (comp->effect_ping_fbo) glDeleteFramebuffers(1, &comp->effect_ping_fbo);
    if (comp->effect_base_fbo) glDeleteFramebuffers(1, &comp->effect_base_fbo);
    if (comp->effect_aux_fbo) glDeleteFramebuffers(1, &comp->effect_aux_fbo);
    if (comp->effect_source_texture) glDeleteTextures(1, &comp->effect_source_texture);
    if (comp->effect_ping_texture) glDeleteTextures(1, &comp->effect_ping_texture);
    if (comp->effect_base_texture) glDeleteTextures(1, &comp->effect_base_texture);
    if (comp->effect_aux_texture) glDeleteTextures(1, &comp->effect_aux_texture);
    glDeleteFramebuffers(1, &comp->fbo);
    glDeleteTextures(1, &comp->output_texture);
    glDeleteBuffers(1, &comp->vbo);
    glDeleteVertexArrays(1, &comp->vao);
    FREE(vm, Compositor, comp);
}
void compositor_render(Compositor* comp, double time) {
    ActiveClip* active_clips;
    int active_count = 0;

    render_setup(comp);

    if (comp->active_clips_capacity < (int)comp->timeline->track_count) {
        comp->active_clips_capacity = (int)comp->timeline->track_count;
        comp->active_clips_buffer = realloc(comp->active_clips_buffer, sizeof(ActiveClip) * (size_t)comp->active_clips_capacity);
    }
    active_clips = (ActiveClip*)comp->active_clips_buffer;
    for(int i=0; i<comp->timeline->track_count; i++) {
        Track* track = &comp->timeline->tracks[i];
        if (!(track->flags & 1)) continue;
        TimelineClip* tc = timeline_get_clip_at(track, time);
        if (!tc) continue;
        if (!(tc->flags & 1)) continue;
        active_clips[active_count].clip = tc;
        active_clips[active_count].track_index = i;
        active_count++;
    }

    sort_active_clips(active_clips, active_count);
    for (int i = 0; i < active_count; i++) {
        render_clip(comp, active_clips[i].clip, time);
    }
    render_cleanup(comp);
}
void compositor_blit_to_screen(Compositor* comp, i32 window_width, i32 window_height) {
    static GLuint blit_program = 0;
    static GLint blit_sampler_loc = -1;
    if (blit_program == 0) {
        blit_program = build_shader_program(VS_SCREEN, FS_SCREEN);
        blit_sampler_loc = glGetUniformLocation(blit_program, "screenTexture");
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, window_width, window_height);
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(blit_program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, comp->output_texture);
    set_texture_sampling(comp->output_texture, comp->preview_prefers_nearest);
    glUniform1i(blit_sampler_loc, 0);
    glBindVertexArray(comp->vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    if (comp->preview_prefers_nearest) {
        set_texture_sampling(comp->output_texture, false);
    }
}
void compositor_read_pixels(Compositor* comp, u8* out_buffer) {
    if (!out_buffer) return;
    glBindFramebuffer(GL_FRAMEBUFFER, comp->fbo);
    i32 width = comp->timeline->width;
    i32 height = comp->timeline->height;
    i32 stride = width * 4;
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, out_buffer);
    if (comp->cpu_flip_row_capacity < (size_t)stride) {
        comp->cpu_flip_row = realloc(comp->cpu_flip_row, (size_t)stride);
        comp->cpu_flip_row_capacity = (size_t)stride;
    }
    for (int y = 0; y < height / 2; y++) {
        u8* top_row = out_buffer + y * stride;
        u8* bot_row = out_buffer + (height - 1 - y) * stride;
        memcpy(comp->cpu_flip_row, top_row, stride);
        memcpy(top_row, bot_row, stride);
        memcpy(bot_row, comp->cpu_flip_row, stride);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

u8* compositor_get_cpu_buffer_raw(Compositor* comp) {
    if (comp->cpu_buffer_stale) {
        size_t size = comp->timeline->width * comp->timeline->height * 4;
        if (!comp->cpu_output_buffer) {
            comp->cpu_output_buffer = reallocate(comp->vm, NULL, 0, size);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, comp->fbo);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, comp->timeline->width, comp->timeline->height, GL_RGBA, GL_UNSIGNED_BYTE, comp->cpu_output_buffer);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        comp->cpu_buffer_stale = false;
        comp->cpu_buffer_flipped = false;
    }
    return comp->cpu_output_buffer;
}

u8* compositor_get_cpu_buffer(Compositor* comp) {
    u8* buffer = compositor_get_cpu_buffer_raw(comp);
    i32 width;
    i32 height;
    i32 stride;

    if (!buffer) return NULL;
    if (comp->cpu_buffer_flipped) return buffer;

    width = comp->timeline->width;
    height = comp->timeline->height;
    stride = width * 4;
    if (comp->cpu_flip_row_capacity < (size_t)stride) {
        comp->cpu_flip_row = realloc(comp->cpu_flip_row, (size_t)stride);
        comp->cpu_flip_row_capacity = (size_t)stride;
    }
    for (int y = 0; y < height / 2; y++) {
        u8* top_row = buffer + y * stride;
        u8* bot_row = buffer + (height - 1 - y) * stride;
        memcpy(comp->cpu_flip_row, top_row, stride);
        memcpy(top_row, bot_row, stride);
        memcpy(bot_row, comp->cpu_flip_row, stride);
    }
    comp->cpu_buffer_flipped = true;
    return buffer;
}
