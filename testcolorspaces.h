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

/// For now this is not really used, but is a placeholder for a multi-platform
/// way to describe the scene
struct Compositor_Scene
{
	u32 ready;
};

struct Compositor_State;

/// Allocate and set up a new Compositor instance, each instance can handle one
/// window
Compositor_State *Compositor_New(void *windowhandle, u16 dpi_x, u16 dpi_y);

/// Shut down and destroy a Compositor instance
void Compositor_Destroy(Compositor_State *state);

/// Check if the underlying compositor device has been lost and recreate it.
void Compositor_CheckDeviceState(Compositor_State *state);

/// Initializes the compositor and graphics device if needed, updates all
/// visuals overlaid on the window to match the current state.
void Compositor_Update(Compositor_State* state, const Compositor_Scene *scene);
