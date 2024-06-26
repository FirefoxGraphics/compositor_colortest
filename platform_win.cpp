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
    IDXGISwapChain4* swapchain = nullptr;
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
    IDCompositionDevice* dcomp = nullptr;
    IDCompositionTarget* dcomptarget = nullptr;
    ID3D11DeviceContext* context = nullptr;
    IDCompositionVisual* rootvisual = nullptr;

    // Scene state in our native representation
    Compositor_Scene_Platform scene;
};

void Compositor_UncreateDevice(Compositor_State* comp)
{
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

void Compositor_CreateDevice(Compositor_State* comp)
{
    switch (comp->status)
    {
    case Compositor_Status_Running:
        return;
    default:
        break;
    }

    // Start by destroying the previous device if any
    Compositor_UncreateDevice(comp);

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

    u32 flags =
        D3D11_CREATE_DEVICE_BGRA_SUPPORT |
        D3D11_CREATE_DEVICE_DEBUG;

    constexpr D3D_FEATURE_LEVEL feature_levels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };

    D3D_FEATURE_LEVEL featureLevelSupported = feature_levels[0];

    HRESULT hr = D3D11CreateDevice(
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
    if (!SUCCEEDED(hr))
        return;

    hr = comp->d3d->QueryInterface(&comp->dxgi);
    if (!SUCCEEDED(hr))
        return;

    hr = DCompositionCreateDevice(
        comp->dxgi,
        __uuidof(IDCompositionDevice),
        reinterpret_cast<void**>(&comp->dcomp));
    if (!SUCCEEDED(hr))
        return;

    hr = comp->dcomp->CreateTargetForHwnd(comp->hWindow, TRUE, &comp->dcomptarget);
    if (!SUCCEEDED(hr))
        return;

    hr = comp->dcomp->CreateVisual(&comp->rootvisual);
    if (!SUCCEEDED(hr))
        return;

    hr = comp->dxgi->GetAdapter(&comp->adapter);
    if (!SUCCEEDED(hr))
        return;

    // COM is a bit confusing here - CreateDXGIFactory2 simply adds a flags
    // parameter, the 2 in the name is only the API version of CreateDXGIFactory
    // and entirely unrelated to which DXGIFactory version we are requesting
    hr = CreateDXGIFactory2(0u, __uuidof(IDXGIFactory7), reinterpret_cast<void**>(&comp->factory));
    if (!SUCCEEDED(hr))
        return;

    comp->status = Compositor_Status_Running;
}

void Compositor_CheckDeviceState(Compositor_State* comp)
{
    if (comp->status != Compositor_Status_Running)
        Compositor_CreateDevice(comp);
    else if (comp->dcomp)
    {
        BOOL bIsValid = FALSE;
        HRESULT res = comp->dcomp->CheckDeviceState(&bIsValid);
        if (res != S_OK || !bIsValid)
        {
            comp->status = Compositor_Status_Device_Lost;
            Compositor_CreateDevice(comp);
        }
    }
    else
        Compositor_CreateDevice(comp);
}

bool Compositor_Update_Layer(Compositor_State* comp, Compositor_Scene_Platform_Layer* l, const Compositor_Scene_Layer* layer)
{
    if (!layer)
    {
        // prepare existing layer for deletion, which will be done by the caller
        l->base = {};
        SafeRelease(&l->swapchain);
        SafeRelease(&l->dcompvisual);
        return true;
    }

    l->expired = false;

    if (l->base.refreshcounter == layer->refreshcounter)
        return true;

    // Project the 96 DPI scene layer to the display DPI
    u32 x = layer->x * comp->dpiX / 96;
    u32 y = layer->y * comp->dpiY / 96;
    u32 width = layer->width * comp->dpiX / 96;
    u32 height = layer->height * comp->dpiY / 96;

    if (l->swapchain && (l->base.width != width || l->base.height != height || l->base.pixelformat != layer->pixelformat || l->base.pixelcallback != layer->pixelcallback))
    {
        if (l->dcompvisual)
            l->dcompvisual->SetContent(NULL);
        SafeRelease(&l->swapchain);
    }
    if (l->dcompvisual)
    {
        if (l->base.x != x)
        {
            l->base.x = x;
            l->dcompvisual->SetOffsetX(l->base.x);
        }
        if (l->base.y != y)
        {
            l->base.y = y;
            l->dcompvisual->SetOffsetY(l->base.y);
        }
    }
    else
    {
        HRESULT hr = comp->dcomp->CreateVisual(&l->dcompvisual);
        if (!SUCCEEDED(hr))
            return false;
        l->base.x = x;
        l->base.y = y;
        l->dcompvisual->SetOffsetX(l->base.x);
        l->dcompvisual->SetOffsetY(l->base.y);
        hr = comp->rootvisual->AddVisual(l->dcompvisual, TRUE, comp->rootvisual);
        if (!SUCCEEDED(hr))
            return false;
    }

    if (l->swapchain == NULL)
    {
        DXGI_SWAP_CHAIN_DESC1 scdesc = {};
        switch (layer->pixelformat & Compositor_Format_Flags::FORMAT_MASK)
        {
        default:
            // unknown pixel format - update this code if you encounter this
            assert(false);
            scdesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
            break;
        case Compositor_Format_Flags::FORMAT_RGBA32F:
            scdesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
            break;
        case Compositor_Format_Flags::FORMAT_RGBA16F:
            scdesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
            break;
        case Compositor_Format_Flags::FORMAT_RGB10A2:
            scdesc.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
            break;
        case Compositor_Format_Flags::FORMAT_RGBA8:
            scdesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
            break;
        case Compositor_Format_Flags::FORMAT_BGRA8:
            scdesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
            break;
        }
        scdesc.SampleDesc.Count = 1;
        scdesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        scdesc.BufferCount = 2;
        scdesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        scdesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        scdesc.Width = width;
        scdesc.Height = height;
        scdesc.Stereo = FALSE;
        scdesc.Scaling = DXGI_SCALING_STRETCH;
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
        scdesc.Flags =
            DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT |
            DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
        IDXGISwapChain1* swapchain1;
        HRESULT hr = comp->factory->CreateSwapChainForComposition(comp->d3d, &scdesc, NULL, &swapchain1);
        if (!SUCCEEDED(hr))
            return false;
        // Convert the IDXGISwapChain1 to IDXGISwapChain4 because we need a few
        // more features to do HDR stuff
        hr = swapchain1->QueryInterface(__uuidof(IDXGISwapChain4), reinterpret_cast<void**>(&l->swapchain));
        swapchain1->Release();
        if (!SUCCEEDED(hr))
            return false;
        l->swapchain_waitable_object = l->swapchain->GetFrameLatencyWaitableObject();
        l->dcompvisual->SetContent(l->swapchain);
    }

    // Render a new frame in the swapchain and present it
    if (l->swapchain)
    {
        // If the swapchain isn't ready for a new frame, we just skip the render
        DWORD wait_result = WaitForSingleObjectEx(l->swapchain_waitable_object, 0, FALSE);
        if (wait_result != WAIT_TIMEOUT)
        {
            ID3D11Resource* buffer;
            HRESULT hr = l->swapchain->GetBuffer(0, IID_PPV_ARGS(&buffer));
            if (!SUCCEEDED(hr))
                return false;
            ID3D11RenderTargetView* view;
            hr = comp->d3d->CreateRenderTargetView(buffer, NULL, &view);
            if (!SUCCEEDED(hr))
                return false;
            f32 color[] = { 0.0f, 0.0f, 1.0f, 1.0f };
            comp->context->ClearRenderTargetView(view, color);
            buffer->Release();
            view->Release();
            hr = l->swapchain->Present(0, DXGI_PRESENT_ALLOW_TEARING);
            if (!SUCCEEDED(hr))
                return false;
            // Now that we rendered something, update the refreshcounter to
            // match the new scene
            l->base.refreshcounter = layer->refreshcounter;
        }
    }

    return true;
}

bool Compositor_Update(Compositor_State* comp, const Compositor_Scene* scene)
{
    // If an error is encountered, we reinitialize the device and try again, but
    // only once, if the device is lost repeatedly we're not going to make
    // progress, so two attempts is probably optimal
    for (i32 tries = 2; tries >= 0; tries--)
    {
        Compositor_CheckDeviceState(comp);
        if (comp->status != Compositor_Status_Running)
            continue;

        // Mark existing layers as expired, if they are still marked after all new
        // layers are iterated, they will be deleted
        for (auto l : comp->scene.layers)
            if (l)
                l->expired = true;

        // Update each layer, creating them if necessary.
        for (auto layer : scene->layers)
        {
            // See if this exists in the current scene
            bool found = false;
            for (auto l : comp->scene.layers)
            {
                if (l && l->base.z == layer->z)
                {
                    found = true;
                    // Found a match, update it
                    Compositor_Update_Layer(comp, l, layer);
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
                Compositor_Update_Layer(comp, l, layer);
            }
        }

        // If any layers no longer exist, delete them.
        for (u32 i = 0; i < comp->scene.layers.size(); i++)
        {
            auto l = comp->scene.layers[i];
            if (l && l->expired)
            {
                Compositor_Update_Layer(comp, l, NULL);
                delete l;
                comp->scene.layers[i] = NULL;
            }
        }

        // Now commit the transaction to dcomp
        HRESULT hr = comp->dcomp->Commit();
        if (!SUCCEEDED(hr))
            continue;
        return true;
    }
    return false;
}

#define MAX_LOADSTRING 100

// Global Variables:
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
        // Refresh the scene no faster than 1000hz, we don't actually use any
        // wait timeouts, so this is the only way we're yielding CPU
        Sleep(1);
        Compositor_Update(compositor, &scene);
    }

    // Shutdown
    Compositor_UncreateDevice(compositor);
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
    case WM_DESTROY:
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
