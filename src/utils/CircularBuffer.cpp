#include "utils/CircularBuffer.h"
#include <algorithm>

CircularAudioBuffer::CircularAudioBuffer(size_t size) : capacity(size) {
    buffer.resize(capacity);
}

size_t CircularAudioBuffer::write(const uint8_t* data, size_t size) {
    std::lock_guard<std::mutex> lock(buffer_mutex);
    size_t available = available_write();
    size_t to_write = std::min(size, available);
    
    size_t w_pos = write_pos.load();
    if (w_pos + to_write <= capacity) {
        memcpy(buffer.data() + w_pos, data, to_write);
    } else {
        size_t first_chunk = capacity - w_pos;
        memcpy(buffer.data() + w_pos, data, first_chunk);
        memcpy(buffer.data(), data + first_chunk, to_write - first_chunk);
    }
    
    write_pos.store((w_pos + to_write) % capacity);
    return to_write;
}

size_t CircularAudioBuffer::read(uint8_t* data, size_t size) {
    std::lock_guard<std::mutex> lock(buffer_mutex);
    size_t available = available_read();
    size_t to_read = std::min(size, available);
    
    size_t r_pos = read_pos.load();
    if (r_pos + to_read <= capacity) {
        memcpy(data, buffer.data() + r_pos, to_read);
    } else {
        size_t first_chunk = capacity - r_pos;
        memcpy(data, buffer.data() + r_pos, first_chunk);
        memcpy(data + first_chunk, buffer.data(), to_read - first_chunk);
    }
    
    read_pos.store((r_pos + to_read) % capacity);
    return to_read;
}

size_t CircularAudioBuffer::available_read() const {
    size_t w = write_pos.load();
    size_t r = read_pos.load();
    return (w >= r) ? (w - r) : (capacity - r + w);
}

size_t CircularAudioBuffer::available_write() const {
    return capacity - available_read() - 1; // Leave one byte to distinguish full/empty
}

void CircularAudioBuffer::clear() {
    std::lock_guard<std::mutex> lock(buffer_mutex);
    write_pos.store(0);
    read_pos.store(0);
}