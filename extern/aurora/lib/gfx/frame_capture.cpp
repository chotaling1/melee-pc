#include "frame_capture.hpp"

#include "../logging.hpp"
#include "../webgpu/gpu.hpp"

#include <png.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <vector>

#include <magic_enum.hpp>

namespace aurora::gfx::frame_capture {
namespace {
constexpr Module Log{"aurora::gfx::frame_capture"};

std::mutex g_mutex;
std::string g_request;

/* Byte order of one texel, or false for formats this can't write. */
bool texel_order(wgpu::TextureFormat format, bool& bgra) noexcept {
  switch (format) {
  case wgpu::TextureFormat::RGBA8Unorm:
  case wgpu::TextureFormat::RGBA8UnormSrgb:
    bgra = false;
    return true;
  case wgpu::TextureFormat::BGRA8Unorm:
  case wgpu::TextureFormat::BGRA8UnormSrgb:
    bgra = true;
    return true;
  default:
    return false;
  }
}
} // namespace

void request(const char* path) noexcept {
  std::lock_guard lock{g_mutex};
  g_request = path != nullptr ? path : "";
}

std::string take_request() noexcept {
  std::lock_guard lock{g_mutex};
  return std::exchange(g_request, {});
}

Pending encode(const wgpu::CommandEncoder& encoder, const wgpu::Texture& source, std::string path) noexcept {
  if (path.empty() || !source) {
    return {};
  }
  bool bgra = false;
  if (!texel_order(source.GetFormat(), bgra)) {
    Log.warn("Can't capture {}: unsupported frame buffer format {}", path, magic_enum::enum_name(source.GetFormat()));
    return {};
  }
  Pending pending{
      .width = source.GetWidth(),
      .height = source.GetHeight(),
      .bytesPerRow = (source.GetWidth() * 4 + 255) & ~255u, /* copies need 256-byte row alignment */
      .format = source.GetFormat(),
      .path = std::move(path),
  };
  const wgpu::BufferDescriptor descriptor{
      .label = "Frame capture readback",
      .usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::MapRead,
      .size = static_cast<uint64_t>(pending.bytesPerRow) * pending.height,
  };
  pending.buffer = webgpu::g_device.CreateBuffer(&descriptor);
  const wgpu::TexelCopyTextureInfo src{.texture = source};
  const wgpu::TexelCopyBufferInfo dst{
      .layout = {.offset = 0, .bytesPerRow = pending.bytesPerRow, .rowsPerImage = pending.height},
      .buffer = pending.buffer,
  };
  const wgpu::Extent3D size{.width = pending.width, .height = pending.height, .depthOrArrayLayers = 1};
  encoder.CopyTextureToBuffer(&src, &dst, &size);
  return pending;
}

void write(Pending&& pending) noexcept {
  if (!pending.buffer) {
    return;
  }
  const uint64_t size = static_cast<uint64_t>(pending.bytesPerRow) * pending.height;
  wgpu::MapAsyncStatus mapStatus = wgpu::MapAsyncStatus::Error;
  const auto future = pending.buffer.MapAsync(
      wgpu::MapMode::Read, 0, size, wgpu::CallbackMode::WaitAnyOnly,
      [&mapStatus](wgpu::MapAsyncStatus status, wgpu::StringView) { mapStatus = status; });
  const auto waitStatus = webgpu::g_instance.WaitAny(future, 5000000000ull);
  if (waitStatus != wgpu::WaitStatus::Success || mapStatus != wgpu::MapAsyncStatus::Success) {
    Log.warn("Frame capture {} failed: wait {}, map {}", pending.path, magic_enum::enum_name(waitStatus),
             magic_enum::enum_name(mapStatus));
    return;
  }
  const auto* mapped = static_cast<const uint8_t*>(pending.buffer.GetConstMappedRange(0, size));
  bool bgra = false;
  texel_order(pending.format, bgra);
  std::vector<uint8_t> rgba(static_cast<size_t>(pending.width) * pending.height * 4);
  for (uint32_t y = 0; y < pending.height; ++y) {
    const uint8_t* in = mapped + static_cast<size_t>(y) * pending.bytesPerRow;
    uint8_t* out = rgba.data() + static_cast<size_t>(y) * pending.width * 4;
    for (uint32_t x = 0; x < pending.width; ++x, in += 4, out += 4) {
      out[0] = bgra ? in[2] : in[0];
      out[1] = in[1];
      out[2] = bgra ? in[0] : in[2];
      out[3] = 255; /* EFB alpha is a GX value, not coverage */
    }
  }
  pending.buffer.Unmap();

  /* Write next to the target and rename, so a script polling for the file
   * never reads a half-written PNG. */
  const std::filesystem::path path{pending.path};
  std::error_code ec;
  if (path.has_parent_path()) {
    std::filesystem::create_directories(path.parent_path(), ec);
  }
  auto tmp = path;
  tmp += ".tmp";
  png_image image{};
  image.version = PNG_IMAGE_VERSION;
  image.width = pending.width;
  image.height = pending.height;
  image.format = PNG_FORMAT_RGBA;
  if (png_image_write_to_file(&image, tmp.string().c_str(), 0, rgba.data(), 0, nullptr) == 0) {
    Log.warn("Frame capture {} failed: {}", pending.path, image.message);
    png_image_free(&image);
    return;
  }
  std::filesystem::rename(tmp, path, ec);
  if (ec) {
    Log.warn("Frame capture {} failed: {}", pending.path, ec.message());
    return;
  }
  Log.info("Captured {}x{} frame to {}", pending.width, pending.height, pending.path);
}
} // namespace aurora::gfx::frame_capture

/* Defined here rather than in aurora.cpp: aurora_core is linked after
 * aurora_gx, so the host's reference to this is what pulls this object (and
 * the encode/write that aurora.cpp calls) out of the static library. */
void aurora_capture_frame(const char* path) { aurora::gfx::frame_capture::request(path); }
