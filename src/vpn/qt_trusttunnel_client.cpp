#include "qt_trusttunnel_client.h"
#include <QMetaObject>
#include <algorithm>
#include <toml++/toml.h>
#include "vpn/vpn.h" // for ag::iovec on Windows

#ifdef _WIN32
#include <windows.h>

static bool is_process_elevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return false;
    }
    TOKEN_ELEVATION elevation{};
    DWORD size = 0;
    const BOOL ok = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size);
    CloseHandle(token);
    return ok && elevation.TokenIsElevated;
}
#endif

static ag::LogLevel parse_log_level(const QString &level) {
    const QString l = level.toLower();
    if (l == "error") return ag::LOG_LEVEL_ERROR;
    if (l == "warn" || l == "warning") return ag::LOG_LEVEL_WARN;
    if (l == "debug") return ag::LOG_LEVEL_DEBUG;
    if (l == "trace") return ag::LOG_LEVEL_TRACE;
    return ag::LOG_LEVEL_INFO;
}

QtTrustTunnelClient::QtTrustTunnelClient(QObject *parent)
    : QObject(parent) {
    m_reconnectTimer.setSingleShot(true);
    connect(&m_reconnectTimer, &QTimer::timeout, this, &QtTrustTunnelClient::doConnectAttempt);
}

QtTrustTunnelClient::~QtTrustTunnelClient() {
    disconnectVpn();
}

void QtTrustTunnelClient::setConfig(ag::TrustTunnelConfig config) {
    m_config = std::move(config);
    m_config->loglevel = m_logLevel;
    ag::Logger::set_log_level(m_logLevel);
    if (std::holds_alternative<ag::TrustTunnelConfig::TunListener>(m_config->listener)) {
        auto &tun = std::get<ag::TrustTunnelConfig::TunListener>(m_config->listener);
        tun.included_routes.insert(tun.included_routes.end(), m_extraIncludedRoutes.begin(), m_extraIncludedRoutes.end());
        tun.excluded_routes.insert(tun.excluded_routes.end(), m_extraExcludedRoutes.begin(), m_extraExcludedRoutes.end());
    }
}

bool QtTrustTunnelClient::loadConfigFromFile(const QString &path) {
    const std::string configPath = path.toStdString();
    toml::parse_result parsed = toml::parse_file(configPath);
    if (!parsed) {
        const std::string_view descrView = parsed.error().description();
        const std::string descr{descrView};
        setState(State::Error);
        emit vpnError(QString("Failed parsing config: %1").arg(QString::fromStdString(descr)));
        return false;
    }

    auto config = ag::TrustTunnelConfig::build_config(parsed.table());
    if (!config.has_value()) {
        setState(State::Error);
        emit vpnError(QStringLiteral("Invalid TrustTunnel config structure"));
        return false;
    }

    setConfig(std::move(*config));
    setState(State::Disconnected);
    return true;
}

void QtTrustTunnelClient::setAutoReconnectEnabled(bool enabled) {
    m_autoReconnect = enabled;
}

void QtTrustTunnelClient::setReconnectBoundsMs(int initialDelayMs, int maxDelayMs) {
    m_reconnectDelayMs = std::max(250, initialDelayMs);
    m_reconnectMaxMs = std::max(m_reconnectDelayMs, maxDelayMs);
}

void QtTrustTunnelClient::connectVpn() {
    if (m_state == State::Connecting || m_state == State::Connected || m_state == State::Reconnecting) {
        return;
    }
#ifndef _WIN32
    if (::geteuid() != 0) {
        emit vpnError(QStringLiteral("Root permissions are required to initialize VPN (run app with sudo)."));
        return;
    }
#else
    if (!is_process_elevated()) {
        emit vpnError(QStringLiteral("Administrator privileges are required to initialize VPN. Restart the app as Administrator."));
        return;
    }
#endif
    m_stopRequested = false;
    m_reconnectTimer.stop();
    doConnectAttempt();
}

void QtTrustTunnelClient::doConnectAttempt() {
    if (m_stopRequested) {
        return;
    }

    setState(m_client ? State::Reconnecting : State::Connecting);

    try {
        if (!m_client) {
            if (!m_config.has_value()) {
                setState(State::Error);
                emit vpnError(QStringLiteral("TrustTunnel config is not set"));
                return;
            }
            m_client = std::make_unique<ag::TrustTunnelClient>(std::move(*m_config), makeCallbacks());
            m_networkMonitor = std::make_unique<ag::AutoNetworkMonitor>(m_client.get());
            if (!m_networkMonitor->start()) {
                m_networkMonitor.reset();
                setState(State::Error);
                emit vpnError(QStringLiteral("Failed to start network monitor"));
                return;
            }
            m_config.reset();
        }

        if (auto dnsErr = m_client->set_system_dns()) {
            const std::string errText = dnsErr->str();
            const QString qErr = QString::fromStdString(errText);
            // DNS setup failure is usually fatal until privileges change; stop auto-retry.
            m_stopRequested = true;
            setState(State::Error);
            emit vpnError(QString("set_system_dns() failed: %1").arg(qErr));
            return;
        }

        if (auto err = m_client->connect(ag::TrustTunnelClient::AutoSetup{})) {
            const std::string errText = err->str();
            QString qErr = QString::fromStdString(errText);
            if (qErr.contains("Failed to create listener", Qt::CaseInsensitive)) {
                qErr += QStringLiteral(" (likely needs sudo/admin privileges)");
                // Fatal until privileges/config change: stop reconnect loop.
                m_stopRequested = true;
                setState(State::Error);
                emit vpnError(QString("connect() failed: %1").arg(qErr));
                return;
            }
            scheduleReconnect(QString("connect() failed: %1").arg(qErr));
            return;
        }

        m_reconnectDelayMs = 1000;
        setState(State::Connected);
        emit vpnConnected();
    } catch (const std::exception &e) {
        scheduleReconnect(QString::fromUtf8(e.what()));
    }
}

void QtTrustTunnelClient::disconnectVpn() {
    m_stopRequested = true;
    m_reconnectTimer.stop();
    setState(State::Disconnecting);

    if (m_client) {
        m_client->disconnect();
        if (m_networkMonitor) {
            m_networkMonitor->stop();
            m_networkMonitor.reset();
        }
        m_client.reset();
    }

    setState(State::Disconnected);
    emit vpnDisconnected();
    m_stopRequested = false;
}

bool QtTrustTunnelClient::isConnected() const {
    return m_state == State::Connected;
}

QtTrustTunnelClient::State QtTrustTunnelClient::state() const {
    return m_state;
}

void QtTrustTunnelClient::setLogLevel(const QString &level) {
    m_logLevel = parse_log_level(level);
    ag::Logger::set_log_level(m_logLevel);
    if (m_config.has_value()) {
        m_config->loglevel = m_logLevel;
    }
}

void QtTrustTunnelClient::setRoutingRules(const std::vector<std::string> &includeRoutes,
        const std::vector<std::string> &excludeRoutes) {
    m_extraIncludedRoutes = includeRoutes;
    m_extraExcludedRoutes = excludeRoutes;
    if (m_config.has_value() && std::holds_alternative<ag::TrustTunnelConfig::TunListener>(m_config->listener)) {
        auto &tun = std::get<ag::TrustTunnelConfig::TunListener>(m_config->listener);
        tun.included_routes.insert(tun.included_routes.end(), m_extraIncludedRoutes.begin(), m_extraIncludedRoutes.end());
        tun.excluded_routes.insert(tun.excluded_routes.end(), m_extraExcludedRoutes.begin(), m_extraExcludedRoutes.end());
    }
}

ag::VpnCallbacks QtTrustTunnelClient::makeCallbacks() {
    ag::VpnCallbacks callbacks;
    callbacks.protect_handler = [](ag::SocketProtectEvent *event) {
        if (event) {
            event->result = 0;
        }
    };
    callbacks.verify_handler = [](ag::VpnVerifyCertificateEvent *event) {
        if (event) {
            event->result = 0;
        }
    };
    callbacks.state_changed_handler = [this](ag::VpnStateChangedEvent *event) {
        ag::VpnSessionState state = event ? event->state : ag::VPN_SS_DISCONNECTED;
        QMetaObject::invokeMethod(this, [this, state]() { handleCoreStateChanged(state); }, Qt::QueuedConnection);
    };
    callbacks.client_output_handler = [this](ag::VpnClientOutputEvent *event) {
        size_t bytes = 0;
        if (event) {
            for (size_t i = 0; i < event->packet.chunks_num; ++i) {
                bytes += event->packet.chunks[i].iov_len;
            }
        }
        QMetaObject::invokeMethod(this, [this, bytes]() { emit clientOutput(QString::number(bytes)); },
                Qt::QueuedConnection);
    };
    callbacks.connection_info_handler = [this](ag::VpnConnectionInfoEvent *event) {
        QString line = QStringLiteral("connection info");
        if (event) {
            QString action;
            switch (event->action) {
            case ag::VPN_FCA_BYPASS: action = QStringLiteral("bypass"); break;
            case ag::VPN_FCA_TUNNEL: action = QStringLiteral("tunnel"); break;
            case ag::VPN_FCA_REJECT: action = QStringLiteral("reject"); break;
            default: action = QStringLiteral("unknown"); break;
            }
            const QString domain = event->domain ? QString::fromUtf8(event->domain) : QStringLiteral("-");
            line = QStringLiteral("%1 %2").arg(action, domain);
        }
        QMetaObject::invokeMethod(this, [this, line]() { emit connectionInfo(line); }, Qt::QueuedConnection);
    };
    return callbacks;
}

void QtTrustTunnelClient::scheduleReconnect(const QString &reason) {
    const QString message = reason.isEmpty() ? QStringLiteral("connect() failed") : reason;
    if (m_stopRequested || !m_autoReconnect) {
        setState(State::Error);
        emit vpnError(message);
        return;
    }

    setState(State::Reconnecting);
    emit vpnError(message);
    m_reconnectTimer.start(m_reconnectDelayMs);
    m_reconnectDelayMs = std::min(m_reconnectDelayMs * 2, m_reconnectMaxMs);
}

void QtTrustTunnelClient::setState(State s) {
    if (m_state == s) {
        return;
    }
    m_state = s;
    emit stateChanged(m_state);
}

void QtTrustTunnelClient::handleCoreStateChanged(ag::VpnSessionState state) {
    if (m_stopRequested) {
        return;
    }

    switch (state) {
    case ag::VPN_SS_CONNECTED:
        m_reconnectDelayMs = 1000;
        setState(State::Connected);
        emit vpnConnected();
        break;
    case ag::VPN_SS_CONNECTING:
    case ag::VPN_SS_RECOVERING:
        setState(State::Reconnecting);
        break;
    case ag::VPN_SS_WAITING_RECOVERY:
    case ag::VPN_SS_WAITING_FOR_NETWORK:
        scheduleReconnect(QStringLiteral("core requested recovery"));
        break;
    case ag::VPN_SS_DISCONNECTED:
        if (!m_stopRequested) {
            scheduleReconnect(QStringLiteral("core disconnected"));
        }
        break;
    default:
        break;
    }
}
#ifndef _WIN32
#include <unistd.h>
#endif
