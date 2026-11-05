#pragma once

#if defined(__PS3__) || defined(__PPU__)
#include "app.hpp"

namespace Novuss {
namespace RenderTiny3D {

// Initialize Tiny3D video and allocate GPU textures in RSX VRAM.
// Returns true on success.
bool init(int width, int height);

// Free Tiny3D resources on shutdown.
void shutdown();

// Render full frame using Tiny3D hardware pipeline (RSX GPU).
void renderFrame(App& app);

} // namespace RenderTiny3D
} // namespace Novuss
#endif
