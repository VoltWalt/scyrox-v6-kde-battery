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
    setMinimumWidth(340);

    auto *layout = new QVBoxLayout(this);

    auto *infoLabel = new QLabel(tr("Scyrox V6 Battery Indicator\n"
                                    "Lightweight system tray battery monitor"), this);
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
    m_lowThresholdSpin->setSuffix(QStringLiteral("%"));
    notifLayout->addRow(tr("Low battery warning:"), m_lowThresholdSpin);

    m_criticalThresholdSpin = new QSpinBox(this);
    m_criticalThresholdSpin->setRange(1, 20);
    m_criticalThresholdSpin->setValue(m_settings->value("criticalThreshold", 10).toInt());
    m_criticalThresholdSpin->setSuffix(QStringLiteral("%"));
    notifLayout->addRow(tr("Critical battery alert:"), m_criticalThresholdSpin);

    m_hintLabel = new QLabel(this);
    m_hintLabel->setStyleSheet(QStringLiteral("color: #c62828;"));
    m_hintLabel->setWordWrap(true);
    m_hintLabel->setVisible(false);
    notifLayout->addRow(QString(), m_hintLabel);

    layout->addWidget(notifGroup);

    connect(m_lowThresholdSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &SettingsDialog::validateThresholds);
    connect(m_criticalThresholdSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &SettingsDialog::validateThresholds);

    auto *btnLayout = new QHBoxLayout;
    btnLayout->addStretch();
    m_okBtn = new QPushButton(tr("OK"), this);
    auto *cancelBtn = new QPushButton(tr("Cancel"), this);
    btnLayout->addWidget(m_okBtn);
    btnLayout->addWidget(cancelBtn);
    layout->addLayout(btnLayout);

    connect(m_okBtn, &QPushButton::clicked, this, [this]() {
        // Belt and braces: validateThresholds() already disables OK, but the
        // dialog can also be accepted with the keyboard, so re-check here
        // rather than persisting a pair where critical >= low.
        if (m_criticalThresholdSpin->value() >= m_lowThresholdSpin->value())
            return;
        m_settings->setValue("showPercentage", m_showPercentageCheck->isChecked());
        m_settings->setValue("showTooltip", m_showTooltipCheck->isChecked());
        m_settings->setValue("notificationsEnabled", m_notificationsCheck->isChecked());
        m_settings->setValue("lowThreshold", m_lowThresholdSpin->value());
        m_settings->setValue("criticalThreshold", m_criticalThresholdSpin->value());
        accept();
    });

    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    validateThresholds();
}

void SettingsDialog::validateThresholds()
{
    const int low = m_lowThresholdSpin->value();

    // Cap the critical spin box below the warning level: the invalid
    // combination then becomes unreachable instead of merely tinted red.
    m_criticalThresholdSpin->setMaximum(qMax(1, low - 1));

    const int critical = m_criticalThresholdSpin->value();
    const bool valid = critical < low;

    m_okBtn->setEnabled(valid);
    m_hintLabel->setVisible(!valid);
    if (!valid)
        m_hintLabel->setText(tr("The critical alert must be lower than the low warning."));

    const QString invalidStyle = QStringLiteral("QSpinBox { background-color: #ffcccc; }");
    m_criticalThresholdSpin->setStyleSheet(valid ? QString() : invalidStyle);
    m_lowThresholdSpin->setStyleSheet(valid ? QString() : invalidStyle);
}
