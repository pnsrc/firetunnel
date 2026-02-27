#pragma once

#include <QDialog>
#include <QWidget>
#include <QRadioButton>

#include "AppSettings.h"

class QCheckBox;
class QComboBox;
class QLineEdit;

class SettingsDialog : public QDialog {
public:
    explicit SettingsDialog(const QString &lang, const AppSettings &settings, QWidget *parent = nullptr);

    bool saveLogs() const;
    QString logLevel() const;
    QString logPath() const;
    QString themeMode() const;
    bool autoConnectOnStart() const;
    bool showLogsPanel() const;
    bool showTrafficInStatus() const;
    bool routingEnabled() const;
    QString routingMode() const;
    QString routingSourceUrl() const;
    QString routingCachePath() const;

private:
    QCheckBox *m_saveLogsCheck = nullptr;
    QComboBox *m_logLevelCombo = nullptr;
    QCheckBox *m_showLogsPanelCheck = nullptr;
    QCheckBox *m_showTrafficCheck = nullptr;
    QComboBox *m_themeModeCombo = nullptr;
    QLineEdit *m_logPathEdit = nullptr;
    QCheckBox *m_autoConnectCheck = nullptr;
    QCheckBox *m_routingEnableCheck = nullptr;
    QRadioButton *m_routingTunnelRadio = nullptr;
    QRadioButton *m_routingBypassRadio = nullptr;
    QLineEdit *m_routingUrlEdit = nullptr;
    QLineEdit *m_routingCacheEdit = nullptr;
};
