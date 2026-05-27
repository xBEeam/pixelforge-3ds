#include "color_theory.h"
#include <math.h>

RGB ct_hex_to_rgb(uint32_t hex) {
    RGB rgb;
    rgb.r = (hex >> 16) & 0xFF;
    rgb.g = (hex >> 8)  & 0xFF;
    rgb.b =  hex        & 0xFF;
    return rgb;
}

uint32_t ct_rgb_to_hex(RGB rgb) {
    return ((uint32_t)rgb.r << 16)
         | ((uint32_t)rgb.g << 8)
         |  (uint32_t)rgb.b;
}

HSL ct_rgb_to_hsl(RGB rgb) {
    float r = rgb.r / 255.0f;
    float g = rgb.g / 255.0f;
    float b = rgb.b / 255.0f;
    float maxv = fmaxf(fmaxf(r, g), b);
    float minv = fminf(fminf(r, g), b);
    HSL hsl;
    hsl.l = (maxv + minv) / 2.0f;
    if (maxv == minv) {
        hsl.h = 0.0f;
        hsl.s = 0.0f;
    } else {
        float d = maxv - minv;
        hsl.s = hsl.l > 0.5f ? d / (2.0f - maxv - minv)
                             : d / (maxv + minv);
        if      (maxv == r) hsl.h = (g - b) / d + (g < b ? 6.0f : 0.0f);
        else if (maxv == g) hsl.h = (b - r) / d + 2.0f;
        else                hsl.h = (r - g) / d + 4.0f;
        hsl.h *= 60.0f;
    }
    hsl.s *= 100.0f;
    hsl.l *= 100.0f;
    return hsl;
}

static float hue2rgb(float p, float q, float t) {
    if (t < 0.0f) t += 1.0f;
    if (t > 1.0f) t -= 1.0f;
    if (t < 1.0f / 6.0f) return p + (q - p) * 6.0f * t;
    if (t < 1.0f / 2.0f) return q;
    if (t < 2.0f / 3.0f) return p + (q - p) * (2.0f / 3.0f - t) * 6.0f;
    return p;
}

RGB ct_hsl_to_rgb(HSL hsl) {
    float h = fmodf(fmodf(hsl.h, 360.0f) + 360.0f, 360.0f) / 360.0f;
    float s = hsl.s / 100.0f;
    float l = hsl.l / 100.0f;
    float r, g, b;
    if (s == 0.0f) {
        r = g = b = l;
    } else {
        float q = l < 0.5f ? l * (1.0f + s) : l + s - l * s;
        float p = 2.0f * l - q;
        r = hue2rgb(p, q, h + 1.0f / 3.0f);
        g = hue2rgb(p, q, h);
        b = hue2rgb(p, q, h - 1.0f / 3.0f);
    }
    RGB out;
    out.r = (uint8_t)fminf(255.0f, fmaxf(0.0f, r * 255.0f + 0.5f));
    out.g = (uint8_t)fminf(255.0f, fmaxf(0.0f, g * 255.0f + 0.5f));
    out.b = (uint8_t)fminf(255.0f, fmaxf(0.0f, b * 255.0f + 0.5f));
    return out;
}

static float shift_hue_toward(float h, float target, float amount) {
    float diff = target - h;
    if (diff > 180.0f)  diff -= 360.0f;
    if (diff < -180.0f) diff += 360.0f;
    float sign = (diff > 0.0f) - (diff < 0.0f);
    float shift = sign * fminf(fabsf(diff), amount);
    float r = h + shift;
    while (r < 0.0f)     r += 360.0f;
    while (r >= 360.0f)  r -= 360.0f;
    return r;
}

uint32_t ct_shade_once(uint32_t hex, float intensity) {
    HSL hsl = ct_rgb_to_hsl(ct_hex_to_rgb(hex));
    hsl.l = fmaxf(2.0f, hsl.l - 12.0f * intensity);
    hsl.h = shift_hue_toward(hsl.h, 240.0f, 12.0f * intensity);
    float desat = hsl.l < 20.0f ? 8.0f : 3.0f;
    hsl.s = fmaxf(0.0f, hsl.s - desat * intensity);
    return ct_rgb_to_hex(ct_hsl_to_rgb(hsl));
}

uint32_t ct_highlight_once(uint32_t hex, float intensity) {
    HSL hsl = ct_rgb_to_hsl(ct_hex_to_rgb(hex));
    hsl.l = fminf(98.0f, hsl.l + 12.0f * intensity);
    hsl.h = shift_hue_toward(hsl.h, 60.0f, 12.0f * intensity);
    float desat = hsl.l > 80.0f ? 10.0f : 4.0f;
    hsl.s = fmaxf(0.0f, hsl.s - desat * intensity);
    return ct_rgb_to_hex(ct_hsl_to_rgb(hsl));
}

void ct_build_ramp(uint32_t base, uint32_t out[5]) {
    out[0] = ct_shade_once(base, 2.0f);
    out[1] = ct_shade_once(base, 1.0f);
    out[2] = base;
    out[3] = ct_highlight_once(base, 1.0f);
    out[4] = ct_highlight_once(base, 2.0f);
}

uint8_t ct_nearest_palette_index(uint32_t target,
                                 const uint32_t* palette,
                                 uint8_t palette_size) {
    RGB t = ct_hex_to_rgb(target);
    int best = 0;
    float best_d = 1e30f;
    for (int i = 0; i < palette_size; i++) {
        RGB p = ct_hex_to_rgb(palette[i]);
        float dr = (float)t.r - (float)p.r;
        float dg = (float)t.g - (float)p.g;
        float db = (float)t.b - (float)p.b;
        float d  = dr * dr + dg * dg + db * db;
        if (d < best_d) { best_d = d; best = i; }
    }
    return (uint8_t)best;
}

uint8_t ct_resolve_index(uint32_t target,
                        uint32_t* palette,
                        uint8_t* palette_size,
                        uint8_t palette_max,
                        bool snap) {
    // If the exact target already exists, return it
    for (int i = 0; i < *palette_size; i++) {
        if (palette[i] == target) return (uint8_t)i;
    }
    if (snap || *palette_size >= palette_max) {
        return ct_nearest_palette_index(target, palette, *palette_size);
    }
    // Append new color to the palette
    palette[*palette_size] = target;
    uint8_t idx = *palette_size;
    (*palette_size)++;
    return idx;
}

// citro2d C2D_Color32 is ABGR8888 (little-endian R first in memory).
// Construct with full alpha from our 0xRRGGBB hex.
uint32_t ct_c2d_color(uint32_t hex) {
    uint8_t r = (hex >> 16) & 0xFF;
    uint8_t g = (hex >> 8)  & 0xFF;
    uint8_t b =  hex        & 0xFF;
    return ((uint32_t)0xFF << 24)
         | ((uint32_t)b    << 16)
         | ((uint32_t)g    << 8)
         |  (uint32_t)r;
}
