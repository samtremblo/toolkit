# Creative Technologist Toolkit

A great sage once said "if you have a nail, everything can be a hammer", I say "If you have a good hammer, you can nail a lot of things ;)"

## Goal

A comprehensive toolkit for creative technologists featuring essential utilities commonly needed across creative and technical workflows. Currently optimized for macOS with plans for full cross-platform compatibility.

**Vision**: Provide high-quality, performance-oriented tools that bridge the gap between creative exploration and technical implementation.

## Current Tools

### Enhanced Video Player
A high-performance video player built with C++ featuring threaded audio processing and precise audio-video synchronization. Demonstrates modern C++ practices with object-oriented design, RAII resource management, and multi-threaded architecture.

#### Video Player Features

- **Threaded Audio System**: Dedicated audio thread with circular buffer for smooth playback
- **Precise A/V Sync**: 40ms synchronization threshold with automatic drift correction
- **Video Caching**: Pre-loads entire video into memory for smooth playback
- **Real-time Controls**: Seeking (0-9 keys), pause/resume (Space), mute toggle (M)
- **Crash Protection**: Signal handlers for graceful cleanup on errors
- **Cross-platform Audio**: SDL2 integration for reliable audio output
- **FFmpeg Integration**: Supports wide range of video/audio formats

#### Video Player Architecture

```
├── src/
│   ├── core/           # Main orchestrator and FFmpeg resources
│   ├── audio/          # Threaded audio system with synchronization
│   ├── video/          # Video caching and rendering
│   └── utils/          # Circular buffer and utilities
├── include/            # Header files with clear interfaces
└── build/              # Generated build files
```

## Requirements (Video Player)

- C++17 compiler
- CMake 3.10+
- OpenCV 4.x
- FFmpeg libraries (avcodec, avformat, avutil, swresample, swscale)
- SDL2
- pkg-config

## Building (Video Player)

```bash
./build.sh
```

## Usage (Video Player)

```bash
./build/video_player your_video.mp4
```

#### Controls

- **0-9**: Seek to 0%, 10%, 20%, ..., 90%
- **Space**: Pause/Resume playback
- **M**: Mute/Unmute audio
- **Q/ESC**: Quit player

## Technical Details (Video Player)

- **Object-Oriented Design**: Clean separation of concerns with dedicated managers
- **Thread Safety**: Atomic variables and mutex protection for shared data
- **Memory Management**: RAII principles with smart pointers
- **Performance**: Lock-free circular buffer for audio, cached video frames
- **Error Handling**: Comprehensive error checking and graceful degradation

## Roadmap

- **Multi-Player Synchronization**: UDP cue system for synchronized playback across multiple video players over network
- **Audio Processing Tools**: Real-time audio analysis, effects, and synthesis
- **Image Processing Utilities**: Batch processing, format conversion, filtering
- **Data Visualization**: Interactive plotting and data exploration tools
- **Network Utilities**: Protocol testing, API clients, WebSocket tools
- **File Management**: Batch operations, metadata extraction, organization
- **Creative Coding Helpers**: Generative art tools, shader utilities
- **Cross-platform Support**: Windows and Linux compatibility

## Contributing

This toolkit aims to solve real problems for creative technologists. Each tool should be:
- High-performance and reliable
- Well-documented with clear examples
- Cross-platform (or moving toward it)
- Useful in real creative/technical workflows

## License

This project demonstrates modern programming techniques for creative technology applications and is intended for educational and creative purposes.