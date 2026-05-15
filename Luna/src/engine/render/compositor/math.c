#include "internal.h"
#define TEXT_RENDERER_MAX_GLYPHS 128
mat4 mat4_identity(void) {
    mat4 res = {0};
    res.m[0] = 1.0f; res.m[5] = 1.0f; res.m[10] = 1.0f; res.m[15] = 1.0f;
    return res;
}
mat4 mat4_mult(mat4 a, mat4 b) {
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
mat4 mat4_translate(float x, float y) {
    mat4 res = mat4_identity();
    res.m[12] = x;
    res.m[13] = y;
    return res;
}
mat4 mat4_scale(float sx, float sy) {
    mat4 res = mat4_identity();
    res.m[0] = sx;
    res.m[5] = sy;
    return res;
}
mat4 mat4_rotate(float angle_deg) {
    float rad = angle_deg * 3.141592653589793f / 180.0f;
    float c = cosf(rad);
    float s = sinf(rad);
    mat4 res = mat4_identity();
    res.m[0] = c; res.m[1] = -s;
    res.m[4] = s; res.m[5] = c;
    return res;
}
mat4 mat4_shear_x(float factor) {
    mat4 res = mat4_identity();
    res.m[4] = factor;
    return res;
}
mat4 mat4_skew(float skew_deg, float axis_deg) {
    float clamped = fmaxf(fminf(skew_deg, 89.0f), -89.0f);
    float shear = tanf(clamped * 3.141592653589793f / 180.0f);
    mat4 local = mat4_identity();
    local = mat4_mult(mat4_rotate(axis_deg), local);
    local = mat4_mult(mat4_shear_x(shear), local);
    local = mat4_mult(mat4_rotate(-axis_deg), local);
    return local;
}
mat4 mat4_ortho(float left, float right, float bottom, float top, float near, float far) {
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
