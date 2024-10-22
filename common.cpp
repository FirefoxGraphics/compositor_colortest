/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// common.cpp : Shared elements between the platforms, such as test scenes

#include <cassert>
#include <stdlib.h>
#include "common.h"

// These test colors represent an RGB color wheel, but with deliberately out of
// gamut colors (which often require negative values for other components) and
// HDR intensity (2.0 = 160 nits scene referred)
const f32 testcolors[4][7] = {
    // Red
    {  2.00f,  2.00f, -0.25f, -0.25f, -0.25f,  2.00f,  2.00f},
    // Green
    { -0.25f,  2.00f,  2.00f,  2.00f, -0.25f, -0.25f, -0.25f},
    // Blue
    { -0.25f, -0.25f, -0.25f,  2.00f,  2.00f,  2.00f, -0.25f},
    // Alpha
    {  1.00f,  1.00f,  1.00f,  1.00f,  1.00f,  1.00f,  1.00f}
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
    // Some notes:
    // f32 is 1 sign bit, 8 exponent bits, 23 mantissa bits
    // f16 is 1 sign bit, 5 exponent bits, 10 mantissa bits
    // 1.0 as f32 is 0x3f800000 (exp=127 of 0-255)
    // s0 e01111111 m00000000000000000000000
    // 1.0 as f16 is 0x7800 (exp=15 of 0-31)
    // s0 e...01111 m0000000000.............
    // if we shift the exponents to align the same, 127-15=112, f16 exp is f32
    // exp - 112, since the sign bit precedes it we need to mask that off before
    // adjusting, the mantissa directly follows the exponent so we can shift
    // both by the same amount to align with the f16 format, and subtract 112
    // from the exponent and we get f16 from f32 with bit math alone.
    //
    // e112 = s0 e011100000 m... = 0x38000000
    // e113 = s0 e011100001 m... = 0x38800000
    //
    // We also have to handle the fact that e103 to 112 become denormals, but
    // it is easier to simply treat <=e112 as zero, a lot of float
    // implementations either ignore denormals or process them very slowly so
    // turning them into zero is a reasonable behavior here.
    //
    // Note that this simple shift approach inherently applies round-toward-zero
    // behavior, regardless of the current FPU rounding mode.
    union
    {
        f32 f;
        u32 i;
    }
    u;
    u.f = f;
    u32 i = u.i;
    // Adjust exponent from +127 bias to +15 bias, if it would become less than
    // exponent 1 we treat it as a full zero (rather than try to deal with
    // denormals, which typically have a performance penalty anyway)
    u32 a = ((i & 0x7FFFFFFF) < 0x38800000) ? 0 : i - 0x38000000;
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
