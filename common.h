/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#pragma once

#include <cstdint>
#include <vector>

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

// Compositor system

typedef enum Compositor_Status
{
	Compositor_Status_No_Device,
	Compositor_Status_Device_Lost,
	Compositor_Status_Device_Creation_Failed,
	Compositor_Status_Running,
}
Compositor_Status;

enum Compositor_Format_Flags
{
	FORMAT_RGBA32F = 0x0000, // 128bpp
	FORMAT_RGBA16F = 0x0001, // 64bpp
	FORMAT_RGB10A2 = 0x0002, // 32bpp
	FORMAT_RGBA8 = 0x0003, // 32bpp
	FORMAT_BGRA8 = 0x0004, // 32bpp
	FORMAT_MASK = 0x000F,
	TRANSFER_NONE = 0x0000, // no transformation (linear mapping)
	TRANSFER_SRGB = 0x0010, // sRGB OETF, approximately gamma 2.2
	TRANSFER_REC709 = 0x0020, // Rec709 OETF, approximately gamma 2.2222
	TRANSFER_REC2020_12BIT = 0x0030, // Rec2020 OETF for 12bit, this is a slightly more precise parameters than Rec709 OETF
	TRANSFER_REC2100_PQ = 0x0040, // Rec2100 OETF for HDR12, up to 10000 nits
	TRANSFER_REC2100_HLG = 0x0050, // Rec2100 OETF for HDR10, up to 1000 nits
	TRANSFER_MASK = 0x00F0,
	PRIMARIES_REC709 = 0x0000, // primaries used by Rec709 and sRGB : {0.64, 0.33}, {0.3, 0.6}, {0.15, 0.06}
	PRIMARIES_DCIP3 = 0x0100, // primaries used by DCI-P3 : {0.68, 0.32}, {0.265, 0.69}, {0.15, 0.06}
	PRIMARIES_REC2020 = 0x0200, // primaries used by Rec2020 and Rec2100 (HDR) : {0.708, 0.292}, {0.17, 0.797}, {0.131, 0.046}
	PRIMARIES_MASK = 0x0F00,
	WHITE_D65 = 0x0000, // whitepoint used by Rec2020, Rec2100, DCI-P3, and often Rec709 and sRGB : {0.312713, 0.329016}
	WHITE_D50 = 0x1000, // whitepoint used by some content in Rec709 and sRGB : {0.34567, 0.35850}
	WHITE_MASK = 0xF000,
};

enum Compositor_PixelFormat
{
	rgba32f_scrgb =
		Compositor_Format_Flags::FORMAT_RGBA32F |
		Compositor_Format_Flags::TRANSFER_NONE |
		Compositor_Format_Flags::PRIMARIES_REC709 |
		Compositor_Format_Flags::WHITE_D65,
	rgba16f_scrgb =
		Compositor_Format_Flags::FORMAT_RGBA16F |
		Compositor_Format_Flags::TRANSFER_NONE |
		Compositor_Format_Flags::PRIMARIES_REC709 |
		Compositor_Format_Flags::WHITE_D65,
	srgb =
		Compositor_Format_Flags::FORMAT_RGBA8 |
		Compositor_Format_Flags::TRANSFER_SRGB |
		Compositor_Format_Flags::PRIMARIES_REC709 |
		Compositor_Format_Flags::WHITE_D50,
	dcip3 =
		Compositor_Format_Flags::FORMAT_RGBA8 |
		Compositor_Format_Flags::TRANSFER_SRGB |
		Compositor_Format_Flags::PRIMARIES_DCIP3 |
		Compositor_Format_Flags::WHITE_D65,
	rec709 =
		Compositor_Format_Flags::FORMAT_RGBA8 |
		Compositor_Format_Flags::TRANSFER_REC709 |
		Compositor_Format_Flags::PRIMARIES_REC709 |
		Compositor_Format_Flags::WHITE_D65,
	rec2020_8bit =
		Compositor_Format_Flags::FORMAT_RGBA8 |
		Compositor_Format_Flags::TRANSFER_REC709 |
		Compositor_Format_Flags::PRIMARIES_REC2020 |
		Compositor_Format_Flags::WHITE_D65,
	rec2020_10bit =
		Compositor_Format_Flags::FORMAT_RGB10A2 |
		Compositor_Format_Flags::TRANSFER_REC709 |
		Compositor_Format_Flags::PRIMARIES_REC2020 |
		Compositor_Format_Flags::WHITE_D65,
	rec2100_10bit =
		Compositor_Format_Flags::FORMAT_RGB10A2 |
		Compositor_Format_Flags::TRANSFER_REC709 |
		Compositor_Format_Flags::PRIMARIES_REC2020 |
		Compositor_Format_Flags::WHITE_D65,
};

struct Compositor_Scene_Layer
{
	/// Constant identifier for this layer across frames
	u64 z = 0;
	/// Frame counter that can be incremented to regenerate the image
	u64 refreshcounter = 0;
	/// Coordinates are for 96 DPI, actual sizes determined based on display DPI
	u16 x = 0;
	u16 y = 0;
	u16 width = 0;
	u16 height = 0;
	Compositor_PixelFormat pixelformat = Compositor_PixelFormat::srgb;
	/// Function that will be called to generate each pixel color in the test
	/// image, x and y are in the range 0..width and 0..height respectively,
	/// output color is rgba32f_scrgb and will be converted by caller.
	void (*pixelcallback)(f32 output[4], f32 x, f32 y, f32 width, f32 height) = nullptr;
};

/// For now this is very barebones
struct Compositor_Scene
{
	std::vector<Compositor_Scene_Layer*> layers;
};

struct Compositor_State;

/// Constructs a test scene
void Compositor_Scene_Make_Colorspace_Test(Compositor_Scene* scene);

/// Initializes the compositor and graphics device if needed, updates all
/// visuals overlaid on the window to match the current state.
/// Returns false if an error occurred twice and it has given up (e.g.
/// repeatedly losing the device).
bool Compositor_Update(Compositor_State* state, const Compositor_Scene *scene);
