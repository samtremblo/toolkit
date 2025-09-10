#pragma once
#include <atomic>
#include <chrono>
#include <mutex>

class AudioClock {
private:
    std::atomic<double> pts{0.0};
    std::chrono::high_resolution_clock::time_point base_time;
    std::mutex clock_mutex;
    
public:
    AudioClock();
    
    void set(double timestamp);
    double get() const;
};