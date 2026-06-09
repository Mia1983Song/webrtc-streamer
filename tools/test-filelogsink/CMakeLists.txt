# Standalone test driver for FileLogSink — does NOT depend on the WebRTC tree.
# Uses stub headers under stubs/ to satisfy FileLogSink.h's
# absl::string_view + webrtc::LogSink includes.
#
# Build:
#   cmake -B tools/test-filelogsink/build -S tools/test-filelogsink
#   cmake --build tools/test-filelogsink/build --config Release
# Run:
#   tools/test-filelogsink/build/Release/test_filelogsink.exe   (MSVC)
#   tools/test-filelogsink/build/test_filelogsink               (single-config gen)
cmake_minimum_required(VERSION 3.20)
project(test_filelogsink CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Reach up to the repo root for src/ and inc/
get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

add_executable(test_filelogsink
	test_filelogsink.cpp
	${REPO_ROOT}/src/FileLogSink.cpp
)

target_include_directories(test_filelogsink PRIVATE
	# stubs MUST come before the real include paths so the fake
	# absl::string_view + webrtc::LogSink win the search
	${CMAKE_CURRENT_SOURCE_DIR}/stubs
	${REPO_ROOT}/inc
)

# Thread support for the multithread test
find_package(Threads REQUIRED)
target_link_libraries(test_filelogsink PRIVATE Threads::Threads)

if (MSVC)
	target_compile_options(test_filelogsink PRIVATE /W4 /permissive-)
else()
	target_compile_options(test_filelogsink PRIVATE -Wall -Wextra -Wpedantic)
endif()
