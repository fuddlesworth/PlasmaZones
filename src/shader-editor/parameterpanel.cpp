// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "parameterpanel.h"

#include <QCheckBox>
#include <QColor>
#include <QColorDialog>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLoggingCategory>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

#include <KLocalizedString>

Q_LOGGING_CATEGORY(lcParameterPanel, "plasmazones.shadereditor.parameters")

namespace PlasmaZones {

ParameterPanel::ParameterPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);

    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    auto* contentWidget = new QWidget(scrollArea);
    m_groupsLayout = new QVBoxLayout(contentWidget);
    m_groupsLayout->setContentsMargins(4, 4, 4, 4);
    m_groupsLayout->setSpacing(4);
    m_groupsLayout->addStretch();

    scrollArea->setWidget(contentWidget);
    outerLayout->addWidget(scrollArea);

    // Bottom button bar
    auto* buttonBar = new QHBoxLayout;
    buttonBar->setContentsMargins(4, 2, 4, 4);

    auto* copyDefaultsBtn = new QPushButton(i18n("Copy as Defaults"), this);
    auto* resetAllBtn = new QPushButton(i18n("Reset All"), this);
    auto* applyBtn = new QPushButton(i18n("Apply"), this);

    buttonBar->addWidget(copyDefaultsBtn);
    buttonBar->addWidget(resetAllBtn);
    buttonBar->addStretch();
    buttonBar->addWidget(applyBtn);

    outerLayout->addLayout(buttonBar);

    connect(copyDefaultsBtn, &QPushButton::clicked, this, &ParameterPanel::copyDefaultsRequested);
    connect(resetAllBtn, &QPushButton::clicked, this, &ParameterPanel::resetRequested);
    connect(applyBtn, &QPushButton::clicked, this, &ParameterPanel::applyRequested);
}

ParameterPanel::~ParameterPanel() = default;

void ParameterPanel::clearControls()
{
    m_controls.clear();

    // Remove all group widgets from layout (but not the stretch at the end)
    for (auto* w : m_groupWidgets) {
        m_groupsLayout->removeWidget(w);
        delete w;
    }
    m_groupWidgets.clear();
}

void ParameterPanel::loadFromMetadata(const QString& metadataJson)
{
    clearControls();

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(metadataJson.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        qCWarning(lcParameterPanel) << "Failed to parse metadata JSON:" << parseError.errorString();
        return;
    }

    const QJsonArray params = doc.object().value(QStringLiteral("parameters")).toArray();
    if (params.isEmpty()) {
        return;
    }

    // Group parameters by their "group" field, preserving order of first occurrence
    QStringList groupOrder;
    QMap<QString, QList<QJsonObject>> groupedParams;

    for (const QJsonValue& paramVal : params) {
        const QJsonObject param = paramVal.toObject();
        QString group = param.value(QStringLiteral("group")).toString().trimmed();
        if (group.isEmpty()) {
            group = i18n("General");
        }

        if (!groupedParams.contains(group)) {
            groupOrder.append(group);
        }
        groupedParams[group].append(param);
    }

    // Create collapsible sections for each group
    bool isFirstGroup = true;
    const int insertIndex = m_groupsLayout->count() - 1; // before the stretch
    int currentInsert = insertIndex;

    for (const QString& groupName : groupOrder) {
        const QList<QJsonObject>& groupParams = groupedParams[groupName];

        // Container for this group (header + content)
        auto* groupContainer = new QWidget;
        auto* groupLayout = new QVBoxLayout(groupContainer);
        groupLayout->setContentsMargins(0, 0, 0, 0);
        groupLayout->setSpacing(0);

        // Header button
        auto* headerBtn = new QPushButton(groupContainer);
        headerBtn->setCheckable(true);
        headerBtn->setChecked(isFirstGroup);
        headerBtn->setStyleSheet(
            QStringLiteral("QPushButton { background: #333; color: #ccc; border: none; border-radius: 4px; "
                           "padding: 6px 8px; text-align: left; font-weight: bold; }"
                           "QPushButton:hover { background: #3a3a3a; }"));

        const int paramCount = groupParams.size();
        const QString expandedText = QStringLiteral("\u25BE %1").arg(groupName);
        const QString collapsedText = QStringLiteral("\u25B8 %1 (%2)").arg(groupName).arg(
            i18np("%1 parameter", "%1 parameters", paramCount));

        headerBtn->setText(isFirstGroup ? expandedText : collapsedText);

        groupLayout->addWidget(headerBtn);

        // Content widget
        auto* content = new QWidget(groupContainer);
        auto* contentLayout = new QVBoxLayout(content);
        contentLayout->setContentsMargins(8, 4, 4, 8);
        contentLayout->setSpacing(4);

        for (const QJsonObject& param : groupParams) {
            const QString type = param.value(QStringLiteral("type")).toString();
            const QString name = param.value(QStringLiteral("name")).toString();
            const int slot = param.value(QStringLiteral("slot")).toInt(-1);

            if (slot < 0) {
                continue;
            }

            const QString uniformName = computeUniformName(type, slot);

            QWidget* control = nullptr;
            if (type == QLatin1String("float")) {
                const double defaultVal = param.value(QStringLiteral("default")).toDouble();
                const double min = param.value(QStringLiteral("min")).toDouble(0.0);
                const double max = param.value(QStringLiteral("max")).toDouble(1.0);
                control = createFloatControl(name, uniformName, defaultVal, min, max, slot);
            } else if (type == QLatin1String("int")) {
                const int defaultVal = param.value(QStringLiteral("default")).toInt();
                const int min = param.value(QStringLiteral("min")).toInt(0);
                const int max = param.value(QStringLiteral("max")).toInt(100);
                control = createIntControl(name, uniformName, defaultVal, min, max, slot);
            } else if (type == QLatin1String("bool")) {
                const bool defaultVal = param.value(QStringLiteral("default")).toBool();
                control = createBoolControl(name, uniformName, defaultVal, slot);
            } else if (type == QLatin1String("color")) {
                const QString defaultColor = param.value(QStringLiteral("default")).toString();
                control = createColorControl(name, uniformName, defaultColor, slot);
            }

            if (control) {
                contentLayout->addWidget(control);
            }
        }

        content->setVisible(isFirstGroup);
        groupLayout->addWidget(content);

        // Connect header toggle
        connect(headerBtn, &QPushButton::toggled, this, [headerBtn, content, expandedText, collapsedText](bool checked) {
            content->setVisible(checked);
            headerBtn->setText(checked ? expandedText : collapsedText);
        });

        m_groupsLayout->insertWidget(currentInsert, groupContainer);
        m_groupWidgets.append(groupContainer);
        ++currentInsert;

        isFirstGroup = false;
    }
}

QWidget* ParameterPanel::createFloatControl(const QString& name, const QString& uniformName,
                                            double defaultVal, double min, double max, int slot)
{
    auto* row = new QWidget;
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 2, 0, 2);

    auto* label = new QLabel(name, row);
    label->setMinimumWidth(100);

    auto* slider = new QSlider(Qt::Horizontal, row);
    slider->setRange(0, 10000);
    const double clampedDefault = qBound(min, defaultVal, max);
    const int initialPos = (max > min) ? qRound((clampedDefault - min) / (max - min) * 10000.0) : 0;
    slider->setValue(initialPos);

    auto* valueLabel = new QLabel(QString::number(clampedDefault, 'f', 2), row);
    valueLabel->setFixedWidth(50);
    valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    auto* insertBtn = new QPushButton(i18n("Insert \u2192"), row);
    insertBtn->setFixedWidth(70);

    layout->addWidget(label);
    layout->addWidget(slider, 1);
    layout->addWidget(valueLabel);
    layout->addWidget(insertBtn);

    // Store control info
    ParamControl ctrl;
    ctrl.type = QStringLiteral("float");
    ctrl.slot = slot;
    ctrl.uniformName = uniformName;
    ctrl.slider = slider;
    ctrl.valueLabel = valueLabel;
    ctrl.floatMin = min;
    ctrl.floatMax = max;
    m_controls.append(ctrl);

    const int controlIndex = m_controls.size() - 1;

    connect(slider, &QSlider::valueChanged, this, [this, controlIndex](int pos) {
        const ParamControl& c = m_controls.at(controlIndex);
        const double val = c.floatMin + (pos / 10000.0) * (c.floatMax - c.floatMin);
        c.valueLabel->setText(QString::number(val, 'f', 2));
        Q_EMIT parameterChanged();
    });

    connect(insertBtn, &QPushButton::clicked, this, [this, uniformName]() {
        Q_EMIT insertUniformRequested(uniformName);
    });

    return row;
}

QWidget* ParameterPanel::createIntControl(const QString& name, const QString& uniformName,
                                          int defaultVal, int min, int max, int slot)
{
    auto* row = new QWidget;
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 2, 0, 2);

    auto* label = new QLabel(name, row);
    label->setMinimumWidth(100);

    auto* spinBox = new QSpinBox(row);
    spinBox->setRange(min, max);
    spinBox->setValue(defaultVal);

    auto* insertBtn = new QPushButton(i18n("Insert \u2192"), row);
    insertBtn->setFixedWidth(70);

    layout->addWidget(label);
    layout->addWidget(spinBox, 1);
    layout->addWidget(insertBtn);

    ParamControl ctrl;
    ctrl.type = QStringLiteral("int");
    ctrl.slot = slot;
    ctrl.uniformName = uniformName;
    ctrl.spinBox = spinBox;
    m_controls.append(ctrl);

    connect(spinBox, &QSpinBox::valueChanged, this, [this]() {
        Q_EMIT parameterChanged();
    });

    connect(insertBtn, &QPushButton::clicked, this, [this, uniformName]() {
        Q_EMIT insertUniformRequested(uniformName);
    });

    return row;
}

QWidget* ParameterPanel::createBoolControl(const QString& name, const QString& uniformName,
                                           bool defaultVal, int slot)
{
    auto* row = new QWidget;
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 2, 0, 2);

    auto* label = new QLabel(name, row);
    label->setMinimumWidth(100);

    auto* checkBox = new QCheckBox(row);
    checkBox->setChecked(defaultVal);

    auto* insertBtn = new QPushButton(i18n("Insert \u2192"), row);
    insertBtn->setFixedWidth(70);

    layout->addWidget(label);
    layout->addWidget(checkBox, 1);
    layout->addWidget(insertBtn);

    ParamControl ctrl;
    ctrl.type = QStringLiteral("bool");
    ctrl.slot = slot;
    ctrl.uniformName = uniformName;
    ctrl.checkBox = checkBox;
    m_controls.append(ctrl);

    connect(checkBox, &QCheckBox::toggled, this, [this]() {
        Q_EMIT parameterChanged();
    });

    connect(insertBtn, &QPushButton::clicked, this, [this, uniformName]() {
        Q_EMIT insertUniformRequested(uniformName);
    });

    return row;
}

QWidget* ParameterPanel::createColorControl(const QString& name, const QString& uniformName,
                                            const QString& defaultColor, int slot)
{
    auto* row = new QWidget;
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 2, 0, 2);

    auto* label = new QLabel(name, row);
    label->setMinimumWidth(100);

    QColor color(defaultColor);
    if (!color.isValid()) {
        color = Qt::gray;
    }

    auto* colorBtn = new QPushButton(row);
    colorBtn->setFixedSize(28, 22);
    colorBtn->setStyleSheet(
        QStringLiteral("background-color: %1; border: 1px solid #666; border-radius: 4px;").arg(color.name()));

    auto* hexLabel = new QLabel(color.name().toUpper(), row);
    hexLabel->setFixedWidth(70);

    auto* insertBtn = new QPushButton(i18n("Insert \u2192"), row);
    insertBtn->setFixedWidth(70);

    layout->addWidget(label);
    layout->addWidget(colorBtn);
    layout->addWidget(hexLabel, 1);
    layout->addWidget(insertBtn);

    ParamControl ctrl;
    ctrl.type = QStringLiteral("color");
    ctrl.slot = slot;
    ctrl.uniformName = uniformName;
    ctrl.colorBtn = colorBtn;
    ctrl.valueLabel = hexLabel;
    ctrl.currentColor = color;
    m_controls.append(ctrl);

    const int controlIndex = m_controls.size() - 1;

    connect(colorBtn, &QPushButton::clicked, this, [this, controlIndex]() {
        if (controlIndex >= m_controls.size()) return;
        ParamControl& c = m_controls[controlIndex];
        const QColor chosen = QColorDialog::getColor(c.currentColor, this, i18n("Choose Color"));
        if (!chosen.isValid() || controlIndex >= m_controls.size()) return;
        ParamControl& c2 = m_controls[controlIndex];
        c2.currentColor = chosen;
        c2.colorBtn->setStyleSheet(
            QStringLiteral("background-color: %1; border: 1px solid #666; border-radius: 4px;").arg(chosen.name()));
        c2.valueLabel->setText(chosen.name().toUpper());
        Q_EMIT parameterChanged();
    });

    connect(insertBtn, &QPushButton::clicked, this, [this, uniformName]() {
        Q_EMIT insertUniformRequested(uniformName);
    });

    return row;
}

QVariantMap ParameterPanel::currentUniformValues() const
{
    QVariantMap uniforms;

    for (const ParamControl& ctrl : m_controls) {
        if (ctrl.uniformName.isEmpty()) {
            continue;
        }

        if (ctrl.type == QLatin1String("float") && ctrl.slider) {
            const double val = ctrl.floatMin + (ctrl.slider->value() / 10000.0) * (ctrl.floatMax - ctrl.floatMin);
            uniforms[ctrl.uniformName] = val;
        } else if (ctrl.type == QLatin1String("int") && ctrl.spinBox) {
            uniforms[ctrl.uniformName] = static_cast<double>(ctrl.spinBox->value());
        } else if (ctrl.type == QLatin1String("bool") && ctrl.checkBox) {
            uniforms[ctrl.uniformName] = ctrl.checkBox->isChecked() ? 1.0 : 0.0;
        } else if (ctrl.type == QLatin1String("color")) {
            uniforms[ctrl.uniformName] = ctrl.currentColor.name(QColor::HexArgb);
        }
    }

    return uniforms;
}

QString ParameterPanel::computeUniformName(const QString& type, int slot)
{
    if (type == QLatin1String("color")) {
        if (slot < 0 || slot > 15) {
            return {};
        }
        return QStringLiteral("customColor%1").arg(slot + 1);
    }

    // float, int, bool
    if (slot < 0 || slot > 31) {
        return {};
    }

    const int vecIndex = slot / 4;
    const int component = slot % 4;
    static const char* components[] = {"x", "y", "z", "w"};
    return QStringLiteral("customParams%1_%2").arg(vecIndex + 1).arg(QLatin1String(components[component]));
}

} // namespace PlasmaZones
