/* ---------------------------------------------------------------------------
 * SPDX-License-Identifier: Unlicense
 *
 * This is free and unencumbered software released into the public domain.
 *
 * Anyone is free to copy, modify, publish, use, compile, sell, or distribute this
 * software, either in source code form or as a compiled binary, for any purpose,
 * commercial or non-commercial, and by any means.
 *
 * For more information, please refer to <http://unlicense.org/>
 * -------------------------------------------------------------------------*/

// [FORK] BEGIN: persist RTC_LOG output to a size-rotated file
// Keeps a durable RTSP error history on the production Windows host.
// See docs/PLAN.md (Track A).

#pragma once

#include <cstddef>
#include <fstream>
#include <mutex>
#include <string>

#include "absl/strings/string_view.h"
#include "rtc_base/logging.h"

class FileLogSink : public webrtc::LogSink
{
	public:
		FileLogSink(const std::string& path, size_t maxBytes = 10 * 1024 * 1024, int maxFiles = 5);
		~FileLogSink() override;

		// webrtc::LogSink overrides. The severity/tag overload is the one the
		// LogMessage destructor calls; the plain overload is the pure virtual we
		// must implement and also covers any direct caller.
		void OnLogMessage(const std::string& msg) override;
		void OnLogMessage(absl::string_view msg, webrtc::LoggingSeverity severity, const char* tag) override;

		// [FORK] true if the backing file stream opened successfully; lets main.cpp
		// avoid reporting a misleading success when the log file cannot be opened.
		bool isOpen() const;

	private:
		void writeLocked(absl::string_view msg, webrtc::LoggingSeverity severity);   // caller must hold m_mutex
		void rotateLocked();                        // caller must hold m_mutex

		mutable std::mutex m_mutex;
		std::ofstream m_stream;
		std::string m_path;
		size_t m_maxBytes;
		int m_maxFiles;
		size_t m_currentBytes;
};

// [FORK] END
