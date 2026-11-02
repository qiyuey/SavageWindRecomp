// Blocking ASTC decode: large images use a small worker pool; pixels are ready on return.
#pragma once
#include <stdbool.h>
#include <stdint.h>

void vk_astc_decode_ldr(const uint8_t *blocks, uint32_t width, uint32_t height,
                        uint32_t bw, uint32_t bh, bool srgb, uint8_t *out);
