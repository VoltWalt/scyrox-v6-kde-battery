#include "settingsdialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QCheckBox>
#include <QSpinBox>
#include <QPushButton>
#include <QGroupBox>
#include <QFormLayout>

SettingsDialog::SettingsDialog(QSettings *settings, QWidget *parent)
    : QDialog(parent)
    , m_settings(settings)
{
    setWindowTitle(tr("Scyrox V6 Settings"));
    setMinimumWidth(320);

    auto *layout = new QVBoxLayout(this);

    auto *infoLabel = new QLabel(tr("Scyrox V6 Battery Indicator\n"
                                 "Lightweight system tray battery monitor"));
    infoLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(infoLabel);

    auto *displayGroup = new QGroupBox(tr("Display"), this);
    auto *displayLayout = new QFormLayout(displayGroup);

    m_showPercentageCheck = new QCheckBox(tr("Show percentage in tooltip"), this);
    m_showPercentageCheck->setChecked(m_settings->value("showPercentage", true).toBool());
    displayLayout->addRow(m_showPercentageCheck);

    m_showTooltipCheck = new QCheckBox(tr("Show detailed tooltip"), this);
    m_showTooltipCheck->setChecked(m_settings->value("showTooltip", true).toBool());
    displayLayout->addRow(m_showTooltipCheck);

    layout->addWidget(displayGroup);

    auto *notifGroup = new QGroupBox(tr("Notifications"), this);
    auto *notifLayout = new QFormLayout(notifGroup);

    m_notificationsCheck = new QCheckBox(tr("Enable low battery notifications"), this);
    m_notificationsCheck->setChecked(m_settings->value("notificationsEnabled", true).toBool());
    notifLayout->addRow(m_notificationsCheck);

    m_lowThresholdSpin = new QSpinBox(this);
    m_lowThresholdSpin->setRange(5, 50);
    m_lowThresholdSpin->setValue(m_settings->value("lowThreshold", 20).toInt());
    m_lowThresholdSpin->setSuffix("%");
    notifLayout->addRow(tr("Low battery warning:"), m_lowThresholdSpin);

    m_criticalThresholdSpin = new QSpinBox(this);
    m_criticalThresholdSpin->setRange(1, 20);
    m_criticalThresholdSpin->setValue(m_settings->value("criticalThreshold", 10).toInt());
    m_criticalThresholdSpin->setSuffix("%");
    notifLayout->addRow(tr("Critical battery alert:"), m_criticalThresholdSpin);

    layout->addWidget(notifGroup);

    auto *btnLayout = new QHBoxLayout;
    auto *okBtn = new QPushButton(tr("OK"), this);
    auto *cancelBtn = new QPushButton(tr("Cancel"), this);
    btnLayout->addStretch();
    btnLayout->addWidget(okBtn);
    btnLayout->addWidget(cancelBtn);
    layout->addLayout(btnLayout);

    connect(okBtn, &QPushButton::clicked, this, [this]() {
        m_settings->setValue("showPercentage", m_showPercentageCheck->isChecked());
        m_settings->setValue("showTooltip", m_showTooltipCheck->isChecked());
        m_settings->setValue("notificationsEnabled", m_notificationsCheck->isChecked());
        m_settings->setValue("lowThreshold", m_lowThresholdSpin->value());
        m_settings->setValue("criticalThreshold", m_criticalThresholdSpin->value());
        accept();
    });

    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
}
