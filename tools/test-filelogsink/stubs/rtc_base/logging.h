// Stub for standalone test of FileLogSink. Provides only the minimal surface
// (LoggingSeverity enum + LogSink interface with the two overloads FileLogSink
// overrides). Do NOT include this in the real build — the WebRTC tree's
// rtc_base/logging.h is the canonical version.
#pragma once

#include <string>

#include "absl/strings/string_view.h"

namespace webrtc {

enum LoggingSeverity {
	LS_VERBOSE = 0,
	LS_INFO    = 1,
	LS_WARNING = 2,
	LS_ERROR   = 3,
	LS_NONE    = 4,
};

class LogSink {
public:
	virtual ~LogSink() = default;
	virtual void OnLogMessage(const std::string& msg) = 0;
	virtual void OnLogMessage(absl::string_view msg, LoggingSeverity severity, const char* tag) = 0;
};

}  // namespace webrtc
