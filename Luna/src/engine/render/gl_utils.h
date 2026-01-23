// src/engine/render/gl_utils.h

#pragma once
#include <glad/glad.h>
#include <stdio.h>

static GLuint compile_shader(const char* src, GLenum type) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, NULL);
    glCompileShader(shader);
    
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(shader, 512, NULL, infoLog);
        fprintf(stderr, "Shader Compilation Error: %s\n", infoLog);
    }
    return shader;
}

static GLuint build_shader_program(const char* vs_src, const char* fs_src) {
    GLuint vs = compile_shader(vs_src, GL_VERTEX_SHADER);
    GLuint fs = compile_shader(fs_src, GL_FRAGMENT_SHADER);
    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    
    GLint success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetProgramInfoLog(program, 512, NULL, infoLog);
        fprintf(stderr, "Program Linking Error: %s\n", infoLog);
    }

    glDeleteShader(vs);
    glDeleteShader(fs);
    return program;
}