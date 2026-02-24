#include "AppSettings.h"

#include <QSettings>

AppSettings loadAppSettings() {
    QSettings s("FireTunnel", "TrustTunnelQt");
    AppSettings out;
    out.save_logs = s.value("logs/save", true).toBool();
    out.log_level = s.value("logs/level", "info").toString();
    out.log_path = s.value("logs/path", "/tmp/trusttunnel-qt-helper.log").toString();
    out.theme_mode = s.value("ui/theme_mode", "system").toString();
    out.auto_connect_on_start = s.value("vpn/auto_connect_on_start", false).toBool();
    if (out.log_path.isEmpty()) {
        out.log_path = "/tmp/trusttunnel-qt-helper.log";
    }
    return out;
}

void saveAppSettings(const AppSettings &cfg) {
    QSettings s("FireTunnel", "TrustTunnelQt");
    s.setValue("logs/save", cfg.save_logs);
    s.setValue("logs/level", cfg.log_level);
    s.setValue("logs/path", cfg.log_path);
    s.setValue("ui/theme_mode", cfg.theme_mode);
    s.setValue("vpn/auto_connect_on_start", cfg.auto_connect_on_start);
}
