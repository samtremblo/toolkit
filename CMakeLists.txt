cmake_minimum_required(VERSION 3.10)
project(VideoPlayer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenCV REQUIRED)
find_package(PkgConfig REQUIRED)

# Include directories
include_directories(include)

# Source files
set(SOURCES
    src/main.cpp
    src/core/VideoPlayer.cpp
    src/core/FFmpegResourceManager.cpp
    src/core/NetworkConfig.cpp
    src/audio/AudioManager.cpp
    src/audio/AudioClock.cpp
    src/video/VideoManager.cpp
    src/utils/CircularBuffer.cpp
    src/network/SyncProtocol.cpp
    src/network/UDPSyncServer.cpp
    src/network/UDPSyncSender.cpp
    src/network/SyncManager.cpp
)

add_executable(video_player ${SOURCES})

target_link_libraries(video_player ${OpenCV_LIBS})
target_include_directories(video_player PRIVATE ${OpenCV_INCLUDE_DIRS})

# Find FFmpeg libraries
pkg_check_modules(LIBAV REQUIRED IMPORTED_TARGET
    libavcodec
    libavformat
    libavutil
    libswresample
    libswscale
)

if(LIBAV_FOUND)
    target_link_libraries(video_player PkgConfig::LIBAV)
    message(STATUS "FFmpeg found - video and audio support enabled")
endif()

# Add SDL2 for cross-platform audio output and UI
find_package(SDL2 REQUIRED)
target_link_libraries(video_player ${SDL2_LIBRARIES})
target_include_directories(video_player PRIVATE ${SDL2_INCLUDE_DIRS})

# Add threading support
find_package(Threads REQUIRED)
target_link_libraries(video_player Threads::Threads)