#pragma once

#include <QString>

struct AppSettings {
    bool save_logs = true;
    QString log_level = "info";
    QString log_path = "";
    QString theme_mode = "system";
    QString language = "en";
    bool auto_connect_on_start = false;
    bool show_logs_panel = true;
    bool show_traffic_in_status = true;
    bool routing_enabled = false;
    QString routing_mode = "tunnel_ru"; // tunnel_ru | bypass_ru
    QString routing_cache_path = "";
    QString routing_source_url = "https://antifilter.download/list/subnet.lst";
};

AppSettings loadAppSettings();
void saveAppSettings(const AppSettings &cfg);
