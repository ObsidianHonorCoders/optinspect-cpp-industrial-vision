#pragma once

#include "GuiTypes.hpp"

#include <string>
#include <unordered_set>

namespace optinspect::gui {

/// "<absolute path>|<last write time ticks>|<file size>" - the identity of an
/// image file's content for texture caching. A file rewritten on disk gets a
/// different key, which invalidates the cached texture implicitly.
/// Returns an empty string when the file does not exist.
std::string makeImageCacheKey(const std::string& path);

/// Loads one image for display on the calling thread (never call it from the
/// render thread). A missing/undecodable file yields a slot with `error` set -
/// never an exception, never a substituted file. When the file's cache key is in
/// `cached_keys` the decode is skipped and `already_cached` is set.
/// 16-bit images are scaled to 8 bit so every loaded image can become a texture.
ImageSlot loadImageSlot(const std::string& path, const std::unordered_set<std::string>& cached_keys);

}  // namespace optinspect::gui
