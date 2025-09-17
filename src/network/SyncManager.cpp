#include "network/SyncManager.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <chrono>
#include <unistd.h>
#include <sys/utsname.h>

SyncManager::SyncManager(const std::string& client_name, uint16_t port)
    : my_client_id(generate_client_id()), port(port), enabled(false) {
    
    if (client_name.empty()) {
        my_client_name = generate_client_name();
    } else {
        my_client_name = client_name;
    }
    
    // Set default config values
    network_config.listen_port = port;
    network_config.client_name = my_client_name;
    
    server = std::make_unique<UDPSyncServer>(port);
    sender = std::make_unique<UDPSyncSender>(my_client_id, my_client_name, port);
    
    std::cout << "SyncManager created - Client: " << my_client_name 
              << " (ID: " << my_client_id << ") Port: " << port << std::endl;
}

std::unique_ptr<SyncManager> SyncManager::create_with_config(const std::string& config_file_path) {
    // First load config to get the correct port
    auto temp_parser = std::make_unique<NetworkConfigParser>(config_file_path);
    const auto& temp_config = temp_parser->get_config();
    
    // Create with correct initial parameters
    auto manager = std::make_unique<SyncManager>(temp_config.client_name, temp_config.listen_port);
    
    // Set the config and parser
    manager->config_parser = std::move(temp_parser);
    manager->network_config = manager->config_parser->get_config();
    
    std::cout << "SyncManager created with config - Client: " << manager->my_client_name
              << " Port: " << manager->port << " Targets: " << manager->network_config.targets.size() << std::endl;
    
    return manager;
}

std::unique_ptr<SyncManager> SyncManager::create_default(const std::string& client_name, uint16_t port) {
    return std::make_unique<SyncManager>(client_name, port);
}

SyncManager::~SyncManager() {
    shutdown();
}

bool SyncManager::initialize_with_config(const std::string& config_file_path) {
    if (!load_config(config_file_path)) {
        return false;
    }
    
    apply_config();
    return initialize();
}

bool SyncManager::initialize() {
    if (enabled) {
        return true;
    }
    
    // Set up message callback for server
    server->set_message_callback(
        [this](const SyncMessage& msg, const std::string& sender_ip) {
            handle_sync_message(msg, sender_ip);
        }
    );
    
    // Start server
    if (!server->start()) {
        std::cerr << "Failed to start sync server" << std::endl;
        return false;
    }
    
    // Initialize sender
    if (!sender->initialize()) {
        std::cerr << "Failed to initialize sync sender" << std::endl;
        server->stop();
        return false;
    }
    
    // Start heartbeat
    sender->start_heartbeat();
    
    enabled = true;
    std::cout << "SyncManager initialized successfully" << std::endl;
    return true;
}

void SyncManager::shutdown() {
    if (!enabled) {
        return;
    }
    
    enabled = false;
    
    if (sender) {
        sender->shutdown();
    }
    
    if (server) {
        server->stop();
    }
    
    std::cout << "SyncManager shutdown" << std::endl;
}

void SyncManager::set_enabled(bool enable) {
    if (enable && !enabled) {
        initialize();
    } else if (!enable && enabled) {
        shutdown();
    }
}

bool SyncManager::broadcast_sync_cue(uint32_t frame_number) {
    if (!enabled || !sender) {
        return false;
    }
    
    return sender->send_sync_cue(frame_number);
}

bool SyncManager::broadcast_seek_cue(double position) {
    if (!enabled || !sender) {
        return false;
    }
    
    return sender->send_seek_cue(position);
}

bool SyncManager::broadcast_pause_cue() {
    if (!enabled || !sender) {
        return false;
    }
    
    return sender->send_pause_cue();
}

bool SyncManager::broadcast_resume_cue() {
    if (!enabled || !sender) {
        return false;
    }
    
    return sender->send_resume_cue();
}

std::vector<ConnectedClient> SyncManager::get_connected_clients() {
    if (!enabled || !server) {
        return {};
    }
    
    return server->get_connected_clients();
}

size_t SyncManager::get_client_count() {
    if (!enabled || !server) {
        return 0;
    }
    
    return server->get_client_count();
}

void SyncManager::handle_sync_message(const SyncMessage& msg, const std::string& sender_ip) {
    // Ignore messages from ourselves
    if (msg.sender_id == my_client_id) {
        return;
    }
    
    switch (msg.type) {
        case SyncMessageType::SYNC_CUE:
            if (on_sync_cue) {
                on_sync_cue(msg.frame_number);
            }
            break;
            
        case SyncMessageType::SEEK_CUE:
            if (on_seek_cue) {
                on_seek_cue(msg.seek_position);
            }
            break;
            
        case SyncMessageType::PAUSE_CUE:
            if (on_pause_cue) {
                on_pause_cue();
            }
            break;
            
        case SyncMessageType::RESUME_CUE:
            if (on_resume_cue) {
                on_resume_cue();
            }
            break;
            
        case SyncMessageType::CLIENT_ANNOUNCE:
            if (on_client_connected) {
                on_client_connected(msg.sender_id, std::string(msg.client_name), sender_ip);
            }
            break;
            
        case SyncMessageType::HEARTBEAT:
            // Heartbeats are handled by the server for client tracking
            break;
    }
}

uint32_t SyncManager::generate_client_id() {
    // Generate a pseudo-random client ID based on current time and process ID
    auto now = std::chrono::high_resolution_clock::now();
    auto timestamp = now.time_since_epoch().count();
    pid_t pid = getpid();
    
    std::hash<std::string> hasher;
    std::string seed = std::to_string(timestamp) + std::to_string(pid);
    
    return static_cast<uint32_t>(hasher(seed));
}

std::string SyncManager::generate_client_name() {
    struct utsname system_info;
    std::string hostname = "unknown";
    
    if (uname(&system_info) == 0) {
        hostname = system_info.nodename;
    }
    
    // Extract just the hostname part (before any dots)
    size_t dot_pos = hostname.find('.');
    if (dot_pos != std::string::npos) {
        hostname = hostname.substr(0, dot_pos);
    }
    
    // Add process ID to make it unique
    pid_t pid = getpid();
    
    std::stringstream ss;
    ss << hostname << "-" << pid;
    
    return ss.str();
}


bool SyncManager::load_config(const std::string& config_file_path) {
    config_parser = std::make_unique<NetworkConfigParser>();
    
    if (!config_parser->load_from_file(config_file_path)) {
        return false;
    }
    
    network_config = config_parser->get_config();
    return true;
}

bool SyncManager::save_config(const std::string& config_file_path) const {
    if (!config_parser) {
        return false;
    }
    
    return config_parser->save_to_file(config_file_path);
}

void SyncManager::apply_config() {
    // Apply configuration settings to existing components
    if (network_config.auto_sync_on_startup) {
        initialize();
    }
}

std::vector<std::string> SyncManager::get_target_ips() const {
    std::vector<std::string> ips;
    
    for (const auto& target : network_config.targets) {
        if (target.enabled) {
            ips.push_back(target.ip_address);
        }
    }
    
    return ips;
}

bool SyncManager::send_targeted_sync_cue(uint32_t frame_number) {
    if (!enabled || !sender) {
        return false;
    }
    
    SyncMessage msg = SyncProtocol::create_sync_cue(my_client_id, frame_number, my_client_name);
    
    if (network_config.enable_broadcast) {
        sender->broadcast_message(msg);
    }
    
    // Send to specific targets
    auto target_ips = get_target_ips();
    if (!target_ips.empty()) {
        std::cout << "Sending SYNC_CUE to " << target_ips.size() << " targets - Frame: " << frame_number << std::endl;
        return sender->send_to_addresses(msg, target_ips);
    }
    
    return network_config.enable_broadcast;
}

bool SyncManager::send_targeted_seek_cue(double position) {
    if (!enabled || !sender) {
        return false;
    }
    
    SyncMessage msg = SyncProtocol::create_seek_cue(my_client_id, position, my_client_name);
    
    if (network_config.enable_broadcast) {
        sender->broadcast_message(msg);
    }
    
    auto target_ips = get_target_ips();
    if (!target_ips.empty()) {
        std::cout << "Sending SEEK_CUE to " << target_ips.size() << " targets - Position: " << position << "s" << std::endl;
        return sender->send_to_addresses(msg, target_ips);
    }
    
    return network_config.enable_broadcast;
}

bool SyncManager::send_targeted_pause_cue() {
    if (!enabled || !sender) {
        return false;
    }
    
    SyncMessage msg = SyncProtocol::create_pause_cue(my_client_id, my_client_name);
    
    if (network_config.enable_broadcast) {
        sender->broadcast_message(msg);
    }
    
    auto target_ips = get_target_ips();
    if (!target_ips.empty()) {
        std::cout << "Sending PAUSE_CUE to " << target_ips.size() << " targets" << std::endl;
        return sender->send_to_addresses(msg, target_ips);
    }
    
    return network_config.enable_broadcast;
}

bool SyncManager::send_targeted_resume_cue() {
    if (!enabled || !sender) {
        return false;
    }
    
    SyncMessage msg = SyncProtocol::create_resume_cue(my_client_id, my_client_name);
    
    if (network_config.enable_broadcast) {
        sender->broadcast_message(msg);
    }
    
    auto target_ips = get_target_ips();
    if (!target_ips.empty()) {
        std::cout << "Sending RESUME_CUE to " << target_ips.size() << " targets" << std::endl;
        return sender->send_to_addresses(msg, target_ips);
    }
    
    return network_config.enable_broadcast;
}