/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// testcolorspaces.cpp : Defines the entry point for the application.
//

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dcomp.lib")
#pragma comment(lib, "dxgi.lib")

#ifndef WINVER              // Allow use of features specific to Windows 7 or later.
#define WINVER 0x0A00       // Change this to the appropriate value to target other versions of Windows.
#endif

#ifndef _WIN32_WINNT        // Allow use of features specific to Windows 7 or later.
#define _WIN32_WINNT 0x0A00 // Change this to the appropriate value to target other versions of Windows.
#endif

#ifndef UNICODE
#define UNICODE
#endif

#define WIN32_LEAN_AND_MEAN     // Exclude rarely-used items from Windows headers

#include "Resource.h"

#include <cassert>
#include <chrono>
#include <sstream>
#include <vector>

#include <Windows.h>
#include <d3d11.h>
#include <dcomp.h>
#include <dxgi1_6.h>

// Rust has better names for the regular types.
using i8 = int8_t;
using u8 = uint8_t;
using i16 = int16_t;
using u16 = uint16_t;
using f32 = float;
using i32 = int32_t;
using u32 = uint32_t;
using f64 = double;
using i64 = int64_t;
using u64 = uint64_t;
using usize = size_t;

template <class T> void SafeRelease(T** ppT)
{
    if (*ppT)
    {
        (*ppT)->Release();
        *ppT = NULL;
    }
}

enum class Compositor_Status
{
    No_Device,
    Device_Lost,
    Device_Creation_Failed,
    Running,
};

class Compositor;
class Compositor_Layer
{
public:
    /// Store the layer properties that were used at creation time for
    /// convenience when debugging
    f32 x = 0;
    f32 y = 0;
    u32 width = 0;
    u32 height = 0;
    DXGI_COLOR_SPACE_TYPE dxgiColorspace = DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
    DXGI_FORMAT dxgiFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
    u8 bytesPerPixel;
    bool isWindow;
    bool isSurface;

    /// DirectComposition visual represents the presentation shape (rect) and
    /// various rendering properties
    IDCompositionVisual* dcompvisual = nullptr;
    /// DXGI swap chain is an image flip book of rendered frames, latest one is
    /// displayed when the Present method is called, followed by Commit to
    /// update the DirectComposition scene
    ///
    /// Both of these refer to the same underlying object, but via different
    /// interfaces, it is important that Release is called on swapchain3 before
    /// swapchain1 to avoid a race condition, we can't call Release on
    /// swapchain1 right after QueryInterface.
    /// https://learn.microsoft.com/en-us/windows/win32/api/unknwn/nf-unknwn-iunknown-queryinterface(q)
    IDXGISwapChain1* swapchain1 = nullptr;
    IDXGISwapChain3* swapchain3 = nullptr;
    /// DXGI surface is a single image that retains previous content
    IDXGISurface* surface = nullptr;

    ~Compositor_Layer();
    void UpdateSwapChain(Compositor* comp, IDXGISwapChain1 *swapchain, void* tPixels);
    void WindowWithSwapChain(Compositor* comp, u32 _width, u32 _height, DXGI_COLOR_SPACE_TYPE _type, DXGI_FORMAT _format, u8 _bpp, void* tPixels);
    void VisualWithSwapChain(Compositor* comp, f32 _x, f32 _y, u32 _width, u32 _height, DXGI_COLOR_SPACE_TYPE _type, DXGI_FORMAT _format, u8 _bpp, void* tPixels);
    void VisualWithSurface(Compositor* comp, f32 _x, f32 _y, u32 _width, u32 _height, DXGI_COLOR_SPACE_TYPE _type, DXGI_FORMAT _format, u8 _bpp, void* tPixels);
};

Compositor_Layer::~Compositor_Layer()
{
    // This crashes for some reason
    // SafeRelease(&swapchain3);
    SafeRelease(&swapchain1);
    SafeRelease(&surface);
    // This crashes for some reason
    // SafeRelease(&dcompvisual);
}

class Compositor
{
public:
    Compositor_Status status = Compositor_Status::No_Device;
    HWND hWindow = nullptr;
    f32 scale = 1.0f;
    ID3D11Device* d3d = nullptr;
    IDXGIDevice* dxgi = nullptr;
    IDXGIAdapter* adapter = nullptr;
    IDXGIFactory7* factory = nullptr;
    IDXGISwapChain1* windowswapchain1 = nullptr;
    IDCompositionDevice* dcomp = nullptr;
    IDCompositionTarget* dcomptarget = nullptr;
    ID3D11DeviceContext* context = nullptr;
    IDCompositionVisual* rootvisual = nullptr;

    // Currently active layers
    std::vector<Compositor_Layer> layers;

    ~Compositor();
    void UpdateStatus();
    void DestroyDevice();
    void CreateDevice(HWND hWnd);
    void CreateScene();
    void Update(HWND hWnd, bool reset);
};

void Compositor::DestroyDevice()
{
    status = Compositor_Status::No_Device;
    layers.clear();
    if (rootvisual) {
        rootvisual->RemoveAllVisuals();
    }
    SafeRelease(&windowswapchain1);
    SafeRelease(&rootvisual);
    SafeRelease(&adapter);
    SafeRelease(&factory);
    SafeRelease(&dxgi);
    SafeRelease(&dcomp);
    SafeRelease(&dcomptarget);
    SafeRelease(&context);
    SafeRelease(&d3d);
}

void Compositor::UpdateStatus()
{
    if (dcomp) {
        BOOL bIsValid = FALSE;
        HRESULT res = dcomp->CheckDeviceState(&bIsValid);
        if (res == S_OK && bIsValid) {
            status = Compositor_Status::Running;
        } else {
            status = Compositor_Status::Device_Lost;
        }
    } else {
        status = Compositor_Status::No_Device;
    }
}

void Compositor::CreateDevice(HWND hWnd)
{
    // Assume device creation failed if this function exits early.
    status = Compositor_Status::Device_Creation_Failed;
    hWindow = hWnd;

    // Get the current DPI of the display the window is on
    scale = 1.0f;
    HDC hdc = GetDC(hWindow);
    if (hdc)
    {
        scale = GetDeviceCaps(hdc, LOGPIXELSX) / 96.0f;
        scale = scale < 1.0f / 1024.0f ? 1.0f / 1024.0f : scale < 1024.0f ? scale : 1024.0f;
        ReleaseDC(hWindow, hdc);
    }

    u32 flags = 0;
    flags |= D3D11_CREATE_DEVICE_DEBUG;

    constexpr D3D_FEATURE_LEVEL feature_levels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };

    D3D_FEATURE_LEVEL featureLevelSupported = feature_levels[0];

    HRESULT hr;
    hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        NULL,
        flags,
        feature_levels,
        sizeof(feature_levels) / sizeof(feature_levels[0]),
        D3D11_SDK_VERSION,
        &d3d,
        &featureLevelSupported,
        nullptr);
    assert(SUCCEEDED(hr));
    if (FAILED(hr)) {
        return;
    }

    hr = d3d->QueryInterface(&dxgi);
    assert(SUCCEEDED(hr));
    if (FAILED(hr)) {
        return;
    }

    hr = DCompositionCreateDevice(
        dxgi,
        __uuidof(IDCompositionDevice),
        reinterpret_cast<void**>(&dcomp));
    assert(SUCCEEDED(hr));
    if (FAILED(hr)) {
        return;
    }

    hr = dcomp->CreateTargetForHwnd(hWindow, TRUE, &dcomptarget);
    assert(SUCCEEDED(hr));
    if (FAILED(hr)) {
        return;
    }

    hr = dcomp->CreateVisual(&rootvisual);
    assert(SUCCEEDED(hr));
    if (FAILED(hr)) {
        return;
    }

    hr = dxgi->GetAdapter(&adapter);
    assert(SUCCEEDED(hr));
    if (FAILED(hr)) {
        return;
    }

    // COM is a bit confusing here - CreateDXGIFactory2 simply adds a flags
    // parameter, the 2 in the name is only the API version of CreateDXGIFactory
    // and entirely unrelated to which DXGIFactory version we are requesting
    hr = CreateDXGIFactory2(0u, __uuidof(IDXGIFactory7), reinterpret_cast<void**>(&factory));
    assert(SUCCEEDED(hr));
    if (FAILED(hr)) {
        return;
    }

#if !ONLY_DCOMP
    DXGI_SWAP_CHAIN_DESC1 scDesc = {};
    scDesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    scDesc.BufferCount = 2;
    scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_BACK_BUFFER | DXGI_USAGE_SHADER_INPUT;
    scDesc.Flags = 0;
    scDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    scDesc.SampleDesc.Count = 1;
    scDesc.Scaling = DXGI_SCALING_STRETCH;
    scDesc.Stereo = FALSE;
    scDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    hr = factory->CreateSwapChainForHwnd(
        d3d,
        hWnd,
        &scDesc,
        nullptr,
        nullptr,
        &windowswapchain1
    );
    assert(SUCCEEDED(hr));
#endif

    d3d->GetImmediateContext(&context);

    status = Compositor_Status::Running;
}

void Compositor_Layer::UpdateSwapChain(Compositor* comp, IDXGISwapChain1 *swapchain, void* tPixels)
{
    DXGI_SWAP_CHAIN_DESC1 scDesc = {};
    swapchain->GetDesc1(&scDesc);
    ID3D11Resource* buffer = nullptr;
    ID3D11RenderTargetView* view = nullptr;
    ID3D11Texture2D* tex = nullptr;
    D3D11_TEXTURE2D_DESC tDesc;
    tDesc.Width = scDesc.Width;
    tDesc.Height = scDesc.Height;
    tDesc.MipLevels = 1;
    tDesc.ArraySize = 1;
    tDesc.Format = dxgiFormat;
    u8 bpp = bytesPerPixel;
    // tDesc uses UINT, so let's check for overflow first.
    u64 tPitch = tDesc.Width * bpp;
    u64 tSlicePitch = tPitch * tDesc.Height;
    if (tPitch > UINT_MAX || tSlicePitch > UINT_MAX)
    {
        assert(false);
        return;
    }
    tDesc.SampleDesc.Count = 1;
    tDesc.SampleDesc.Quality = 0;
    tDesc.Usage = D3D11_USAGE_DEFAULT;
    tDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    tDesc.CPUAccessFlags = 0;
    tDesc.MiscFlags = 0;

    HRESULT hr;
    hr = swapchain->GetBuffer(0, IID_PPV_ARGS(&buffer));
    assert(SUCCEEDED(hr));
    if (SUCCEEDED(hr) && buffer)
    {
#if !USE_RENDERTARGETVIEW
        // Just copy pixels into the backbuffer
        D3D11_BOX texBox;
        texBox.left = 0;
        texBox.right = scDesc.Width;
        texBox.top = 0;
        texBox.bottom = scDesc.Height;
        texBox.front = 0;
        texBox.back = 1;
        comp->context->UpdateSubresource(buffer, 0, &texBox, tPixels, static_cast<UINT>(tPitch), static_cast<UINT>(tSlicePitch));
#else
        // Create a texture to hold the pixels, and set up a shader to
        // copy that into the backbuffer
        hr = comp->d3d->CreateRenderTargetView(buffer, NULL, &view);
        if (SUCCEEDED(hr) && view)
        {
            f32 debugColor[] = { 1.0f, 0.0f, 0.0f, 0.5f };
            comp->context->ClearRenderTargetView(view, debugColor);

            D3D11_SUBRESOURCE_DATA tInitData;
            tInitData.SysMemPitch = static_cast<UINT>(tPitch);
            tInitData.SysMemSlicePitch = static_cast<UINT>(tSlicePitch);
            tInitData.pSysMem = tPixels;
            hr = comp->d3d->CreateTexture2D(&tDesc, &tInitData, &tex);
            if (SUCCEEDED(hr) && tex)
            {
                D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
                srvDesc.Format = tDesc.Format;
                srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                srvDesc.Texture2D.MipLevels = 1;
                ID3D11ShaderResourceView* srv = nullptr;
                hr = comp->d3d->CreateShaderResourceView(tex, &srvDesc, &srv);
                if (SUCCEEDED(hr) && srv)
                {
                    // TODO
                }
            }
        }
#endif
    }

    if (tex)
        tex->Release();
    if (view)
        view->Release();
    if (buffer)
        buffer->Release();

    // Flip the backbuffer to front
#if USE_ALLOW_TEARING
    hr = swapchain->Present1(0, DXGI_PRESENT_ALLOW_TEARING, nullptr);
#else
    hr = swapchain->Present(0, 0);
#endif
    assert(SUCCEEDED(hr));
}

void Compositor_Layer::WindowWithSwapChain(Compositor* comp, u32 _width, u32 _height, DXGI_COLOR_SPACE_TYPE _type, DXGI_FORMAT _format, u8 _bpp, void* tPixels)
{
    x = 0;
    y = 0;
    width = _width;
    height = _height;
    dxgiColorspace = _type;
    dxgiFormat = _format;
    bytesPerPixel = _bpp;
    isSurface = false;
    isWindow = true;
    UpdateSwapChain(comp, comp->windowswapchain1, tPixels);
}

void Compositor_Layer::VisualWithSwapChain(Compositor* comp, f32 _x, f32 _y, u32 _width, u32 _height, DXGI_COLOR_SPACE_TYPE _type, DXGI_FORMAT _format, u8 _bpp, void *tPixels)
{
    x = _x;
    y = _y;
    width = _width;
    height = _height;
    dxgiColorspace = _type;
    dxgiFormat = _format;
    bytesPerPixel = _bpp;
    isSurface = false;
    isWindow = false;

    if (!dcompvisual) {
        HRESULT hr = comp->dcomp->CreateVisual(&dcompvisual);
        assert(SUCCEEDED(hr));
        if (FAILED(hr))
            return;
    }
    dcompvisual->SetOffsetX(x);
    dcompvisual->SetOffsetY(y);

    if (!swapchain1) {
        DXGI_SWAP_CHAIN_DESC1 scDesc = {};
        scDesc.Format = dxgiFormat;
        scDesc.SampleDesc.Count = 1;
        scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_BACK_BUFFER | DXGI_USAGE_SHADER_INPUT;
        scDesc.BufferCount = 2;
        scDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        scDesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        scDesc.Width = static_cast<u32>(width);
        scDesc.Height = static_cast<u32>(height);
        scDesc.Stereo = FALSE;
        scDesc.Scaling = DXGI_SCALING_STRETCH;
        // DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT requires Windows
        // 8.1 or later, and gives us access to the
        // GetFrameLatencyWaitableObject method so we can use
        // WaitForSingleObjectEx to wait for the frame to be shown on the
        // display, rather than queuing D3D commands to a limit.
        //
        // DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING is required for support of
        // variable refresh rate displays, and requires Windows 10 1511, we
        // could check if this is supported but a Windows 10 version from 2016
        // is well past EOL so this code doesn't bother checking.
        // https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/variable-refresh-rate-displays
        scDesc.Flags = 0;
        HRESULT hr = comp->factory->CreateSwapChainForComposition(comp->d3d, &scDesc, NULL, &swapchain1);
        assert(SUCCEEDED(hr));
        if (FAILED(hr)) {
            return;
        }
        // Convert the IDXGISwapChain1 to IDXGISwapChain3 because we need a few
        // more features to do HDR stuff, such as specifying colorspace for
        // HDR10 (scRGB doesn't need the hint because the scDesc.Format being
        // R16F16B16A16_FLOAT tells dcomp it is scRGB in that case)
        hr = swapchain1->QueryInterface(__uuidof(IDXGISwapChain3), reinterpret_cast<void**>(&swapchain3));
        assert(SUCCEEDED(hr));
        if (FAILED(hr)) {
            return;
        }
        swapchain3->SetColorSpace1(dxgiColorspace);
        dcompvisual->SetContent(swapchain3);
    }

    // Render a new frame in the swapchain and present it
    if (swapchain1 && width >= 1 && height >= 1)
        UpdateSwapChain(comp, swapchain1, tPixels);
}

void Compositor_Layer::VisualWithSurface(Compositor* comp, f32 _x, f32 _y, u32 _width, u32 _height, DXGI_COLOR_SPACE_TYPE _type, DXGI_FORMAT _format, u8 _bpp, void* tPixels)
{
    x = _x;
    y = _y;
    width = _width;
    height = _height;
    dxgiColorspace = _type;
    dxgiFormat = _format;
    bytesPerPixel = _bpp;
    isSurface = true;
    isWindow = false;

    if (!dcompvisual) {
        HRESULT hr = comp->dcomp->CreateVisual(&dcompvisual);
        assert(SUCCEEDED(hr));
        if (FAILED(hr))
            return;
    }
    dcompvisual->SetOffsetX(x);
    dcompvisual->SetOffsetY(y);

    // TODO
}


void Compositor::Update(HWND hWnd, bool reset)
{
    // If an error is encountered, we reinitialize the device and try again, but
    // only once, if the device is lost repeatedly we're not going to make
    // progress, so two attempts is probably optimal
    for (i32 tries = 2; tries >= 0; tries--)
    {
        UpdateStatus();
        if (status == Compositor_Status::Running) {
            if (hWindow == hWnd && !reset) {
                return;
            }
            break;
        }
        DestroyDevice();
        CreateDevice(hWnd);
    }

    CreateScene();

    // Add the layer visuals to our root visual as they must form a tree
    for (auto layer : layers) {
        if (layer.dcompvisual) {
            HRESULT hr = rootvisual->AddVisual(layer.dcompvisual, TRUE, nullptr);
            assert(SUCCEEDED(hr));
        }
    }
    // Commit transaction so it displays the new layers
    dcomp->Commit();
}

Compositor::~Compositor()
{
    DestroyDevice();
}

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
/// toward zero behavior, which may not be the active floating point mode).
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

void GenerateImage_RGBA16F_scRGB(u16* pixels, u16 width, u16 height)
{
    for (u16 y = 0; y < height; y++)
    {
        for (u16 x = 0; x < width; x++)
        {
            auto p = (u16*)pixels + 4 * (y * width + x);
            f32 c[4];
            TestColors_Gradient_PixelCallback(c, x, y, width, height);
            for (u16 i = 0; i < 4; i++)
                p[i] = (u16)ToF16(c[i]);
        }
    }
}

void Color_OETF_PQ(f32 c[], f32 o[])
{
    constexpr auto m1 = 2610.0f / 16384.0f;
    constexpr auto m2 = 128.0f * 2523.0f / 4096.0f;
    constexpr auto c1 = 3424.0f / 4096.0f;
    constexpr auto c2 = 32.0f * 2413.0f / 4096.0f;
    constexpr auto c3 = 32.0f * 2392.0f / 4096.0f;
    for (u32 i = 0; i < 3; i++)
    {
        f32 j = powf(c[i] / 10000.0f, m1);
        f32 f = ((c1 + c2 * j) / (1.0f + c3 * j)) * m2;
        o[i] = f < 0.0f ? 0.0f : f < 1.0f ? f : 1.0f;
    }
    o[3] = c[3];
}

void GenerateImage_RGB10A2_Rec2020_PQ(u32* pixels, u16 width, u16 height)
{
    for (u16 y = 0; y < height; y++)
    {
        for (u16 x = 0; x < width; x++)
        {
            auto p = (u32*)pixels + y * width + x;
            f32 c[4];
            TestColors_Gradient_PixelCallback(c, x, y, width, height);
            Color_OETF_PQ(c, c);
            Pixel_To_Int(c, 1023.0f, 0.0f, 1023.0f);
            *p =
                (u32)c[0] * 0x1 +
                (u32)c[1] * 0x400 +
                (u32)c[2] * 0x100000 +
                ((u32)c[3] >> 8) * 0xC0000000;
        }
    }
}

void Color_OETF_sRGB(f32 c[], f32 o[])
{
    // sRGB piecewise gamma
    for (u32 i = 0; i < 3; i++)
    {
        f32 f = c[i];
        f = f < 0.0f ? 0.0f : f < 1.0f ? f : 1.0f;
        o[i] = f < 0.0031308f ? f * 12.92f : 1.055f * powf(f, 0.41666f) - 0.055f;
    }
    o[3] = c[3];
}

void GenerateImage_BGRA8_sRGB(u32* pixels, u16 width, u16 height)
{
    // 8bit sRGB or rec709 (Windows doesn't distinguish between them)
    for (u16 y = 0; y < height; y++)
    {
        for (u16 x = 0; x < width; x++)
        {
            auto p = pixels + y * width + x;
            f32 c[4];
            TestColors_Gradient_PixelCallback(c, x, y, width, height);
            Color_OETF_sRGB(c, c);
            Pixel_To_Int(c, 255.0f, 0.0f, 255.0f);
            *p =
                (u32)c[2] * 0x1 +
                (u32)c[1] * 0x100 +
                (u32)c[0] * 0x10000 +
                (u32)c[3] * 0x1000000;
        }
    }
}

void Compositor::CreateScene()
{
    layers.clear();
    layers.reserve(16);

    // Get the window swapchain size
    DXGI_SWAP_CHAIN_DESC1 scDesc = {};
    windowswapchain1->GetDesc1(&scDesc);
    u32 windowWidth = static_cast<u16>(scDesc.Width);
    u32 windowHeight = static_cast<u16>(scDesc.Height);
    windowWidth = windowWidth < 1 ? 1 : windowWidth < 16384 ? windowWidth : 32;
    windowHeight = windowHeight < 1 ? 1 : windowHeight < 16384 ? windowHeight : 32;

    // Put a gradient image behind everything
    layers.push_back(Compositor_Layer());
    auto pixelsWindow = new u16[4 * windowWidth * windowHeight];
    GenerateImage_RGBA16F_scRGB(pixelsWindow, windowWidth, windowHeight);
    layers[layers.size() - 1].WindowWithSwapChain(this, windowWidth, windowHeight, DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709, DXGI_FORMAT_R16G16B16A16_FLOAT, 8, (void*)pixelsWindow);

    u32 w = (u32)(256.0f * scale);
    u32 h = (u32)(32.0f * scale);
    w = w < 1 ? 1 : w < 16384 ? w : 16384;
    h = h < 1 ? 1 : h < 16384 ? h : 16384;
    auto pixelsSCRGB16F = new u16[4 * w * h];
    GenerateImage_RGBA16F_scRGB(pixelsSCRGB16F, w, h);
    auto pixelsRec2020PQ = new u32[w * h];
    GenerateImage_RGB10A2_Rec2020_PQ(pixelsRec2020PQ, w, h);
    auto pixelsSRGB8 = new u32[w * h];
    GenerateImage_BGRA8_sRGB(pixelsSRGB8, w, h);

    f32 fw = (f32)w;
    f32 fh = (f32)h;
    f32 grid_w = fw + 4 * scale;
    f32 grid_h = fh + 4 * scale;
    f32 x = 32 * scale;
    f32 y = 32 * scale;

    // Add some smaller gradients as visuals using swapchains in various formats
    layers.push_back(Compositor_Layer());
    layers[layers.size() - 1].VisualWithSwapChain(this, x, y, w, h, DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709, DXGI_FORMAT_R16G16B16A16_FLOAT, 8, (void*)pixelsSCRGB16F);
    y += grid_h;
    layers.push_back(Compositor_Layer());
    layers[layers.size() - 1].VisualWithSwapChain(this, x, y, w, h, DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020, DXGI_FORMAT_R10G10B10A2_UNORM, 4, (void*)pixelsRec2020PQ);
    y += grid_h;
    layers.push_back(Compositor_Layer());
    layers[layers.size() - 1].VisualWithSwapChain(this, x, y, w, h, DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709, DXGI_FORMAT_R8G8B8A8_UNORM, 4, (void*)pixelsSRGB8);
    y += grid_h;

    // Add some smaller gradients as visuals using surfaces in various formats
    x += grid_w;
    y = 32 * scale;
    layers.push_back(Compositor_Layer());
    layers[layers.size() - 1].VisualWithSurface(this, x, y, w, h, DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709, DXGI_FORMAT_R16G16B16A16_FLOAT, 8, (void*)pixelsSCRGB16F);
    y += grid_h;
    layers.push_back(Compositor_Layer());
    layers[layers.size() - 1].VisualWithSurface(this, x, y, w, h, DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020, DXGI_FORMAT_R10G10B10A2_UNORM, 4, (void*)pixelsRec2020PQ);
    y += grid_h;
    layers.push_back(Compositor_Layer());
    layers[layers.size() - 1].VisualWithSurface(this, x, y, w, h, DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709, DXGI_FORMAT_R8G8B8A8_UNORM, 4, (void*)pixelsSRGB8);
    y += grid_h;
}

#define MAX_LOADSTRING 100

// Global Variables:
bool quit;
HINSTANCE hInst;                                // current instance
HWND hWindow;
u32 win_dpiX;
u32 win_dpiY;
WCHAR szTitle[MAX_LOADSTRING];                  // The title bar text
WCHAR szWindowClass[MAX_LOADSTRING];            // the main window class name
static Compositor* compositor;

// Forward declarations of functions included in this code module:
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    // TODO: Place code here.

    // Initialize global strings
    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_TESTCOLORSPACES, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);

    // Perform application initialization:
    if (!InitInstance (hInstance, nCmdShow))
    {
        return FALSE;
    }

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_TESTCOLORSPACES));

    MSG msg;

    // Main message loop:
    quit = false;
    compositor->Update(hWindow, true);
    for (;;)
    {
        while (PeekMessage(&msg, nullptr, 0, 0, TRUE))
        {
            if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
            {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }
        if (quit)
            break;
        // Refresh the scene no faster than 60hz, we don't actually use any
        // wait timeouts, so this is the only way we're yielding CPU
        Sleep(16);
        compositor->Update(hWindow, false);
    }

    // Shutdown
    compositor->DestroyDevice();
    delete compositor;
    compositor = nullptr;

    return (int) msg.wParam;
}



//
//  FUNCTION: MyRegisterClass()
//
//  PURPOSE: Registers the window class.
//
ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style          = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = WndProc;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = hInstance;
    wcex.hIcon          = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_TESTCOLORSPACES));
    wcex.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground  = (HBRUSH)(COLOR_WINDOW+1);
    wcex.lpszMenuName   = MAKEINTRESOURCEW(IDC_TESTCOLORSPACES);
    wcex.lpszClassName  = szWindowClass;
    wcex.hIconSm        = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassExW(&wcex);
}

//
//   FUNCTION: InitInstance(HINSTANCE, int)
//
//   PURPOSE: Saves instance handle and creates main window
//
//   COMMENTS:
//
//        In this function, we save the instance handle in a global variable and
//        create and display the main program window.
//
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
   hInst = hInstance; // Store instance handle in our global variable

   win_dpiX = 96;
   win_dpiY = 96;
   HDC hdc = GetDC(NULL);
   if (hdc)
   {
       win_dpiX = GetDeviceCaps(hdc, LOGPIXELSX);
       win_dpiY = GetDeviceCaps(hdc, LOGPIXELSY);
       ReleaseDC(NULL, hdc);
   }
   u32 width = static_cast<u32>(ceil(640.0f * win_dpiX / 96.f));
   u32 height = static_cast<u32>(ceil(480.0f * win_dpiY / 96.f));

   HWND hWnd = CreateWindowW(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT, 0, width, height, nullptr, nullptr, hInstance, nullptr);

   if (!hWnd)
      return FALSE;

   hWindow = hWnd;
   ShowWindow(hWnd, nCmdShow);
   UpdateWindow(hWnd);

   compositor = new Compositor();
   compositor->hWindow = hWnd;

   return TRUE;
}

//
//  FUNCTION: WndProc(HWND, UINT, WPARAM, LPARAM)
//
//  PURPOSE: Processes messages for the main window.
//
//  WM_COMMAND  - process the application menu
//  WM_PAINT    - Paint the main window
//  WM_DESTROY  - post a quit message and return
//
//
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_COMMAND:
        {
            int wmId = LOWORD(wParam);
            // Parse the menu selections:
            switch (wmId)
            {
            case IDM_ABOUT:
                DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
                break;
            case IDM_EXIT:
                quit = true;
                DestroyWindow(hWnd);
                break;
            default:
                return DefWindowProc(hWnd, message, wParam, lParam);
            }
        }
        break;
    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            // TODO: Add any drawing code that uses hdc here...
            EndPaint(hWnd, &ps);
        }
        break;
    case WM_CLOSE:
        quit = true;
        DestroyWindow(hWnd);
        break;
    case WM_DESTROY:
        quit = true;
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

// Message handler for about box.
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}
