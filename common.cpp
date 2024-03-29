/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// common.cpp : Shared elements between the platforms, such as test scenes

#include <stdlib.h>
#include "common.h"

const f32 testcolors[3][5] = {
    {1.0f, 0.4f, 0.1f, 0.1f, 0.4f},
    {0.4f, 1.0f, 1.0f, 0.4f, 0.1f},
    {0.1f, 0.1f, 0.4f, 1.0f, 1.0f}
};

void TestColors_Gradient_PixelCallback(float output[], f32 x, f32 y, f32 width, f32 height)
{
    constexpr u32 limit = sizeof(testcolors[0]) / sizeof(testcolors[0][0]);
    constexpr u32 limit1 = limit - 1;
    constexpr u32 limit2 = limit - 2;
    f32 f = (x / width) * limit;
    f = f < 0.0f ? 0.0f : f < (float)limit1 ? f : (float)limit;
    u32 i = (int)f;
    i = i < 0 ? 0 : i < limit2 ? i : limit2;
    f32 lerp = f - i;
    f32 ilerp = 1.0f - lerp;
    for (u32 c = 0; c < 3; c++)
        output[c] = testcolors[c][i] * ilerp + testcolors[c][i + 1] * lerp;
    output[3] = 1.0f;
}

static u64 Compositor_Scene_Test_Add_Layer(Compositor_Scene* scene, u64 refreshcounter, u64 z, Compositor_PixelFormat pixelformat, u16 x, u16 y, u16 width, u16 height, void (*pixelcallback)(f32 output[4], f32 x, f32 y, f32 width, f32 height))
{
    Compositor_Scene_Layer* layer = new Compositor_Scene_Layer();
    layer->z = z;
    layer->refreshcounter = refreshcounter;
    layer->pixelformat = pixelformat;
    layer->x = x;
    layer->y = y;
    layer->width = width;
    layer->height = height;
    scene->layers.push_back(layer);
    return scene->layers.size() - 1;
}

void Compositor_Scene_Make_Colorspace_Test(Compositor_Scene* scene)
{
    u64 refreshcounter = 1;
    u16 w = 256;
    u16 h = 32;
    Compositor_Scene_Test_Add_Layer(scene, refreshcounter, 0, Compositor_PixelFormat::srgb, 0, h * 0, w, h, TestColors_Gradient_PixelCallback);
    Compositor_Scene_Test_Add_Layer(scene, refreshcounter, 1, Compositor_PixelFormat::rec709, 0, h * 1, w, h, TestColors_Gradient_PixelCallback);
    Compositor_Scene_Test_Add_Layer(scene, refreshcounter, 2, Compositor_PixelFormat::dcip3, 0, h * 2, w, h, TestColors_Gradient_PixelCallback);
    Compositor_Scene_Test_Add_Layer(scene, refreshcounter, 3, Compositor_PixelFormat::rec2020_8bit, 0, h * 3, w, h, TestColors_Gradient_PixelCallback);
    Compositor_Scene_Test_Add_Layer(scene, refreshcounter, 4, Compositor_PixelFormat::rec2020_10bit, 0, h * 4, w, h, TestColors_Gradient_PixelCallback);
    Compositor_Scene_Test_Add_Layer(scene, refreshcounter, 5, Compositor_PixelFormat::rec2100_10bit, 0, h * 5, w, h, TestColors_Gradient_PixelCallback);
    Compositor_Scene_Test_Add_Layer(scene, refreshcounter, 6, Compositor_PixelFormat::rgba16f_scrgb, 0, h * 6, w, h, TestColors_Gradient_PixelCallback);
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

static void Pixel_Modulate_And_Clamp(f32 c[], f32 scale, f32 low, f32 high)
{
    for (u32 i = 0; i < 32; i++)
    {
        f32 f = c[i];
        f *= scale;
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
    return ((i & 0x8000) >> 16) | ((i & 0x7C000000) >> 13) | ((i & 0x007FE000) >> 13);
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
                layer->pixelcallback(p, x, y, width, height);
            }
        }
        break;
    case Compositor_Format_Flags::FORMAT_RGBA16F:
        for (u16 y = 0; y < height; y++)
        {
            for (u16 x = 0; x < width; x++)
            {
                auto p = (u16*)pixels + 4 *(y * width + x);
                f32 c[4];
                Color_OETF(layer->pixelformat, c, c);
                layer->pixelcallback(c, x, y, width, height);
                p[0] = ToF16(c[0]);
                p[1] = ToF16(c[1]);
                p[2] = ToF16(c[2]);
                p[3] = ToF16(c[3]);
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
                Pixel_Modulate_And_Clamp(c, 1023.0f, 0.0f, 1023.0f);
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
                Pixel_Modulate_And_Clamp(c, 255.0f, 0.0f, 255.0f);
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
                Pixel_Modulate_And_Clamp(c, 255.0f, 0.0f, 255.0f);
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
