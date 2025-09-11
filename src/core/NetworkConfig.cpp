#include "core/NetworkConfig.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <regex>

NetworkConfigParser::NetworkConfigParser(const std::string& file_path) 
    : config_file_path(file_path) {
    load_from_file(file_path);
}

bool NetworkConfigParser::load_from_file(const std::string& file_path) {
    config_file_path = file_path;
    
    std::ifstream file(file_path);
    if (!file.is_open()) {
        std::cerr << "Could not open config file: " << file_path << std::endl;
        return false;
    }
    
    std::string line;
    while (std::getline(file, line)) {
        line = trim(line);
        
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') {
            continue;
        }
        
        parse_line(line);
    }
    
    file.close();
    
    if (!validate_config()) {
        std::cerr << "Config validation failed for: " << file_path << std::endl;
        auto errors = get_validation_errors();
        for (const auto& error : errors) {
            std::cerr << "  " << error << std::endl;
        }
        return false;
    }
    
    std::cout << "Loaded network config from: " << file_path << std::endl;
    std::cout << "  Listen port: " << config.listen_port << std::endl;
    std::cout << "  Targets: " << config.targets.size() << std::endl;
    std::cout << "  Auto-discover: " << (config.auto_discover ? "enabled" : "disabled") << std::endl;
    
    return true;
}

bool NetworkConfigParser::save_to_file(const std::string& file_path) const {
    std::string output_path = file_path.empty() ? config_file_path : file_path;
    
    std::ofstream file(output_path);
    if (!file.is_open()) {
        std::cerr << "Could not create config file: " << output_path << std::endl;
        return false;
    }
    
    file << "# Network Configuration for Video Player Sync\n";
    file << "# Lines starting with # are comments\n\n";
    
    file << "# Local settings\n";
    file << "listen_port=" << config.listen_port << "\n";
    if (!config.client_name.empty()) {
        file << "client_name=" << config.client_name << "\n";
    }
    file << "auto_discover=" << (config.auto_discover ? "true" : "false") << "\n";
    file << "enable_broadcast=" << (config.enable_broadcast ? "true" : "false") << "\n\n";
    
    file << "# Timing settings\n";
    file << "heartbeat_interval=" << config.heartbeat_interval_seconds << "\n";
    file << "client_timeout=" << config.client_timeout_seconds << "\n\n";
    
    file << "# Sync behavior\n";
    file << "auto_sync=" << (config.auto_sync_on_startup ? "true" : "false") << "\n";
    file << "auto_master=" << (config.auto_master_on_startup ? "true" : "false") << "\n";
    file << "respond_to_cues=" << (config.respond_to_external_cues ? "true" : "false") << "\n\n";
    
    file << "# Target clients (ip:port:name:enabled)\n";
    for (const auto& target : config.targets) {
        file << "target=" << target.ip_address << ":" << target.port;
        if (!target.name.empty()) {
            file << ":" << target.name;
        }
        file << ":" << (target.enabled ? "true" : "false") << "\n";
    }
    
    file.close();
    std::cout << "Saved network config to: " << output_path << std::endl;
    return true;
}

void NetworkConfigParser::parse_line(const std::string& line) {
    size_t equals_pos = line.find('=');
    if (equals_pos == std::string::npos) {
        return;
    }
    
    std::string key = trim(line.substr(0, equals_pos));
    std::string value = trim(line.substr(equals_pos + 1));
    
    if (key == "listen_port") {
        config.listen_port = static_cast<uint16_t>(std::stoi(value));
    } else if (key == "client_name") {
        config.client_name = value;
    } else if (key == "auto_discover") {
        config.auto_discover = (value == "true" || value == "1");
    } else if (key == "enable_broadcast") {
        config.enable_broadcast = (value == "true" || value == "1");
    } else if (key == "heartbeat_interval") {
        config.heartbeat_interval_seconds = std::stoi(value);
    } else if (key == "client_timeout") {
        config.client_timeout_seconds = std::stoi(value);
    } else if (key == "auto_sync") {
        config.auto_sync_on_startup = (value == "true" || value == "1");
    } else if (key == "auto_master") {
        config.auto_master_on_startup = (value == "true" || value == "1");
    } else if (key == "respond_to_cues") {
        config.respond_to_external_cues = (value == "true" || value == "1");
    } else if (key == "target") {
        parse_target_line(value);
    }
}

void NetworkConfigParser::parse_target_line(const std::string& line) {
    auto parts = split(line, ':');
    
    if (parts.size() < 2) {
        std::cerr << "Invalid target format: " << line << " (expected ip:port:name:enabled)" << std::endl;
        return;
    }
    
    NetworkTarget target;
    target.ip_address = trim(parts[0]);
    target.port = static_cast<uint16_t>(std::stoi(trim(parts[1])));
    
    if (parts.size() > 2 && !parts[2].empty()) {
        target.name = trim(parts[2]);
    } else {
        target.name = target.ip_address;
    }
    
    if (parts.size() > 3) {
        std::string enabled_str = trim(parts[3]);
        target.enabled = (enabled_str == "true" || enabled_str == "1");
    } else {
        target.enabled = true;
    }
    
    config.targets.push_back(target);
}

void NetworkConfigParser::add_target(const std::string& ip, uint16_t port, 
                                     const std::string& name, bool enabled) {
    NetworkTarget target(ip, port, name.empty() ? ip : name, enabled);
    config.targets.push_back(target);
}

void NetworkConfigParser::remove_target(const std::string& ip) {
    config.targets.erase(
        std::remove_if(config.targets.begin(), config.targets.end(),
            [&ip](const NetworkTarget& target) {
                return target.ip_address == ip;
            }),
        config.targets.end()
    );
}

bool NetworkConfigParser::validate_config() const {
    return get_validation_errors().empty();
}

std::vector<std::string> NetworkConfigParser::get_validation_errors() const {
    std::vector<std::string> errors;
    
    // Validate port range
    if (config.listen_port < 1024 || config.listen_port > 65535) {
        errors.push_back("listen_port must be between 1024 and 65535");
    }
    
    // Validate timing settings
    if (config.heartbeat_interval_seconds < 1 || config.heartbeat_interval_seconds > 300) {
        errors.push_back("heartbeat_interval must be between 1 and 300 seconds");
    }
    
    if (config.client_timeout_seconds < config.heartbeat_interval_seconds * 2) {
        errors.push_back("client_timeout must be at least 2x heartbeat_interval");
    }
    
    // Validate target IPs
    std::regex ip_regex(R"(^(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})$)");
    
    for (const auto& target : config.targets) {
        if (!std::regex_match(target.ip_address, ip_regex)) {
            errors.push_back("Invalid IP address: " + target.ip_address);
            continue;
        }
        
        // Check IP octets are valid (0-255)
        auto octets = split(target.ip_address, '.');
        for (const auto& octet : octets) {
            int value = std::stoi(octet);
            if (value < 0 || value > 255) {
                errors.push_back("Invalid IP octet in: " + target.ip_address);
                break;
            }
        }
        
        if (target.port < 1024 || target.port > 65535) {
            errors.push_back("Invalid port for " + target.ip_address + ": " + std::to_string(target.port));
        }
    }
    
    return errors;
}

std::string NetworkConfigParser::trim(const std::string& str) const {
    size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }
    size_t end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}

std::vector<std::string> NetworkConfigParser::split(const std::string& str, char delimiter) const {
    std::vector<std::string> parts;
    std::stringstream ss(str);
    std::string part;
    
    while (std::getline(ss, part, delimiter)) {
        parts.push_back(part);
    }
    
    return parts;
}