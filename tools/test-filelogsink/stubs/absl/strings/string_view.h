// Stub for standalone test of FileLogSink. The real WebRTC tree provides
// absl::string_view as a pre-C++17 polyfill; we alias to std::string_view
// so the test driver builds without the absl dependency.
#pragma once

#include <string_view>

namespace absl {
using string_view = std::string_view;
}
