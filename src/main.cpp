#include <iostream>
#include <exception>
#include <vector>
#include <string>
#include <algorithm>
#include <dirent.h>
#include <sys/stat.h>
#include <SDL2/SDL.h>
#include "core/VideoPlayer.h"

// Simple bitmap font for rendering text using 5x7 pixel patterns
void drawText(SDL_Renderer* renderer, const std::string& text, int x, int y, SDL_Color color) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);

    // Simple 5x7 bitmap font patterns
    static const unsigned char font[128][7] = {
        [' '] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
        ['A'] = {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11},
        ['B'] = {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E},
        ['C'] = {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E},
        ['D'] = {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E},
        ['E'] = {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F},
        ['F'] = {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10},
        ['G'] = {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F},
        ['H'] = {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11},
        ['I'] = {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E},
        ['J'] = {0x0F, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C},
        ['K'] = {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11},
        ['L'] = {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F},
        ['M'] = {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11},
        ['N'] = {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11},
        ['O'] = {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E},
        ['P'] = {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10},
        ['Q'] = {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D},
        ['R'] = {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11},
        ['S'] = {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E},
        ['T'] = {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04},
        ['U'] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E},
        ['V'] = {0x11, 0x11, 0x11, 0x11, 0x0A, 0x0A, 0x04},
        ['W'] = {0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11},
        ['X'] = {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11},
        ['Y'] = {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04},
        ['Z'] = {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F},
        ['a'] = {0x00, 0x00, 0x0E, 0x01, 0x0F, 0x11, 0x0F},
        ['b'] = {0x10, 0x10, 0x1E, 0x11, 0x11, 0x11, 0x1E},
        ['c'] = {0x00, 0x00, 0x0E, 0x10, 0x10, 0x11, 0x0E},
        ['d'] = {0x01, 0x01, 0x0F, 0x11, 0x11, 0x11, 0x0F},
        ['e'] = {0x00, 0x00, 0x0E, 0x11, 0x1F, 0x10, 0x0E},
        ['f'] = {0x06, 0x09, 0x08, 0x1C, 0x08, 0x08, 0x08},
        ['g'] = {0x00, 0x00, 0x0F, 0x11, 0x0F, 0x01, 0x0E},
        ['h'] = {0x10, 0x10, 0x16, 0x19, 0x11, 0x11, 0x11},
        ['i'] = {0x04, 0x00, 0x0C, 0x04, 0x04, 0x04, 0x0E},
        ['j'] = {0x02, 0x00, 0x06, 0x02, 0x02, 0x12, 0x0C},
        ['k'] = {0x10, 0x10, 0x12, 0x14, 0x18, 0x14, 0x12},
        ['l'] = {0x0C, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E},
        ['m'] = {0x00, 0x00, 0x1A, 0x15, 0x15, 0x11, 0x11},
        ['n'] = {0x00, 0x00, 0x16, 0x19, 0x11, 0x11, 0x11},
        ['o'] = {0x00, 0x00, 0x0E, 0x11, 0x11, 0x11, 0x0E},
        ['p'] = {0x00, 0x00, 0x1E, 0x11, 0x1E, 0x10, 0x10},
        ['q'] = {0x00, 0x00, 0x0F, 0x11, 0x0F, 0x01, 0x01},
        ['r'] = {0x00, 0x00, 0x16, 0x19, 0x10, 0x10, 0x10},
        ['s'] = {0x00, 0x00, 0x0E, 0x10, 0x0E, 0x01, 0x1E},
        ['t'] = {0x08, 0x08, 0x1C, 0x08, 0x08, 0x09, 0x06},
        ['u'] = {0x00, 0x00, 0x11, 0x11, 0x11, 0x13, 0x0D},
        ['v'] = {0x00, 0x00, 0x11, 0x11, 0x11, 0x0A, 0x04},
        ['w'] = {0x00, 0x00, 0x11, 0x11, 0x15, 0x15, 0x0A},
        ['x'] = {0x00, 0x00, 0x11, 0x0A, 0x04, 0x0A, 0x11},
        ['y'] = {0x00, 0x00, 0x11, 0x11, 0x0F, 0x01, 0x0E},
        ['z'] = {0x00, 0x00, 0x1F, 0x02, 0x04, 0x08, 0x1F},
        ['0'] = {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E},
        ['1'] = {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},
        ['2'] = {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F},
        ['3'] = {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E},
        ['4'] = {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02},
        ['5'] = {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E},
        ['6'] = {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E},
        ['7'] = {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},
        ['8'] = {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E},
        ['9'] = {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C},
        ['.'] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C},
        ['-'] = {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00},
        ['_'] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F},
    };

    for (size_t i = 0; i < text.length(); i++) {
        char c = text[i];
        if (c < 0 || c > 127) c = ' '; // Handle invalid characters

        // Draw each row of the character
        for (int row = 0; row < 7; row++) {
            unsigned char pattern = font[(int)c][row];
            for (int col = 0; col < 5; col++) {
                if (pattern & (1 << (4 - col))) {
                    SDL_RenderDrawPoint(renderer, x + (int)i * 6 + col, y + row);
                }
            }
        }
    }
}

// Simple SDL-based graphical file picker
std::string showFilePicker() {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::cerr << "SDL_Init Error: " << SDL_GetError() << std::endl;
        return "";
    }

    SDL_Window* window = SDL_CreateWindow("Select Video File",
                                         SDL_WINDOWPOS_CENTERED,
                                         SDL_WINDOWPOS_CENTERED,
                                         600, 400,
                                         SDL_WINDOW_SHOWN);
    if (!window) {
        std::cerr << "SDL_CreateWindow Error: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return "";
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        std::cerr << "SDL_CreateRenderer Error: " << SDL_GetError() << std::endl;
        SDL_DestroyWindow(window);
        SDL_Quit();
        return "";
    }

    std::vector<std::string> videoFiles;
    std::string currentDir = "./";

    // Get current directory files
    DIR* dir = opendir(currentDir.c_str());
    if (dir) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            std::string filename = entry->d_name;
            if (filename == "." || filename == "..") continue;

            struct stat statbuf;
            std::string fullpath = currentDir + "/" + filename;
            if (stat(fullpath.c_str(), &statbuf) == 0) {
                if (!S_ISDIR(statbuf.st_mode)) {
                    // Check for video file extensions
                    std::string ext = filename.substr(filename.find_last_of(".") + 1);
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                    if (ext == "mp4" || ext == "avi" || ext == "mkv" || ext == "mov" ||
                        ext == "wmv" || ext == "flv" || ext == "webm" || ext == "m4v") {
                        videoFiles.push_back(filename);
                    }
                }
            }
        }
        closedir(dir);
    }

    if (videoFiles.empty()) {
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        std::cout << "No video files found in the current directory." << std::endl;
        return "";
    }

    int selectedIndex = 0;
    bool running = true;
    std::string selectedFile = "";

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                running = false;
            } else if (e.type == SDL_KEYDOWN) {
                switch (e.key.keysym.sym) {
                    case SDLK_UP:
                        if (selectedIndex > 0) selectedIndex--;
                        break;
                    case SDLK_DOWN:
                        if (selectedIndex < (int)videoFiles.size() - 1) selectedIndex++;
                        break;
                    case SDLK_RETURN:
                        if (selectedIndex < (int)videoFiles.size()) {
                            selectedFile = currentDir + "/" + videoFiles[selectedIndex];
                            running = false;
                        }
                        break;
                    case SDLK_ESCAPE:
                        running = false;
                        break;
                }
            } else if (e.type == SDL_MOUSEBUTTONDOWN) {
                if (e.button.button == SDL_BUTTON_LEFT) {
                    int mouseY = e.button.y;
                    int itemHeight = 30;
                    int startY = 80;

                    int clickedIndex = (mouseY - startY) / itemHeight;
                    if (clickedIndex >= 0 && clickedIndex < (int)videoFiles.size()) {
                        selectedIndex = clickedIndex;
                        selectedFile = currentDir + "/" + videoFiles[selectedIndex];
                        running = false;
                    }
                }
            }
        }

        // Clear screen
        SDL_SetRenderDrawColor(renderer, 30, 30, 30, 255);
        SDL_RenderClear(renderer);

        // Draw title bar
        SDL_SetRenderDrawColor(renderer, 70, 70, 70, 255);
        SDL_Rect titleRect = {0, 0, 600, 50};
        SDL_RenderFillRect(renderer, &titleRect);

        // Draw title text
        SDL_Color titleColor = {255, 255, 255, 255};
        drawText(renderer, "Select Video File", 20, 20, titleColor);

        // Draw file list background
        SDL_SetRenderDrawColor(renderer, 50, 50, 50, 255);
        SDL_Rect listRect = {10, 60, 580, 330};
        SDL_RenderFillRect(renderer, &listRect);

        // Draw files as colored rectangles with text
        int y = 80;
        for (int i = 0; i < (int)videoFiles.size() && y < 380; i++) {
            SDL_Rect fileRect = {20, y, 560, 25};

            if (i == selectedIndex) {
                // Highlight selected file
                SDL_SetRenderDrawColor(renderer, 100, 150, 255, 255);
            } else {
                // Normal file color
                SDL_SetRenderDrawColor(renderer, 80, 80, 80, 255);
            }

            SDL_RenderFillRect(renderer, &fileRect);

            // Draw border
            SDL_SetRenderDrawColor(renderer, 120, 120, 120, 255);
            SDL_RenderDrawRect(renderer, &fileRect);

            // Draw filename text
            SDL_Color textColor = {255, 255, 255, 255}; // White text
            if (i == selectedIndex) {
                textColor = {255, 255, 0, 255}; // Yellow text for selected
            }
            drawText(renderer, videoFiles[i], 25, y + 8, textColor);

            y += 30;
        }

        // Draw instructions at bottom
        SDL_SetRenderDrawColor(renderer, 40, 40, 40, 255);
        SDL_Rect instructRect = {0, 360, 600, 40};
        SDL_RenderFillRect(renderer, &instructRect);

        // Draw instruction text
        SDL_Color instructColor = {200, 200, 200, 255};
        drawText(renderer, "Use arrows keys or click to select  Enter to open  Esc to cancel", 10, 370, instructColor);

        SDL_RenderPresent(renderer);
        SDL_Delay(16); // ~60 FPS
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return selectedFile;
}

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
        std::string video_file;
        std::string config_file = "";

        if (argc == 1) {
            // No arguments provided, show file picker
            std::cout << "Enhanced Video Player with Threaded Audio Support" << std::endl;
            std::cout << "No file specified. Opening file picker..." << std::endl;

            video_file = showFilePicker();
            if (video_file.empty()) {
                std::cout << "No file selected. Exiting..." << std::endl;
                return 0;
            }
        } else if (argc == 2 || argc == 3) {
            // Use command line argument if provided
            video_file = argv[1];
            if (argc == 3) {
                config_file = argv[2];
            }
        } else {
            print_usage(argv[0]);
            return -1;
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