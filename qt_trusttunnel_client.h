#pragma once
#include <QObject>
#include <QString>
#include <QTimer>
#include <memory>
#include <functional>
#include <optional>
#include "vpn/trusttunnel/client.h"
#include "vpn/trusttunnel/auto_network_monitor.h"
#include "vpn/trusttunnel/config.h"

class QtTrustTunnelClient : public QObject {
    Q_OBJECT
public:
    enum class State {
        Disconnected,
        Connecting,
        Connected,
        Reconnecting,
        Disconnecting,
        Error,
    };
    Q_ENUM(State)

    explicit QtTrustTunnelClient(QObject *parent = nullptr);
    ~QtTrustTunnelClient();

    void setConfig(ag::TrustTunnelConfig config);
    bool loadConfigFromFile(const QString &path);
    void setAutoReconnectEnabled(bool enabled);
    void setReconnectBoundsMs(int initialDelayMs, int maxDelayMs);

    Q_INVOKABLE void connectVpn();
    Q_INVOKABLE void disconnectVpn();
    Q_INVOKABLE bool isConnected() const;
    Q_INVOKABLE State state() const;

signals:
    void stateChanged(QtTrustTunnelClient::State state);
    void vpnConnected();
    void vpnDisconnected();
    void vpnError(const QString &msg);

private:
    ag::VpnCallbacks makeCallbacks();
    void doConnectAttempt();
    void scheduleReconnect(const QString &reason);
    void setState(State s);
    void handleCoreStateChanged(ag::VpnSessionState state);

    std::unique_ptr<ag::TrustTunnelClient> m_client;
    std::unique_ptr<ag::AutoNetworkMonitor> m_networkMonitor;
    std::optional<ag::TrustTunnelConfig> m_config;
    QTimer m_reconnectTimer;
    State m_state = State::Disconnected;
    bool m_autoReconnect = true;
    bool m_stopRequested = false;
    int m_reconnectDelayMs = 1000;
    int m_reconnectMaxMs = 30000;
};
