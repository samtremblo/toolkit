#!/bin/bash

echo "Building Enhanced Video Player..."
echo "================================"

# Create build directory
mkdir -p build

# Clean previous build
echo "Cleaning previous build..."
rm -f video_player
rm -rf build/*

# Configure with CMake
echo "Configuring with CMake..."
cmake -B build .

if [ $? -ne 0 ]; then
    echo "CMake configuration failed!"
    exit 1
fi

# Build
echo "Building..."
cd build && make

if [ $? -eq 0 ]; then
    echo "Build successful!"
    echo ""
    echo "Executable location: build/video_player"
    echo ""
    echo "Usage:"
    echo "  ./build/video_player your_video.mp4"
    echo ""
    echo "Features:"
    echo "  - Real-time FPS display (Target vs Actual)"
    echo "  - Audio support with SDL2"
    echo "  - Video codec and resolution info"
    echo "  - Progress bar and time display"
    echo "  - Playback speed control (+/-)"
    echo "  - Audio mute/unmute (M key)"
    echo "  - Seeking (arrow keys)"
    echo "  - Play/Pause (Space), Restart (R), Quit (Q/ESC)"
else
    echo "Build failed!"
    exit 1
fi