// SPDX-License-Identifier: AGPL-3.0-only

#include "photo_store.h"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib-object.h>
#include <glib.h>

#include "../../core/types.h"
#include <raven/contracts/camera.h>

namespace raven::media {
namespace {
constexpr auto kPermissions = 0600;
constexpr auto kMinimumFreeBytes = 64ULL * 1024 * 1024;
constexpr auto kChannels = 3;
constexpr auto kBits = 8;

void WriteDurable(const std::filesystem::path& path, const char* data,
                  std::size_t size) {
  const auto partial = path.string() + ".part";
  const int flags =
      static_cast<unsigned>(O_WRONLY) | static_cast<unsigned>(O_CREAT) |
      static_cast<unsigned>(O_EXCL) | static_cast<unsigned>(O_NOFOLLOW) |
      static_cast<unsigned>(O_CLOEXEC);
  const int fd = open(partial.c_str(), flags, kPermissions);
  if (fd < 0) {
    throw std::runtime_error("cannot create media file");
  }
  std::size_t written = 0;
  while (written < size) {
    const auto count = write(fd, data + written, size - written);
    if (count < 0 && errno == EINTR) {
      continue;
    }
    if (count <= 0) {
      close(fd);
      unlink(partial.c_str());
      throw std::runtime_error("media write failed");
    }
    written += static_cast<std::size_t>(count);
  }
  const int synced = fsync(fd);
  const int closed = close(fd);
  if (synced != 0 || closed != 0) {
    unlink(partial.c_str());
    throw std::runtime_error("media sync failed");
  }
  std::filesystem::rename(partial, path);
  const int directory =
      open(path.parent_path().c_str(), static_cast<unsigned>(O_RDONLY) |
                                           static_cast<unsigned>(O_DIRECTORY) |
                                           static_cast<unsigned>(O_CLOEXEC));
  if (directory < 0) {
    throw std::runtime_error("cannot open media directory");
  }
  const int result = fsync(directory);
  close(directory);
  if (result != 0) {
    throw std::runtime_error("media directory sync failed");
  }
}

std::vector<char> Encode(const raven::contracts::CameraFrame& frame,
                         int quality) {
  auto* pixbuf = gdk_pixbuf_new_from_data(
      frame.data.data(), GDK_COLORSPACE_RGB, false, kBits, frame.width,
      frame.height, frame.width * kChannels, nullptr, nullptr);
  if (!pixbuf) {
    throw std::runtime_error("cannot wrap photo pixels");
  }
  gchar* data = nullptr;
  gsize size = 0;
  GError* error = nullptr;
  const auto value = std::to_string(quality);
  const bool saved = gdk_pixbuf_save_to_buffer(
      pixbuf, &data, &size, "jpeg", &error, "quality", value.c_str(), nullptr);
  g_object_unref(pixbuf);
  if (!saved) {
    const std::string message =
        error ? error->message : "photo encoding failed";
    g_clear_error(&error);
    g_free(data);
    throw std::runtime_error(message);
  }
  const std::unique_ptr<gchar, decltype(&g_free)> owned(data, g_free);
  return {owned.get(), owned.get() + size};
}
}  // namespace

PhotoStore::PhotoStore(const std::string& root, int max_photos)
    : root_(std::filesystem::absolute(root).lexically_normal()),
      max_photos_(max_photos) {
  std::filesystem::create_directories(root_ / "photos");
  std::filesystem::create_directories(root_ / "recordings");
  std::ifstream counter(root_ / "capture-index");
  if (counter.good() && !(counter >> index_)) {
    throw std::runtime_error("invalid persistent capture index");
  }
  // Recover an interrupted reservation without replacing a committed index
  // with a shorter, partially written value. Gaps are safe; reused IDs are not.
  const auto partial = root_ / "capture-index.part";
  if (std::filesystem::exists(partial)) {
    std::ifstream pending(partial);
    std::uint32_t reserved = 0;
    if ((pending >> reserved) && reserved > index_) {
      index_ = reserved;
      pending.close();
      std::filesystem::rename(partial, root_ / "capture-index");
    } else {
      pending.close();
      std::filesystem::remove(partial);
    }
  }
}

std::uint32_t PhotoStore::Count() const { return index_; }

std::uint64_t PhotoStore::FreeBytes() const {
  return std::filesystem::space(root_).available;
}

std::uint64_t PhotoStore::TotalBytes() const {
  return std::filesystem::space(root_).capacity;
}

Photo PhotoStore::Write(const raven::contracts::CameraFrame& frame,
                        int quality) {
  if (FreeBytes() < kMinimumFreeBytes ||
      index_ == std::numeric_limits<std::uint32_t>::max()) {
    throw std::runtime_error(
        "insufficient photo storage or capture index exhausted");
  }
  const auto count =
      std::distance(std::filesystem::directory_iterator(root_ / "photos"),
                    std::filesystem::directory_iterator{});
  if (count >= max_photos_) {
    throw std::runtime_error("photo retention limit reached");
  }
  ++index_;
  const auto counter = std::to_string(index_) + "\n";
  // Reserve the index before writing the photo; power loss may leave a gap,
  // but a successful capture identifier is never reused.
  WriteDurable(root_ / "capture-index", counter.data(), counter.size());
  const auto bytes = Encode(frame, quality);
  const auto path = root_ / "photos" /
                    ("photo-" + std::to_string(index_) + "-" +
                     std::to_string(frame.captured_unix_us) + ".jpg");
  WriteDurable(path, bytes.data(), bytes.size());
  return {{},
          "file://" + path.string(),
          frame.optical_frame,
          frame.captured_unix_us,
          index_,
          frame.width,
          frame.height,
          frame.source_stamp_ns};
}
}  // namespace raven::media
