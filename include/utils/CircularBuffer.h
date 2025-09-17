#pragma once
#include <vector>
#include <atomic>
#include <mutex>
#include <cstring>

class CircularAudioBuffer {
private:
    std::vector<uint8_t> buffer;
    std::atomic<size_t> write_pos{0};
    std::atomic<size_t> read_pos{0};
    size_t capacity;
    std::mutex buffer_mutex;
    
public:
    CircularAudioBuffer(size_t size);
    
    size_t write(const uint8_t* data, size_t size);
    size_t read(uint8_t* data, size_t size);
    size_t available_read() const;
    size_t available_write() const;
    void clear();
};