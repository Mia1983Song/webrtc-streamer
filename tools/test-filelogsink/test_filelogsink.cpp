// Standalone test driver for FileLogSink. Pulls in the production
// src/FileLogSink.cpp directly; the WebRTC/absl dependencies are replaced by
// the stubs/ headers (see CMakeLists.txt for include order).
//
// Run: tools/test-filelogsink/build/test_filelogsink[.exe]
//
// Exits 0 on success, 1 on first failure. No external test framework.

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

#include "FileLogSink.h"

namespace fs = std::filesystem;

#define CHECK(cond)                                                              \
	do {                                                                         \
		if (!(cond)) {                                                           \
			std::fprintf(stderr, "FAIL [%s:%d]: %s\n", __FILE__, __LINE__, #cond); \
			std::exit(1);                                                        \
		}                                                                        \
	} while (0)

static std::string read_file(const fs::path& p) {
	std::ifstream f(p, std::ios::binary);
	return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

static void reset_dir(const fs::path& dir) {
	std::error_code ec;
	fs::remove_all(dir, ec);
	fs::create_directories(dir, ec);
}

// -----------------------------------------------------------------------------
// 1. basic write — sink writes, file exists, content lands in order
// -----------------------------------------------------------------------------
static void test_basic_write() {
	fs::path dir = "test-out/basic";
	reset_dir(dir);
	fs::path log = dir / "test.log";

	{
		FileLogSink sink(log.string(), 10 * 1024 * 1024, 5);
		for (int i = 0; i < 10; ++i) {
			sink.OnLogMessage("line " + std::to_string(i) + "\n");
		}
	}

	CHECK(fs::exists(log));
	std::string content = read_file(log);
	CHECK(content.find("line 0\n") != std::string::npos);
	CHECK(content.find("line 9\n") != std::string::npos);
	// strict ordering since we wrote single-threaded
	CHECK(content.find("line 0") < content.find("line 9"));

	std::printf("[PASS] basic write (%zu bytes)\n", content.size());
}

// -----------------------------------------------------------------------------
// 2. rotation — exceeding maxBytes shifts files; oldest gets dropped
// -----------------------------------------------------------------------------
static void test_rotation() {
	fs::path dir = "test-out/rotation";
	reset_dir(dir);
	fs::path log = dir / "test.log";

	// 1 KB cap × 4 files total (current + .1 + .2 + .3); .4 should never appear.
	// 50 × ~100 B = ~5 KB written → at least 4 rotation cycles.
	{
		FileLogSink sink(log.string(), 1024, 4);
		std::string line(99, 'A');
		line.push_back('\n');  // 100 bytes total
		for (int i = 0; i < 50; ++i) {
			sink.OnLogMessage(line);
		}
	}

	CHECK(fs::exists(log));
	CHECK(fs::exists(log.string() + ".1"));
	CHECK(fs::exists(log.string() + ".2"));
	CHECK(fs::exists(log.string() + ".3"));
	// .4 must not exist — that's the oldest-dropped slot
	CHECK(!fs::exists(log.string() + ".4"));

	// current file's size should be below cap (it's freshly rotated)
	CHECK(fs::file_size(log) <= 1024);

	std::printf("[PASS] rotation (4 files present, no .4)\n");
}

// -----------------------------------------------------------------------------
// 3. append across restart — second session keeps first session's content
// -----------------------------------------------------------------------------
static void test_append_across_restart() {
	fs::path dir = "test-out/append";
	reset_dir(dir);
	fs::path log = dir / "test.log";

	{
		FileLogSink sink(log.string(), 10 * 1024 * 1024, 5);
		sink.OnLogMessage("session1\n");
	}
	size_t size1 = fs::file_size(log);

	{
		FileLogSink sink(log.string(), 10 * 1024 * 1024, 5);
		sink.OnLogMessage("session2\n");
	}
	size_t size2 = fs::file_size(log);

	CHECK(size2 > size1);
	std::string content = read_file(log);
	CHECK(content.find("session1") != std::string::npos);
	CHECK(content.find("session2") != std::string::npos);

	std::printf("[PASS] append across restart (%zu -> %zu bytes)\n", size1, size2);
}

// -----------------------------------------------------------------------------
// 4. multithread — N threads × M lines each, no loss, no interleaved bytes
// within a single OnLogMessage call (each call is atomic under m_mutex).
// -----------------------------------------------------------------------------
static void test_multithread() {
	fs::path dir = "test-out/multithread";
	reset_dir(dir);
	fs::path log = dir / "test.log";

	constexpr int kThreads       = 5;
	constexpr int kLinesPerThread = 10000;

	{
		// 50 MB cap → no rotation expected in this test (we want to assert
		// every line lands so a known total count works).
		FileLogSink sink(log.string(), 50 * 1024 * 1024, 5);

		std::vector<std::thread> threads;
		threads.reserve(kThreads);
		for (int t = 0; t < kThreads; ++t) {
			threads.emplace_back([&sink, t]() {
				for (int i = 0; i < kLinesPerThread; ++i) {
					char buf[64];
					std::snprintf(buf, sizeof(buf), "T%d:L%05d\n", t, i);
					sink.OnLogMessage(std::string(buf));
				}
			});
		}
		for (auto& th : threads) th.join();
	}

	std::string content = read_file(log);

	int newline_count = 0;
	for (char c : content) {
		if (c == '\n') newline_count++;
	}
	CHECK(newline_count == kThreads * kLinesPerThread);

	// per-thread: first, middle, last marker must all be present
	for (int t = 0; t < kThreads; ++t) {
		char marker_first[16], marker_mid[16], marker_last[16];
		std::snprintf(marker_first, sizeof(marker_first), "T%d:L00000\n", t);
		std::snprintf(marker_mid,   sizeof(marker_mid),   "T%d:L05000\n", t);
		std::snprintf(marker_last,  sizeof(marker_last),  "T%d:L09999\n", t);
		CHECK(content.find(marker_first) != std::string::npos);
		CHECK(content.find(marker_mid)   != std::string::npos);
		CHECK(content.find(marker_last)  != std::string::npos);
	}

	std::printf("[PASS] multithread (%d lines, no loss, no interleave)\n", newline_count);
}

// -----------------------------------------------------------------------------
// 5. oversized single message — must not spin rotation on an empty file
// (guard at FileLogSink.cpp: m_currentBytes > 0 check before rotateLocked)
// -----------------------------------------------------------------------------
static void test_oversized_message() {
	fs::path dir = "test-out/oversized";
	reset_dir(dir);
	fs::path log = dir / "test.log";

	{
		FileLogSink sink(log.string(), 1024, 4);
		// single 5 KB message — bigger than the cap, but on an empty file
		// must just land without infinite rotation
		std::string huge(5000, 'X');
		huge.push_back('\n');
		sink.OnLogMessage(huge);
	}

	CHECK(fs::exists(log));
	CHECK(fs::file_size(log) > 5000);
	// no rotated archives should exist — the guard prevented spinning
	CHECK(!fs::exists(log.string() + ".1"));

	std::printf("[PASS] oversized single message (%zu bytes, no spurious rotation)\n",
	            static_cast<size_t>(fs::file_size(log)));
}

// -----------------------------------------------------------------------------
int main() {
	std::printf("=== FileLogSink standalone tests ===\n");
	test_basic_write();
	test_rotation();
	test_append_across_restart();
	test_multithread();
	test_oversized_message();
	std::printf("\nALL TESTS PASSED\n");
	return 0;
}
