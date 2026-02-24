#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

#include <toml++/toml.h>

#include "common/logger.h"
#include "vpn/trusttunnel/auto_network_monitor.h"
#include "vpn/trusttunnel/client.h"
#include "vpn/trusttunnel/config.h"

static std::atomic_bool g_stop{false};

static void on_signal(int) {
    g_stop.store(true);
}

static std::string get_arg(int argc, char *argv[], const char *name) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::string_view(argv[i]) == name) {
            return argv[i + 1];
        }
    }
    return {};
}

static ag::LogLevel parse_log_level(std::string_view level) {
    if (level == "error") return ag::LOG_LEVEL_ERROR;
    if (level == "warn") return ag::LOG_LEVEL_WARN;
    if (level == "debug") return ag::LOG_LEVEL_DEBUG;
    if (level == "trace") return ag::LOG_LEVEL_TRACE;
    return ag::LOG_LEVEL_INFO;
}

int main(int argc, char *argv[]) {
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    const std::string config_path = get_arg(argc, argv, "--config");
    const std::string log_level_arg = get_arg(argc, argv, "--loglevel");
    if (config_path.empty()) {
        std::cerr << "Missing --config <path>\n";
        return 1;
    }

    toml::parse_result parsed = toml::parse_file(config_path);
    if (!parsed) {
        std::cerr << "Failed parsing config: " << parsed.error().description() << "\n";
        return 1;
    }

    auto config = ag::TrustTunnelConfig::build_config(parsed.table());
    if (!config.has_value()) {
        std::cerr << "Invalid TrustTunnel config\n";
        return 1;
    }

    if (!log_level_arg.empty()) {
        config->loglevel = parse_log_level(log_level_arg);
        ag::Logger::set_log_level(config->loglevel);
    }

    ag::VpnCallbacks callbacks = {
            .protect_handler = [](ag::SocketProtectEvent *event) {
                if (event) {
                    event->result = 0;
                }
            },
            .verify_handler = [](ag::VpnVerifyCertificateEvent *event) {
                if (event) {
                    event->result = 0;
                }
            },
            .state_changed_handler = [](ag::VpnStateChangedEvent *event) {
                if (event) {
                    std::cout << "[helper] state=" << static_cast<int>(event->state) << std::endl;
                }
            },
            .client_output_handler = [](ag::VpnClientOutputEvent *) {},
            .connection_info_handler = [](ag::VpnConnectionInfoEvent *) {},
    };

    auto client = std::make_unique<ag::TrustTunnelClient>(std::move(*config), std::move(callbacks));
    auto network_monitor = std::make_unique<ag::AutoNetworkMonitor>(client.get());
    if (!network_monitor->start()) {
        std::cerr << "Failed to start network monitor\n";
        return 1;
    }

    if (auto err = client->set_system_dns()) {
        std::cerr << "set_system_dns() failed: " << err->str() << "\n";
        network_monitor->stop();
        return 1;
    }

    if (auto err = client->connect(ag::TrustTunnelClient::AutoSetup{})) {
        std::cerr << "connect() failed: " << err->str() << "\n";
        network_monitor->stop();
        return 1;
    }

    std::cout << "[helper] connected" << std::endl;

    while (!g_stop.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }

    client->disconnect();
    network_monitor->stop();
    std::cout << "[helper] stopped" << std::endl;
    return 0;
}
