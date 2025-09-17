#pragma once
#include <memory>
#include <functional>
#include <random>
#include <string>

#include "network/UDPSyncServer.h"
#include "network/UDPSyncSender.h"
#include "network/SyncProtocol.h"
#include "core/NetworkConfig.h"

class VideoPlayer; // Forward declaration

class SyncManager {
public:
    using SyncCallback = std::function<void(uint32_t frame_number)>;
    using SeekCallback = std::function<void(double position)>;
    using PauseCallback = std::function<void()>;
    using ResumeCallback = std::function<void()>;
    using ClientCallback = std::function<void(uint32_t client_id, const std::string& name, const std::string& ip)>;
    
private:
    std::unique_ptr<UDPSyncServer> server;
    std::unique_ptr<UDPSyncSender> sender;
    
    uint32_t my_client_id;
    std::string my_client_name;
    uint16_t port;
    
    // Configuration
    std::unique_ptr<NetworkConfigParser> config_parser;
    NetworkConfig network_config;
    
    // Callbacks
    SyncCallback on_sync_cue;
    SeekCallback on_seek_cue;
    PauseCallback on_pause_cue;
    ResumeCallback on_resume_cue;
    ClientCallback on_client_connected;
    ClientCallback on_client_disconnected;
    
    bool enabled;
    
public:
    SyncManager(const std::string& client_name = "", uint16_t port = SyncProtocol::DEFAULT_PORT);
    
    // Factory methods to avoid constructor ambiguity
    static std::unique_ptr<SyncManager> create_with_config(const std::string& config_file_path);
    static std::unique_ptr<SyncManager> create_default(const std::string& client_name = "", uint16_t port = SyncProtocol::DEFAULT_PORT);
    ~SyncManager();
    
    bool initialize();
    bool initialize_with_config(const std::string& config_file_path);
    void shutdown();
    
    bool is_enabled() const { return enabled; }
    void set_enabled(bool enable);
    
    // Callback setters
    void set_sync_callback(SyncCallback callback) { on_sync_cue = callback; }
    void set_seek_callback(SeekCallback callback) { on_seek_cue = callback; }
    void set_pause_callback(PauseCallback callback) { on_pause_cue = callback; }
    void set_resume_callback(ResumeCallback callback) { on_resume_cue = callback; }
    void set_client_connected_callback(ClientCallback callback) { on_client_connected = callback; }
    void set_client_disconnected_callback(ClientCallback callback) { on_client_disconnected = callback; }
    
    // Send sync commands
    bool broadcast_sync_cue(uint32_t frame_number);
    bool broadcast_seek_cue(double position);
    bool broadcast_pause_cue();
    bool broadcast_resume_cue();
    
    // Targeted sending (uses config targets)
    bool send_targeted_sync_cue(uint32_t frame_number);
    bool send_targeted_seek_cue(double position);
    bool send_targeted_pause_cue();
    bool send_targeted_resume_cue();
    
    // Client management
    std::vector<ConnectedClient> get_connected_clients();
    size_t get_client_count();
    
    // Client info
    uint32_t get_my_client_id() const { return my_client_id; }
    const std::string& get_my_client_name() const { return my_client_name; }
    
    // Configuration
    const NetworkConfig& get_network_config() const { return network_config; }
    bool load_config(const std::string& config_file_path);
    bool save_config(const std::string& config_file_path = "") const;
    void apply_config();
    
private:
    void handle_sync_message(const SyncMessage& msg, const std::string& sender_ip);
    uint32_t generate_client_id();
    std::string generate_client_name();
    std::vector<std::string> get_target_ips() const;
};