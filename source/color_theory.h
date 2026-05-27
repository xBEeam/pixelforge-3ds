#ifndef COLOR_THEORY_H
#define COLOR_THEORY_H

#include <stdint.h>
#include <stdbool.h>

// Hex format throughout: 0xRRGGBB (24-bit, no alpha)

typedef struct { uint8_t r, g, b; } RGB;
typedef struct { float h, s, l; } HSL;  // h in [0,360), s/l in [0,100]

RGB    ct_hex_to_rgb(uint32_t hex);
uint32_t ct_rgb_to_hex(RGB rgb);
HSL    ct_rgb_to_hsl(RGB rgb);
RGB    ct_hsl_to_rgb(HSL hsl);

// Hue-shifted derivatives. intensity 1.0 = one step, 2.0 = two steps, etc.
uint32_t ct_shade_once(uint32_t hex, float intensity);
uint32_t ct_highlight_once(uint32_t hex, float intensity);

// Fills out[0..4] with [shadow-2, shadow-1, base, light-1, light-2].
void ct_build_ramp(uint32_t base, uint32_t out[5]);

// Nearest by squared RGB distance.
uint8_t ct_nearest_palette_index(uint32_t target,
                                 const uint32_t* palette,
                                 uint8_t palette_size);

// Resolves a target hex into a palette index, either by snapping or by
// appending to the palette. If snap=false and the palette is full,
// silently falls back to snap. Returns the resolved index.
uint8_t ct_resolve_index(uint32_t target,
                         uint32_t* palette,
                         uint8_t* palette_size,
                         uint8_t palette_max,
                         bool snap);

// citro2d uses ABGR8888. Helper to convert our hex format.
uint32_t ct_c2d_color(uint32_t hex);

#endif // COLOR_THEORY_H
