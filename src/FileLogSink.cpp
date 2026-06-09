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

#include "FileLogSink.h"

#include <cstdio>
#include <filesystem>
#include <ios>

// NOTE: never call RTC_LOG from inside a LogSink: the webrtc logging mutex is
// held while sinks run, so re-entering the logger would deadlock. Internal
// errors go to stderr directly.

FileLogSink::FileLogSink(const std::string& path, size_t maxBytes, int maxFiles)
	: m_path(path), m_maxBytes(maxBytes), m_maxFiles(maxFiles), m_currentBytes(0)
{
	std::error_code ec;
	std::filesystem::path p(m_path);
	if (p.has_parent_path())
	{
		std::filesystem::create_directories(p.parent_path(), ec);
	}

	// Append across restarts so a previous session's log is not wiped; seed
	// m_currentBytes with the existing size so rotation still triggers.
	if (std::filesystem::exists(p, ec))
	{
		m_currentBytes = static_cast<size_t>(std::filesystem::file_size(p, ec));
		if (ec)
		{
			m_currentBytes = 0;
		}
	}

	// binary mode: WebRTC log lines already carry '\n'; let the OS NOT rewrite
	// them to "\r\n" on Windows so message bytes reach disk verbatim.
	m_stream.open(m_path, std::ios::out | std::ios::app | std::ios::binary);
	if (!m_stream.is_open())
	{
		std::fprintf(stderr, "FileLogSink: cannot open log file '%s'\n", m_path.c_str());
	}
}

FileLogSink::~FileLogSink()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	if (m_stream.is_open())
	{
		m_stream.flush();
		m_stream.close();
	}
}

void FileLogSink::OnLogMessage(const std::string& msg)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	// [FORK] severity is unavailable on this overload (it is the pure virtual the
	// base default-forwarding chain funnels into). Treat it as LS_ERROR so a
	// message arriving via this path is flushed and never lost on a crash.
	writeLocked(msg, webrtc::LS_ERROR);
}

void FileLogSink::OnLogMessage(absl::string_view msg, webrtc::LoggingSeverity severity, const char* /*tag*/)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	writeLocked(msg, severity);
}

bool FileLogSink::isOpen() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_stream.is_open();
}

void FileLogSink::writeLocked(absl::string_view msg, webrtc::LoggingSeverity severity)
{
	if (!m_stream.is_open())
	{
		return;
	}

	// Rotate before writing if this message would exceed the cap. Guard on
	// m_currentBytes > 0 so a single oversized message never spins rotation on
	// an empty file.
	if (m_maxBytes > 0 && m_currentBytes > 0 && m_currentBytes + msg.size() > m_maxBytes)
	{
		rotateLocked();
	}

	// [FORK] rotateLocked() may have failed to reopen the fresh file; don't write to a
	// closed stream and don't inflate m_currentBytes for bytes that never landed.
	if (!m_stream.is_open())
	{
		return;
	}

	m_stream.write(msg.data(), static_cast<std::streamsize>(msg.size()));

	// [FORK] OnLogMessage runs while WebRTC holds its global logging lock, so a
	// synchronous flush on EVERY record serializes all logging threads behind one
	// disk write -- under an RTSP error/congestion storm that can stall the WebRTC
	// threads and worsen the very hangs this log exists to diagnose. Only force
	// durability for LS_WARNING and above (the RTSP 401/403/404 + congestion lines
	// we actually keep this log for); let INFO/VERBOSE ride the stream buffer and
	// land on buffer-fill, rotation, or close. Post-crash forensics stays intact
	// because the forensically interesting severities are still flushed.
	if (severity >= webrtc::LS_WARNING)
	{
		m_stream.flush();
	}

	// [FORK] A failed write/flush (e.g. transient disk-full) leaves the stream in a
	// fail state while still is_open(); without clearing it, every later write
	// silently no-ops until restart. Detect it, report once, and reopen in append
	// mode so logging self-heals when the disk recovers. The just-attempted bytes
	// may not have landed, so don't count them -- re-seed from the on-disk size.
	if (!m_stream.good())
	{
		std::fprintf(stderr, "FileLogSink: write failed for '%s', reopening\n", m_path.c_str());
		m_stream.clear();
		m_stream.close();
		m_stream.open(m_path, std::ios::out | std::ios::app | std::ios::binary);

		std::error_code ec;
		std::filesystem::path p(m_path);
		m_currentBytes = std::filesystem::exists(p, ec) ? static_cast<size_t>(std::filesystem::file_size(p, ec)) : 0;
		if (ec)
		{
			m_currentBytes = 0;
		}
		return;
	}

	m_currentBytes += msg.size();
}

void FileLogSink::rotateLocked()
{
	namespace fs = std::filesystem;
	std::error_code ec;

	if (m_stream.is_open())
	{
		m_stream.close();
	}

	// [FORK] Track whether the current file was actually archived. On Windows a held
	// handle (a tail tool or AV scanner on .1/.N) makes rename fail; the old code
	// swallowed every error_code, so a failed shift could clobber an un-shifted
	// archive or silently truncate un-rolled data with no trace.
	bool currentArchived = true;

	if (m_maxFiles > 1)
	{
		// Drop the oldest archive (<path>.<maxFiles-1>), then shift the chain up.
		fs::remove(fs::path(m_path + "." + std::to_string(m_maxFiles - 1)), ec);
		for (int i = m_maxFiles - 2; i >= 1; --i)
		{
			fs::path src(m_path + "." + std::to_string(i));
			fs::path dst(m_path + "." + std::to_string(i + 1));
			if (fs::exists(src, ec))
			{
				fs::rename(src, dst, ec);
				if (ec)
				{
					std::fprintf(stderr, "FileLogSink: rotate rename '%s' -> '%s' failed: %s\n",
						src.string().c_str(), dst.string().c_str(), ec.message().c_str());
					ec.clear();
				}
			}
		}
		if (fs::exists(fs::path(m_path), ec))
		{
			fs::rename(fs::path(m_path), fs::path(m_path + ".1"), ec);
			if (ec)
			{
				currentArchived = false;
				std::fprintf(stderr, "FileLogSink: rotate rename '%s' -> '%s.1' failed: %s\n",
					m_path.c_str(), m_path.c_str(), ec.message().c_str());
				ec.clear();
			}
		}
	}

	// [FORK] Only truncate when the current file was successfully archived. If the
	// roll-over failed (e.g. .1 locked), reopen in append mode instead -- better to
	// overshoot the size cap than to destroy log data we could not roll over. The
	// next message will retry rotation, so it self-heals once the lock clears.
	if (currentArchived)
	{
		m_stream.open(m_path, std::ios::out | std::ios::trunc | std::ios::binary);
		m_currentBytes = 0;
	}
	else
	{
		m_stream.open(m_path, std::ios::out | std::ios::app | std::ios::binary);
		std::filesystem::path p(m_path);
		m_currentBytes = fs::exists(p, ec) ? static_cast<size_t>(fs::file_size(p, ec)) : 0;
		if (ec)
		{
			m_currentBytes = 0;
		}
	}

	if (!m_stream.is_open())
	{
		std::fprintf(stderr, "FileLogSink: cannot reopen log file '%s' after rotation\n", m_path.c_str());
	}
}

// [FORK] END
