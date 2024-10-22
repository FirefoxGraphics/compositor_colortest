/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// testcolorspaces.cpp : Defines the entry point for the application.
//

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dcomp.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "gdiplus.lib")

#include "common.h"

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

#include "framework.h"
#include "Resource.h"

#include <array>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>      // std::setprecision
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <unordered_map>
#include <variant>
#include <vector>

using std::move;
using std::vector;
using std::string;
using std::unique_ptr;
using std::shared_ptr;
namespace chrono = std::chrono;

#include <Windows.h>
#include <d3d11.h>
#include <dcomp.h>
#include <dxgi1_6.h>
#include <gdiplus.h>
#include <KnownFolders.h>
#include <ShlObj.h>

template <class T> void SafeRelease(T** ppT)
{
    if (*ppT)
    {
        (*ppT)->Release();
        *ppT = NULL;
    }
}

DXGI_FORMAT Compositor_DXGIFormatForPixelFormat(Compositor_PixelFormat pixelformat, bool sRGBHint)
{
    switch (pixelformat & Compositor_Format_Flags::FORMAT_MASK)
    {
    case Compositor_Format_Flags::FORMAT_RGBA32F:
        return DXGI_FORMAT_R32G32B32A32_FLOAT;
    case Compositor_Format_Flags::FORMAT_RGBA16F:
        return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case Compositor_Format_Flags::FORMAT_RGB10A2:
        return DXGI_FORMAT_R10G10B10A2_UNORM;
    case Compositor_Format_Flags::FORMAT_RGBA8:
        return sRGBHint ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
    case Compositor_Format_Flags::FORMAT_BGRA8:
        return sRGBHint ? DXGI_FORMAT_B8G8R8A8_UNORM_SRGB : DXGI_FORMAT_B8G8R8A8_UNORM;
    default:
        // Unknown pixel format - update this code if you encounter this
        assert(false);
        return DXGI_FORMAT_R32G32B32A32_FLOAT;
    }
}

DXGI_COLOR_SPACE_TYPE Compositor_DXGIColorSpaceForPixelFormat(Compositor_PixelFormat pixelformat)
{
    switch (pixelformat)
    {
    case Compositor_PixelFormat::srgb: return DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
    case Compositor_PixelFormat::rec709: return DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
    case Compositor_PixelFormat::rec2020_10bit: return DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P2020;
    case Compositor_PixelFormat::rec2020_8bit: return DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P2020;
    case Compositor_PixelFormat::rgba16f_scrgb: return DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
    case Compositor_PixelFormat::rgba32f_scrgb: return DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
    default:
        // The following are unsupported by DirectComposition.
    case Compositor_PixelFormat::dcip3:
        assert(false);
        return DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
    }
}


struct Compositor_Scene_Platform_Layer
{
    /// Cross-platform scene layer definition
    Compositor_Scene_Layer base;
    /// Used internally for deleting layers not found in a new scene
    bool expired = true;
    /// DirectComposition visual represents the presentation shape (rect) and
    /// various rendering properties
    IDCompositionVisual* dcompvisual = nullptr;
    /// Direct3D swap chain is an image flip book of rendered frames, latest one
    /// is displayed when the Present method is called, followed by Commit to
    /// update the DirectComposition scene
    IDXGISwapChain3* swapchain = nullptr;
    /// Direct3D swap chain object we can wait on (using WaitForSingleObjectEx)
    /// to ensure we don't submit more frames when one is already in the queue.
    HANDLE swapchain_waitable_object = nullptr;
};

struct Compositor_Scene_Platform
{
    /// Platform-specific layer state based on the provided scene's layers
    std::vector<Compositor_Scene_Platform_Layer*> layers;
};

struct Compositor_State
{
    Compositor_Status status = Compositor_Status_No_Device;
    HWND hWindow = nullptr;
    u32 dpiX = 96;
    u32 dpiY = 96;
    ID3D11Device* d3d = nullptr;
    IDXGIDevice* dxgi = nullptr;
    IDXGIAdapter* adapter = nullptr;
    IDXGIFactory7* factory = nullptr;
    IDXGISwapChain1* swapchain = nullptr;
    IDCompositionDevice* dcomp = nullptr;
    IDCompositionTarget* dcomptarget = nullptr;
    ID3D11DeviceContext* context = nullptr;
    IDCompositionVisual* rootvisual = nullptr;

    bool visuals_changed = true;

    // Scene state in our native representation
    Compositor_Scene_Platform scene;
};

bool Compositor_Update(Compositor_State* comp, const Compositor_Scene* scene, HWND hWnd);

void Compositor_UncreateDevice(Compositor_State* comp, HWND hWnd)
{
    // Clear the scene first, this will release layer-related resources
    Compositor_Scene scene = Compositor_Scene();
    Compositor_Update(comp, &scene, hWnd);

    SafeRelease(&comp->swapchain);
    SafeRelease(&comp->rootvisual);
    SafeRelease(&comp->adapter);
    SafeRelease(&comp->factory);
    SafeRelease(&comp->dxgi);
    SafeRelease(&comp->dcomp);
    SafeRelease(&comp->dcomptarget);
    SafeRelease(&comp->context);
    SafeRelease(&comp->d3d);
    comp->status = Compositor_Status_No_Device;
}

void Compositor_CreateDevice(Compositor_State* comp, HWND hWnd)
{
    switch (comp->status)
    {
    case Compositor_Status_Running:
        return;
    default:
        break;
    }

    // Start by destroying the previous device if any
    Compositor_UncreateDevice(comp, hWnd);

    comp->status = Compositor_Status_Device_Creation_Failed;

    // Get the current DPI of the main display (ideally we'd get the one the
    // window center is sitting on though)
    comp->dpiX = 96;
    comp->dpiY = 96;
    HDC hdc = GetDC(NULL);
    if (hdc)
    {
        comp->dpiX = GetDeviceCaps(hdc, LOGPIXELSX);
        comp->dpiY = GetDeviceCaps(hdc, LOGPIXELSY);
        ReleaseDC(NULL, hdc);
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
        &comp->d3d,
        &featureLevelSupported,
        nullptr);
    if (FAILED(hr))
        return;

    hr = comp->d3d->QueryInterface(&comp->dxgi);
    if (FAILED(hr))
        return;

    hr = DCompositionCreateDevice(
        comp->dxgi,
        __uuidof(IDCompositionDevice),
        reinterpret_cast<void**>(&comp->dcomp));
    if (FAILED(hr))
        return;

    hr = comp->dcomp->CreateTargetForHwnd(comp->hWindow, TRUE, &comp->dcomptarget);
    if (FAILED(hr))
        return;

    hr = comp->dcomp->CreateVisual(&comp->rootvisual);
    if (FAILED(hr))
        return;

    hr = comp->dxgi->GetAdapter(&comp->adapter);
    if (FAILED(hr))
        return;

    // COM is a bit confusing here - CreateDXGIFactory2 simply adds a flags
    // parameter, the 2 in the name is only the API version of CreateDXGIFactory
    // and entirely unrelated to which DXGIFactory version we are requesting
    hr = CreateDXGIFactory2(0u, __uuidof(IDXGIFactory7), reinterpret_cast<void**>(&comp->factory));
    if (FAILED(hr))
        return;

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
    hr = comp->factory->CreateSwapChainForHwnd(
        comp->d3d,
        hWnd,
        &scDesc,
        nullptr,
        nullptr,
        &comp->swapchain
    );
#endif

    comp->d3d->GetImmediateContext(&comp->context);

    comp->status = Compositor_Status_Running;
}

void Compositor_CheckDeviceState(Compositor_State* comp, HWND hWnd)
{
    if (comp->status != Compositor_Status_Running)
        Compositor_CreateDevice(comp, hWnd);
    else if (comp->dcomp)
    {
        BOOL bIsValid = FALSE;
        HRESULT res = comp->dcomp->CheckDeviceState(&bIsValid);
        if (res != S_OK || !bIsValid)
        {
            comp->status = Compositor_Status_Device_Lost;
            Compositor_CreateDevice(comp, hWnd);
        }
    }
    else
        Compositor_CreateDevice(comp, hWnd);
}

static void Compositor_Reset_Layer(Compositor_State* comp, Compositor_Scene_Platform_Layer& l)
{
    // prepare existing layer for deletion, which will be done by the caller
    l.base = Compositor_Scene_Layer();
    if (l.swapchain)
    {
        comp->visuals_changed = true;
        SafeRelease(&l.swapchain);
    }
    if (l.dcompvisual)
    {
        comp->visuals_changed = true;
        SafeRelease(&l.dcompvisual);
    }
}

static bool Compositor_Update_Layer(Compositor_State* comp, Compositor_Scene_Platform_Layer& l, const Compositor_Scene_Layer& layer)
{
    l.expired = false;

#if NOT_ALWAYS_ANIMATED
    if (l.base.refreshcounter == layer.refreshcounter)
        return true;
#endif

    // Project the 96 DPI scene layer to the display DPI
    u32 x = layer.x * comp->dpiX / 96;
    u32 y = layer.y * comp->dpiY / 96;
    u32 width = layer.width * comp->dpiX / 96;
    u32 height = layer.height * comp->dpiY / 96;

    // If this layer represents the whole window, update rect to match swapchain
    if (l.base.wholewindow)
    {
        DXGI_SWAP_CHAIN_DESC1 scDesc = {};
        comp->swapchain->GetDesc1(&scDesc);
        x = 0;
        y = 0;
        width = scDesc.Width;
        height = scDesc.Height;
    }

    if ((!l.base.wholewindow && (!l.dcompvisual || !l.swapchain)) || l.base.width != width || l.base.height != height || l.base.pixelformat != layer.pixelformat || l.base.pixelcallback != layer.pixelcallback)
    {
        comp->visuals_changed = true;
        if (l.swapchain)
            SafeRelease(&l.swapchain);
        if (l.dcompvisual)
            SafeRelease(&l.dcompvisual);
        l.base = layer;
        l.base.x = x;
        l.base.y = y;
        l.base.width = width;
        l.base.height = height;
    }

    if (!l.dcompvisual && !l.base.wholewindow)
    {
        HRESULT hr = comp->dcomp->CreateVisual(&l.dcompvisual);
        if (FAILED(hr))
            return false;
        l.dcompvisual->SetOffsetX(l.base.x);
        l.dcompvisual->SetOffsetY(l.base.y);
    }

    if (l.dcompvisual && l.base.x != x)
    {
        comp->visuals_changed = true;
        l.base.x = x;
        l.dcompvisual->SetOffsetX(l.base.x);
    }

    if (l.dcompvisual && l.base.y != y)
    {
        comp->visuals_changed = true;
        l.base.y = y;
        l.dcompvisual->SetOffsetY(l.base.y);
    }

    if (l.dcompvisual && !l.swapchain)
    {
        DXGI_SWAP_CHAIN_DESC1 scDesc = {};
        scDesc.Format = Compositor_DXGIFormatForPixelFormat(l.base.pixelformat, false);
        scDesc.SampleDesc.Count = 1;
        scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_BACK_BUFFER | DXGI_USAGE_SHADER_INPUT;
        scDesc.BufferCount = 2;
        scDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        scDesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        scDesc.Width = width;
        scDesc.Height = height;
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
#if USE_FRAME_LATENCY_WAITABLE_OBJECT
        scdesc.Flags |= DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
#endif
#if USE_ALLOW_TEARING
        scdesc.Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
#endif
        IDXGISwapChain1* swapchain1 = nullptr;
        HRESULT hr = comp->factory->CreateSwapChainForComposition(comp->d3d, &scDesc, NULL, &swapchain1);
        if (FAILED(hr))
            return false;
        // Convert the IDXGISwapChain1 to IDXGISwapChain3 because we need a few
        // more features to do HDR stuff, such as specifying colorspace for
        // HDR10 (scRGB doesn't need the hint because the scDesc.Format being
        // R16F16B16A16_FLOAT tells dcomp it is scRGB in that case)
        hr = swapchain1->QueryInterface(__uuidof(IDXGISwapChain3), reinterpret_cast<void**>(&l.swapchain));
        swapchain1->Release();
        if (FAILED(hr))
            return false;
#if USE_FRAME_LATENCY_WAITABLE_OBJECT
        l.swapchain_waitable_object = l.swapchain->GetFrameLatencyWaitableObject();
#endif
        l.swapchain->SetColorSpace1(Compositor_DXGIColorSpaceForPixelFormat(l.base.pixelformat));
        l.dcompvisual->SetContent(l.swapchain);
    }

    // Render a new frame in the swapchain and present it
    IDXGISwapChain1* swapchain = l.base.wholewindow ? comp->swapchain : l.swapchain;
    HANDLE swapchain_waitable_object = l.base.wholewindow ? nullptr : l.swapchain_waitable_object;
    if (swapchain && width > 0 && height > 0)
    {
        // If the swapchain isn't ready for a new frame, we just skip the render
        DWORD wait_result = swapchain_waitable_object ? WaitForSingleObjectEx(swapchain_waitable_object, 0, FALSE) : WAIT_IO_COMPLETION;
        if (wait_result != WAIT_TIMEOUT)
        {
            ID3D11Resource* buffer = nullptr;
            ID3D11RenderTargetView* view = nullptr;
            ID3D11Texture2D* tex = nullptr;
            void* tPixels = nullptr;
            D3D11_TEXTURE2D_DESC tDesc;
            tDesc.Width = width;
            tDesc.Height = height;
            tDesc.MipLevels = 1;
            tDesc.ArraySize = 1;
            tDesc.Format = Compositor_DXGIFormatForPixelFormat(l.base.pixelformat, true);
            u8 bpp = Compositor_BytesPerPixelFormat(l.base.pixelformat);
            // tDesc uses UINT, so let's check for overflow first.
            u64 tPitch = width * bpp;
            u64 tSlicePitch = tPitch * height;
            if (tPitch > UINT_MAX || tSlicePitch > UINT_MAX)
            {
                assert(false);
                return false;
            }
            tPixels = calloc(tPitch, height);
            if (!tPixels)
            {
                assert(false);
                return false;
            }
            tDesc.SampleDesc.Count = 1;
            tDesc.SampleDesc.Quality = 0;
            tDesc.Usage = D3D11_USAGE_DEFAULT;
            tDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            tDesc.CPUAccessFlags = 0;
            tDesc.MiscFlags = 0;

            // Calculate the pixels we want to upload
            Generate_Image(tPixels, &l.base);

            HRESULT hr;
            hr = swapchain->GetBuffer(0, IID_PPV_ARGS(&buffer));
            if (SUCCEEDED(hr) && buffer)
            {
#if !USE_RENDERTARGETVIEW
                // Just copy pixels into the backbuffer
                D3D11_BOX texBox;
                texBox.left = 0;
                texBox.right = width;
                texBox.top = 0;
                texBox.bottom = height;
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
                        ID3D11ShaderResourceView *srv = nullptr;
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
            if (tPixels)
                free(tPixels);
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

            // Now that we rendered something, update the refreshcounter to
            // match the new scene
            comp->visuals_changed = true;
            l.base.refreshcounter = layer.refreshcounter;

            if (FAILED(hr))
                return false;
        }
    }

    return true;
}

bool Compositor_Update(Compositor_State* comp, const Compositor_Scene* scene, HWND hWnd)
{
    if (comp->status != Compositor_Status_Running)
        return false;

    // Mark existing layers as expired, if they are still marked after all new
    // layers are iterated, they will be deleted
    for (auto l : comp->scene.layers)
        if (l)
            l->expired = true;

    // Update each layer, creating them if necessary.
    for (auto &layer : scene->layers)
    {
        // See if this exists in the current scene
        bool found = false;
        for (auto l : comp->scene.layers)
        {
            if (l && l->base.sort == layer.sort)
            {
                found = true;
                // Found a match, update it
                Compositor_Update_Layer(comp, *l, layer);
                break;
            }
        }
        if (!found)
        {
            // No matching layer, create one
            auto l = new Compositor_Scene_Platform_Layer();
            u32 i;
            for (i = 0; i < comp->scene.layers.size(); i++)
            {
                if (comp->scene.layers[i] == NULL)
                {
                    comp->scene.layers[i] = l;
                    break;
                }
            }
            // If we didn't find an empty slot, push the new layer
            if (i >= comp->scene.layers.size())
                comp->scene.layers.push_back(l);
            // Now update the newly created layer
            Compositor_Update_Layer(comp, *l, layer);
        }
    }

    // If any layers no longer exist, delete them.
    for (u32 i = 0; i < comp->scene.layers.size(); i++)
    {
        auto l = comp->scene.layers[i];
        if (l && l->expired)
        {
            Compositor_Reset_Layer(comp, *l);
            delete l;
            comp->scene.layers[i] = NULL;
        }
    }

    if (comp->visuals_changed)
    {
        // Rebuild the visuals 
        comp->visuals_changed = false;
        HRESULT hr = comp->rootvisual->RemoveAllVisuals();
        if (FAILED(hr))
            return false;

        for (auto l : comp->scene.layers)
        {
            if (l)
            {
                hr = comp->rootvisual->AddVisual(l->dcompvisual, TRUE, nullptr);
                if (FAILED(hr))
                    return false;
            }
        }

        // Now commit the transaction to dcomp
        hr = comp->dcomp->Commit();
        if (FAILED(hr))
            return false;
    }

    return true;
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
static Compositor_State* compositor;

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
    Compositor_Scene scene;
    Compositor_Scene_Make_Colorspace_Test(&scene);
    quit = false;
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
        // If an error is encountered, we reinitialize the device and try again, but
        // only once, if the device is lost repeatedly we're not going to make
        // progress, so two attempts is probably optimal
        for (i32 tries = 2; tries >= 0; tries--)
        {
            Compositor_CheckDeviceState(compositor, hWindow);
            if (compositor->status == Compositor_Status_Running)
                break;
        }
        Compositor_Update(compositor, &scene, hWindow);
    }

    // Shutdown
    Compositor_UncreateDevice(compositor, hWindow);
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

   compositor = new Compositor_State();
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
