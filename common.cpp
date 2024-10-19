/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// common.cpp : Shared elements between the platforms, such as test scenes

#include <cassert>
#include <stdlib.h>
#include "common.h"

const f32 testcolors[4][5] = {
    // Red
    {  1.0f,  0.4f, -0.1f,  0.1f, 0.4f},
    // Green
    {  0.4f,  1.0f,  1.0f,  0.4f, 0.1f},
    // Blue
    { -0.1f, -0.1f,  0.4f,  1.0f, 1.0f},
    // Alpha
    {  1.0f,  1.0f,  1.0f,  1.0f, 1.0f}
};

void TestColors_Gradient_PixelCallback(float output[], f32 x, f32 y, f32 width, f32 height)
{
    constexpr u32 limit = sizeof(testcolors[0]) / sizeof(testcolors[0][0]);
    constexpr u32 limit1 = limit - 1;
    constexpr u32 limit2 = limit - 2;
    f32 f = (x / (width - 1.0f)) * limit1;
    f = f < 0.0f ? 0.0f : f < (float)limit1 ? f : (float)limit1;
    u32 i = (int)floor(f);
    i = i < 0 ? 0 : i < limit2 ? i : limit2;
    f32 lerp = (f - i) < 1.0f ? (f - i) : 1.0f;
    f32 ilerp = 1.0f - lerp;
    for (u32 c = 0; c < 4; c++)
        output[c] = testcolors[c][i] * ilerp + testcolors[c][i + 1] * lerp;
}

void Compositor_Scene_Make_Colorspace_Test(Compositor_Scene* scene)
{
    constexpr Compositor_PixelFormat pixelformats[] =
    {
        Compositor_PixelFormat::srgb,
        Compositor_PixelFormat::rec709,
        // Not supported by Windows DirectComposition
        // Compositor_PixelFormat::dcip3,
        Compositor_PixelFormat::rec2020_8bit,
        Compositor_PixelFormat::rec2020_10bit,
        Compositor_PixelFormat::rec2100_10bit,
        Compositor_PixelFormat::rgba16f_scrgb,
    };
    constexpr size_t len = sizeof(pixelformats) / sizeof(pixelformats[0]);

    constexpr u64 refreshcounter = 1;
    constexpr u16 w = 256;
    constexpr u16 h = 32;
    scene->layers.clear();
    scene->layers.reserve(len);
    // Put a gradient image behind everything
    u64 sort = 0;
    scene->layers.push_back(
        Compositor_Scene_Layer(
            refreshcounter,
            sort++,
            0,
            0,
            0,
            0,
            TestColors_Gradient_PixelCallback,
            Compositor_PixelFormat::rgba16f_scrgb,
            true
        )
    );
    // Add several smaller gradients in different formats
    for (u64 i = 0; i < len; i++)
        scene->layers.push_back(
            Compositor_Scene_Layer(
                refreshcounter,
                sort++,
                0,
                (u16)(h * i),
                w,
                h,
                TestColors_Gradient_PixelCallback,
                pixelformats[i],
                false
            )
        );
}

void Color_OETF(Compositor_PixelFormat pixelformat, f32 c[], f32 o[])
{
    switch (pixelformat & Compositor_Format_Flags::TRANSFER_MASK)
    {
    default:
    case Compositor_Format_Flags::TRANSFER_NONE:
        for (u32 i = 0; i < 4; i++)
            o[i] = c[i];
        break;
    case Compositor_Format_Flags::TRANSFER_REC709:
        for (u32 i = 0; i < 3; i++)
        {
            f32 f = c[i];
            // rec709 only allows 0-1 colors
            f = f < 0.0f ? 0.0f : f < 1.0f ? f : 1.0f;
            o[i] = f < 0.018f ? f * 4.5f : 1.099f * powf(f, 0.454545f) - 0.099f;
        }
        o[3] = c[3];
        break;
    case Compositor_Format_Flags::TRANSFER_REC2020_12BIT:
        for (u32 i = 0; i < 3; i++)
        {
            f32 f = c[i];
            // rec2020 only allows 0-1 colors
            f = f < 0.0f ? 0.0f : f < 1.0f ? f : 1.0f;
            o[i] = f < 0.0181f ? f * 4.5f : 1.0993f * powf(f, 0.454545f) - 0.0993f;
        }
        o[3] = c[3];
        break;
    case Compositor_Format_Flags::TRANSFER_SRGB:
        for (u32 i = 0; i < 3; i++)
        {
            f32 f = c[i];
            // sRGB only allows 0-1 colors
            f = f < 0.0f ? 0.0f : f < 1.0f ? f : 1.0f;
            o[i] = f < 0.0031308f ? f * 12.92f : 1.055f * powf(f, 0.41666f) - 0.055f;
        }
        o[3] = c[3];
        break;
    }
}

static void Pixel_To_Int(f32 c[], f32 scale, f32 low, f32 high)
{
    for (u32 i = 0; i < 4; i++)
    {
        f32 f = c[i];
        f *= scale;
        f = floorf(f + 0.5f);
        f = f < low ? low : f < high ? f : high;
        c[i] = f;
    }
}

/// This converts an f32 to an f16 using bit manipulation (which achieves round
/// to nearest behavior, which may not be the active floating point mode).
/// See https://en.wikipedia.org/wiki/Half-precision_floating-point_format and
/// compare to https://en.wikipedia.org/wiki/Single-precision_floating-point_format
static u16 ToF16(f32 f)
{
    union
    {
        f32 f;
        u32 i;
    }
    u;
    u.f = f;
    u32 i = u.i;
    // Adjust exponent from +126 bias to +14 bias, if it would become less than
    // exponent 1 we treat it as a full zero (rather than try to deal with
    // denormals, which typically have a performance penalty anyway)
    u32 a = ((i & 0x7FFFFFFF) < 0x38000000) ? 0 : i - 0x38000000;
    // Shift exponent and mantissa to the correct place (same shift for both)
    // and put the sign bit into place
    u16 n = (a >> 13) | ((a & 0x80000000) >> 16);
    return n;
}

u8 Compositor_BytesPerPixelFormat(Compositor_PixelFormat pixelformat)
{
    switch (pixelformat & Compositor_Format_Flags::FORMAT_MASK)
    {
    case Compositor_Format_Flags::FORMAT_RGBA32F: return 16;
    case Compositor_Format_Flags::FORMAT_RGBA16F: return 8;
    case Compositor_Format_Flags::FORMAT_RGB10A2: return 4;
    case Compositor_Format_Flags::FORMAT_RGBA8: return 4;
    case Compositor_Format_Flags::FORMAT_BGRA8: return 4;
    default:
        assert(false);
        return -1;
    }
}

void Generate_Image(void* pixels, Compositor_Scene_Layer* layer)
{
    u32 pixelformat = layer->pixelformat;
    u16 width = layer->width;
    u16 height = layer->height;
    switch (pixelformat & Compositor_Format_Flags::FORMAT_MASK)
    {
    case Compositor_Format_Flags::FORMAT_RGBA32F:
        for (u16 y = 0; y < height; y++)
        {
            for (u16 x = 0; x < width; x++)
            {
                auto p = (f32*)pixels + 4 * (y * width + x);
                f32 c[4];
                layer->pixelcallback(c, x, y, width, height);
                Color_OETF(layer->pixelformat, c, c);
                for (u16 i = 0; i < 4; i++)
                    p[i] = c[i];
            }
        }
        break;
    case Compositor_Format_Flags::FORMAT_RGBA16F:
        for (u16 y = 0; y < height; y++)
        {
            for (u16 x = 0; x < width; x++)
            {
                auto p = (u64*)pixels + (y * width + x);
                f32 c[4];
                Color_OETF(layer->pixelformat, c, c);
                layer->pixelcallback(c, x, y, width, height);
                *p =
                    (u64)ToF16(c[0]) * 0x1 +
                    (u64)ToF16(c[1]) * 0x10000 +
                    (u64)ToF16(c[2]) * 0x100000000 +
                    (u64)ToF16(c[3]) * 0x1000000000000;
            }
        }
        break;
    case Compositor_Format_Flags::FORMAT_RGB10A2:
        for (u16 y = 0; y < height; y++)
        {
            for (u16 x = 0; x < width; x++)
            {
                auto p = (u32*)pixels + y * width + x;
                f32 c[4];
                layer->pixelcallback(c, x, y, width, height);
                Color_OETF(layer->pixelformat, c, c);
                Pixel_To_Int(c, 1023.0f, 0.0f, 1023.0f);
                *p =
                    (u32)c[0] * 0x1 +
                    (u32)c[1] * 0x400 +
                    (u32)c[2] * 0x100000 +
                    ((u32)c[3] >> 8) * 0xC0000000;
            }
        }
        break;
    case Compositor_Format_Flags::FORMAT_RGBA8:
        for (u16 y = 0; y < height; y++)
        {
            for (u16 x = 0; x < width; x++)
            {
                auto p = (u32*)pixels + y * width + x;
                f32 c[4];
                layer->pixelcallback(c, x, y, width, height);
                Color_OETF(layer->pixelformat, c, c);
                Pixel_To_Int(c, 255.0f, 0.0f, 255.0f);
                *p =
                    (u32)c[0] * 0x1 +
                    (u32)c[1] * 0x100 +
                    (u32)c[2] * 0x10000 +
                    (u32)c[3] * 0x1000000;
            }
        }
        break;
    case Compositor_Format_Flags::FORMAT_BGRA8:
        for (u16 y = 0; y < height; y++)
        {
            for (u16 x = 0; x < width; x++)
            {
                auto p = (u32*)pixels + y * width + x;
                f32 c[4];
                layer->pixelcallback(c, x, y, width, height);
                Color_OETF(layer->pixelformat, c, c);
                Pixel_To_Int(c, 255.0f, 0.0f, 255.0f);
                *p =
                    (u32)c[2] * 0x1 +
                    (u32)c[1] * 0x100 +
                    (u32)c[0] * 0x10000 +
                    (u32)c[3] * 0x1000000;
            }
        }
        break;
    }
}
