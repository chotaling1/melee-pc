#pragma once

#include <string>

#include <webgpu/webgpu_cpp.h>

/* Debug frame capture: copies the presented frame (the swapchain texture, so
 * overlays included) to a PNG, or the EFB when the surface can't be copied.
 * Synchronous -- the map is waited on right after submit -- so a captured frame
 * stalls the pipeline once. Meant for scripted test runs, not gameplay. */
namespace aurora::gfx::frame_capture {
struct Pending {
  wgpu::Buffer buffer;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t bytesPerRow = 0;
  wgpu::TextureFormat format = wgpu::TextureFormat::Undefined;
  std::string path;
};

/* Game thread: capture the frame ended by the next aurora_end_frame(). */
void request(const char* path) noexcept;
/* Game thread: hands the pending request (if any) to the frame being ended. */
std::string take_request() noexcept;
/* Record the copy of `source` into the frame's encoder. Returns an empty
 * Pending (no buffer) when path is empty or the format is unsupported. */
Pending encode(const wgpu::CommandEncoder& encoder, const wgpu::Texture& source, std::string path) noexcept;
/* After Submit: wait for the copy, convert to RGBA8 and write the PNG. */
void write(Pending&& pending) noexcept;
} // namespace aurora::gfx::frame_capture
