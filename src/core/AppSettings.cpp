#include "AppSettings.h"

#include <QSettings>
#include <QStandardPaths>

static QString defaultLogPath() {
#ifdef _WIN32
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/trusttunnel-qt.log";
#else
    return "/tmp/trusttunnel-qt.log";
#endif
}

static QString defaultRoutingCachePath() {
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/ru-subnet.lst";
}

AppSettings loadAppSettings() {
    QSettings s("FireTunnel", "TrustTunnelQt");
    AppSettings out;
    out.save_logs = s.value("logs/save", true).toBool();
    out.log_level = s.value("logs/level", "info").toString();
    out.log_path = s.value("logs/path", defaultLogPath()).toString();
    out.theme_mode = s.value("ui/theme_mode", "system").toString();
    out.language = s.value("ui/language", "en").toString();
    out.auto_connect_on_start = s.value("vpn/auto_connect_on_start", false).toBool();
    out.routing_enabled = s.value("routing/enabled", false).toBool();
    out.routing_mode = s.value("routing/mode", "tunnel_ru").toString();
    out.routing_cache_path = s.value("routing/cache_path", defaultRoutingCachePath()).toString();
    out.routing_source_url = s.value("routing/source_url",
            "https://antifilter.download/list/subnet.lst").toString();
    if (out.log_path.isEmpty()) {
        out.log_path = defaultLogPath();
    }
    return out;
}

void saveAppSettings(const AppSettings &cfg) {
    QSettings s("FireTunnel", "TrustTunnelQt");
    s.setValue("logs/save", cfg.save_logs);
    s.setValue("logs/level", cfg.log_level);
    s.setValue("logs/path", cfg.log_path);
    s.setValue("ui/theme_mode", cfg.theme_mode);
    s.setValue("ui/language", cfg.language);
    s.setValue("vpn/auto_connect_on_start", cfg.auto_connect_on_start);
    s.setValue("routing/enabled", cfg.routing_enabled);
    s.setValue("routing/mode", cfg.routing_mode);
    s.setValue("routing/cache_path", cfg.routing_cache_path);
    s.setValue("routing/source_url", cfg.routing_source_url);
}
