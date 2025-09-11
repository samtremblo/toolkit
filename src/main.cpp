#include <iostream>
#include <exception>
#include "core/VideoPlayer.h"

void print_usage(const char* program_name) {
    std::cout << "Enhanced Video Player with Threaded Audio Support" << std::endl;
    std::cout << "Usage: " << program_name << " <video_file> [config_file]" << std::endl;
    std::cout << "  video_file   : Path to video file to play" << std::endl;
    std::cout << "  config_file  : Optional network configuration file" << std::endl;
    std::cout << std::endl;
    std::cout << "Controls:" << std::endl;
    std::cout << "  0-9          : Seek to percentage (0%, 10%, ..., 90%)" << std::endl;
    std::cout << "  Space        : Pause/Resume" << std::endl;
    std::cout << "  M            : Mute/Unmute audio" << std::endl;
    std::cout << "  S            : Enable/Disable network sync" << std::endl;
    std::cout << "  Shift+S      : Toggle sync master mode" << std::endl;
    std::cout << "  Q/ESC        : Quit" << std::endl;
}

int main(int argc, char* argv[]) {
    try {
        if (argc < 2 || argc > 3) {
            print_usage(argv[0]);
            return -1;
        }
        
        std::string video_file = argv[1];
        std::string config_file = "";
        
        if (argc == 3) {
            config_file = argv[2];
        }
        
        std::cout << "Starting Enhanced Video Player with crash protection..." << std::endl;
        
        std::unique_ptr<VideoPlayer> player;
        
        if (!config_file.empty()) {
            std::cout << "Using network config: " << config_file << std::endl;
            player = std::make_unique<VideoPlayer>(config_file);
        } else {
            player = std::make_unique<VideoPlayer>();
        }
        
        if (player->load_video(video_file)) {
            player->play();
        }
        
        std::cout << "Playback completed successfully" << std::endl;
        return 0;
        
    } catch (const std::exception& e) {
        std::cout << "Exception caught: " << e.what() << std::endl;
        return -1;
    } catch (...) {
        std::cout << "Unknown exception caught" << std::endl;
        return -1;
    }
}