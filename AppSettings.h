#pragma once

#include <QString>

struct AppSettings {
    bool save_logs = true;
    QString log_level = "info";
    QString log_path = "/tmp/trusttunnel-qt-helper.log";
    QString theme_mode = "system";
    bool auto_connect_on_start = false;
};

AppSettings loadAppSettings();
void saveAppSettings(const AppSettings &cfg);
