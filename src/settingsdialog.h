#pragma once

#include <QDialog>
#include <QSettings>

class QCheckBox;
class QSpinBox;
class QLabel;
class QPushButton;

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(QSettings *settings, QWidget *parent = nullptr);

private slots:
    void validateThresholds();

private:
    QSettings *m_settings;
    QCheckBox *m_showPercentageCheck;
    QCheckBox *m_showTooltipCheck;
    QSpinBox *m_lowThresholdSpin;
    QSpinBox *m_criticalThresholdSpin;
    QCheckBox *m_notificationsCheck;
    QLabel *m_hintLabel;
    QPushButton *m_okBtn;
};
