#pragma once
#include <string>
#include <vector>
#include <map>

struct NetworkTarget {
    std::string ip_address;
    uint16_t port;
    std::string name;
    bool enabled;
    
    NetworkTarget(const std::string& ip = "", uint16_t p = 9999, 
                  const std::string& n = "", bool e = true)
        : ip_address(ip), port(p), name(n), enabled(e) {}
};

struct NetworkConfig {
    // Local settings
    uint16_t listen_port = 9999;
    std::string client_name = "";
    bool auto_discover = true;
    bool enable_broadcast = true;
    
    // Target clients for direct communication
    std::vector<NetworkTarget> targets;
    
    // Timing settings
    int heartbeat_interval_seconds = 10;
    int client_timeout_seconds = 30;
    
    // Sync behavior
    bool auto_sync_on_startup = false;
    bool auto_master_on_startup = false;
    bool respond_to_external_cues = true;
    
    NetworkConfig() = default;
};

class NetworkConfigParser {
private:
    NetworkConfig config;
    std::string config_file_path;
    
public:
    NetworkConfigParser() = default;
    explicit NetworkConfigParser(const std::string& file_path);
    
    bool load_from_file(const std::string& file_path);
    bool save_to_file(const std::string& file_path = "") const;
    
    const NetworkConfig& get_config() const { return config; }
    NetworkConfig& get_config() { return config; }
    
    // Convenience methods
    void add_target(const std::string& ip, uint16_t port = 9999, 
                   const std::string& name = "", bool enabled = true);
    void remove_target(const std::string& ip);
    void set_listen_port(uint16_t port) { config.listen_port = port; }
    void set_client_name(const std::string& name) { config.client_name = name; }
    
    // Validation
    bool validate_config() const;
    std::vector<std::string> get_validation_errors() const;
    
private:
    void parse_line(const std::string& line);
    void parse_target_line(const std::string& line);
    std::string trim(const std::string& str) const;
    std::vector<std::string> split(const std::string& str, char delimiter) const;
};