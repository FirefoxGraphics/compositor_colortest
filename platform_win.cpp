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
#include <dxgi.h>
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

#include "framework.h"
#include "testcolorspaces.h"

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
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

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
   {
      return FALSE;
   }

   hWindow = hWnd;
   ShowWindow(hWnd, nCmdShow);
   UpdateWindow(hWnd);

   compositor = Compositor_New(hWnd, win_dpiX, win_dpiY);

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

struct Compositor_State
{
    Compositor_Scene scene;
    Compositor_Status status;
    HWND hWindow;
    u32 dpiX;
    u32 dpiY;
    ID3D11Device* d3d;
    IDXGIDevice* dxgi;
    IDXGIAdapter* adapter;
    IDXGIFactory2* factory;
    IDCompositionDevice* dcomp;
    IDCompositionTarget* dcomptarget;
    ID3D11DeviceContext* context;
    IDCompositionVisual* rootvisual;
};

Compositor_State* Compositor_New(void* windowhandle, u16 dpi_x, u16 dpi_y)
{
    Compositor_State* comp;
    comp = (Compositor_State*)calloc(1, sizeof(*comp));
    if (comp)
    {
        comp->status = Compositor_Status_No_Device;
        comp->dpiX = dpi_x;
        comp->dpiY = dpi_y;
    }
    return comp;
}

static void Compositor_UncreateDevice(Compositor_State* comp)
{
    SafeRelease(&comp->rootvisual);
    SafeRelease(&comp->adapter);
    SafeRelease(&comp->factory);
    SafeRelease(&comp->dxgi);
    SafeRelease(&comp->dcomp);
    SafeRelease(&comp->dcomptarget);
    SafeRelease(&comp->context);
    SafeRelease(&comp->d3d);
}

void Compositor_Destroy(Compositor_State* comp)
{
    Compositor_UncreateDevice(comp);
    free(comp);
}

void Compositor_CreateDevice(Compositor_State *comp)
{
    switch (comp->status)
    {
    case Compositor_Status_Running:
        return;
    default:
        break;
    }

    comp->status = Compositor_Status_Device_Creation_Failed;

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

    hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&comp->factory));
    if (!SUCCEEDED(hr))
        return;

    comp->status = Compositor_Status_Running;
}

void Compositor_CheckDeviceState(Compositor_State *comp)
{
    if (comp->dcomp)
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

const f32 testcolors[3][5] = {
    {1.0f, 0.4f, 0.1f, 0.1f, 0.4f},
    {0.4f, 1.0f, 1.0f, 0.4f, 0.1f},
    {0.1f, 0.1f, 0.4f, 1.0f, 1.0f}
};

static void Generate_Colorspace_Test_Pattern_Pixel(float p[], f32 pos[])
{
    constexpr u32 limit = sizeof(testcolors[0]) / sizeof(testcolors[0][0]);
    constexpr u32 limit1 = limit - 1;
    constexpr u32 limit2 = limit - 2;
    f32 f = pos[0] * limit;
    f = f < 0.0f ? 0.0f : f < (float)limit1 ? f : (float)limit;
    u32 i = (int)f;
    i = i < 0 ? 0 : i < limit2 ? i : limit2;
    f32 lerp = f - i;
    f32 ilerp = 1.0f - lerp;
    for (u32 c = 0;c < 3;c++)
        p[c] = testcolors[c][i] * ilerp + testcolors[c][i + 1] * lerp;
    p[3] = 1.0f;
}

static void Pixel_EncodesRGB(f32 c[], f32 o[])
{
    for (u32 i = 0; i < 3; i++)
    {
        f32 f = c[i];
        o[i] = f < 0.0031308f ? f * 12.92f : 1.055f * pow(f, 0.41666f) - 0.055f;
    }
    o[3] = c[3];
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

static void Generate_Colorspace_Test_Pattern_BGRA8(u32* pixels, u16 width, u16 height)
{
    for (u32 y = 0; y < height; y++)
    {
        for (u32 x = 0; x < width; x++)
        {
            auto p = pixels + y * width + x;
            f32 pos[2];
            f32 c[4];
            pos[0] = x * (1.0f / width);
            pos[1] = y * (1.0f / height);
            Generate_Colorspace_Test_Pattern_Pixel(c, pos);
            Pixel_EncodesRGB(c, c);
            Pixel_Modulate_And_Clamp(c, 255.0f, 0.0f, 255.0f);
            *p = 
                (u32)c[0] * 0x1 +
                (u32)c[1] * 0x100 +
                (u32)c[2] * 0x10000 +
                (u32)c[3] * 0x1000000;
        }
    }
}

static void Generate_Colorspace_Test_Pattern_BGR10A2(u32* pixels, u16 width, u16 height)
{
    for (u32 y = 0; y < height; y++)
    {
        for (u32 x = 0; x < width; x++)
        {
            auto p = pixels + y * width + x;
            f32 pos[2];
            f32 c[4];
            pos[0] = x * (1.0f / width);
            pos[1] = y * (1.0f / height);
            Generate_Colorspace_Test_Pattern_Pixel(c, pos);
            Pixel_EncodesRGB(c, c);
            Pixel_Modulate_And_Clamp(c, 1023.0f, 0.0f, 1023.0f);
            *p = 
                (u32)c[0] * 0x1 +
                (u32)c[1] * 0x400 +
                (u32)c[2] * 0x100000 +
                ((u32)c[3] >> 8) * 0xC0000000;
        }
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

static void Generate_Colorspace_Test_Pattern_RGBA16F(u64* pixels, u16 width, u16 height)
{
    for (u32 y = 0; y < height; y++)
    {
        for (u32 x = 0; x < width; x++)
        {
            auto p = pixels + y * width + x;
            f32 pos[2];
            f32 c[4];
            pos[0] = x * (1.0f / width);
            pos[1] = y * (1.0f / height);
            Generate_Colorspace_Test_Pattern_Pixel(c, pos);
            *p = 
                (u64)ToF16(c[0]) * 0x1ull +
                (u64)ToF16(c[1]) * 0x10000ull + 
                (u64)ToF16(c[2]) * 0x100000000ull + 
                (u64)ToF16(c[3]) * 0x1000000000000ull;
        }
    }
}

static bool Compositor_Make_Colorspace_Test_Scene(Compositor_State* comp)
{
    // Create test gradients for several colorspaces
    comp->scene.ready = 1;
    for (u32 i = 0; i < 16; i++)
    {
        u32 width = 512 * comp->dpiX / 96;
        u32 height = 32 * comp->dpiY / 96;
        IDCompositionVisual* visual = NULL;
        HRESULT hr = comp->dcomp->CreateVisual(&visual);
        if (!SUCCEEDED(hr))
            return false;
        visual->SetOffsetX(0.0f);
        visual->SetOffsetY(i * height * 1.0f);
        DXGI_SWAP_CHAIN_DESC1 scdesc = {};
        scdesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        scdesc.SampleDesc.Count = 1;
        scdesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        scdesc.BufferCount = 2;
        scdesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        scdesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        scdesc.Width = width;
        scdesc.Height = height;
        scdesc.Stereo = FALSE;
        scdesc.Scaling = DXGI_SCALING_STRETCH;
        scdesc.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
        IDXGISwapChain1* swapchain;
        hr = comp->factory->CreateSwapChainForComposition(comp->d3d, &scdesc, NULL, &swapchain);
        if (!SUCCEEDED(hr))
            return false;
        ID3D11Resource* buffer;
        hr = swapchain->GetBuffer(0, IID_PPV_ARGS(&buffer));
        if (!SUCCEEDED(hr))
            return false;
        ID3D11RenderTargetView* view;
        hr = comp->d3d->CreateRenderTargetView(buffer, NULL, &view);
        if (!SUCCEEDED(hr))
            return false;
        f32 color[] = { i * 1.0f / 16.0f, 0.0f, 1.0f, 1.0f };
        comp->context->ClearRenderTargetView(view, color);
        buffer->Release();
        view->Release();
        hr = swapchain->Present(0, DXGI_PRESENT_RESTART);
        if (!SUCCEEDED(hr))
            return false;
    }
}

void Compositor_Update(Compositor_State* comp, const Compositor_Scene* scene)
{
    Compositor_CheckDeviceState(comp);
    if (comp->status != Compositor_Status_Running)
        return;

    // TODO: Actually create the scene definition semantics
    if (!comp->scene.ready)
    {
        Compositor_Make_Colorspace_Test_Scene(comp);
    }
}

