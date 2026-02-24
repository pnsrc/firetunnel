#include "qt_trusttunnel_client.h"
#include <QMetaObject>
#include <algorithm>
#include <toml++/toml.h>

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
            scheduleReconnect(QString("set_system_dns() failed: %1").arg(QString::fromStdString(errText)));
            return;
        }

        if (auto err = m_client->connect(ag::TrustTunnelClient::AutoSetup{})) {
            scheduleReconnect(QStringLiteral("connect() failed"));
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
}

bool QtTrustTunnelClient::isConnected() const {
    return m_state == State::Connected;
}

QtTrustTunnelClient::State QtTrustTunnelClient::state() const {
    return m_state;
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
    callbacks.client_output_handler = [](ag::VpnClientOutputEvent *) {};
    callbacks.connection_info_handler = [](ag::VpnConnectionInfoEvent *) {};
    return callbacks;
}

void QtTrustTunnelClient::scheduleReconnect(const QString &reason) {
    if (m_stopRequested || !m_autoReconnect) {
        setState(State::Error);
        emit vpnError(reason);
        return;
    }

    setState(State::Reconnecting);
    emit vpnError(reason);
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
