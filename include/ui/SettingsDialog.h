#pragma once

#include <QDialog>

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

private:
    QCheckBox *m_saveLogsCheck = nullptr;
    QComboBox *m_logLevelCombo = nullptr;
    QComboBox *m_themeModeCombo = nullptr;
    QLineEdit *m_logPathEdit = nullptr;
    QCheckBox *m_autoConnectCheck = nullptr;
};
