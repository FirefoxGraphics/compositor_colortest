/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#pragma once

#include <cstdint>

#ifdef WIN32
#include "resource.h"
#endif

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

enum Compositor_PixelFormat
{
	rgba32f_scrgb, /// rec709 primaries, but no gamma and no cutoff, components can be negative which enables full CIE Lab equivalence, brightness of 1.0 = 80 nits
	rgba16f_scrgb, /// rec709 primaries, but no gamma and no cutoff, components can be negative which enables full CIE Lab equivalence, brightness of 1.0 = 80 nits
	rgba8_srgb, /// rec709 primaries, D50 whitepoint, approximately gamma 2.2 overall (0.0 to 0.04045 encoded as linear, 0.04045 to 1.0 encoded with gamma 2.4), brightness of 1.0 = 80 nits
	rgba8_rec709, /// rec709 primaries, D65 whitepoint, gamma 2.2222, brightness of 1.0 = 80 nits
	rgba8_rec2020, /// rec2020 primaries, D65 whitepoint, gamma 2.2222, brightness of 1.0 = 80 nits
	rgb10a2_rec2100, /// rec2020 primaries, D65 whitepoint, gamma 2.2222, brightness of 1.0 = 80 nits, but range goes up to 10000 nits
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
	Compositor_PixelFormat pixelformat;
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

/// Initializes the compositor and graphics device if needed, updates all
/// visuals overlaid on the window to match the current state.
/// Returns false if an error occurred twice and it has given up (e.g.
/// repeatedly losing the device).
bool Compositor_Update(Compositor_State* state, const Compositor_Scene *scene);
