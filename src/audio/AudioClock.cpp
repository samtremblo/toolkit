#include "audio/AudioClock.h"

AudioClock::AudioClock() {
    base_time = std::chrono::high_resolution_clock::now();
}

void AudioClock::set(double timestamp) {
    std::lock_guard<std::mutex> lock(clock_mutex);
    pts.store(timestamp);
    base_time = std::chrono::high_resolution_clock::now();
}

double AudioClock::get() const {
    auto now = std::chrono::high_resolution_clock::now();
    double elapsed = std::chrono::duration<double>(now - base_time).count();
    return pts.load() + elapsed;
}