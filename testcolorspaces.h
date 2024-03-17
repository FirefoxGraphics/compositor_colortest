/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#pragma once

#include <cstdint>

#ifdef WIN32
#include "resource.h"
#endif

// Rust has better names for the regular types.
using f32 = float;
using i32 = int32_t;
using u32 = uint32_t;
using f64 = double;
using i64 = int64_t;
using u64 = uint64_t;
using usize = size_t;

// Compositor system

/// For now this is not really used, but is a placeholder for a multi-platform
/// way to describe the scene
struct Compositor_Scene
{
	u32 unused;
};

/// Startup initialization of the compositor system, this doesn't actually
/// create the device context and such, which is handled in Update.
void Compositor_Init();

/// Shutdown the compositor system
void Compositor_Shutdown();

/// Initializes the compositor and graphics device if needed, updates all
/// visuals overlaid on the window to match the current state.
void Compositor_Update(const Compositor_Scene *scene);
