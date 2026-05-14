// src/engine/media/utils/image_loader.h

#pragma once

#include <stdbool.h>
#include <stdint.h>

bool image_load_rgba(const char* filepath, uint8_t** out_pixels, int* out_width, int* out_height);
bool image_probe_dimensions(const char* filepath, int* out_width, int* out_height);
