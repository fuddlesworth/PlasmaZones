// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QColor>
#include <QMap>
#include <QVariantMap>
#include <QVBoxLayout>
#include <QWidget>

class QCheckBox;
class QLabel;
class QPushButton;
class QSlider;
class QSpinBox;

namespace PlasmaZones {

class ParameterPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ParameterPanel(QWidget* parent = nullptr);
    ~ParameterPanel() override;

    void loadFromMetadata(const QString& metadataJson);
    QVariantMap currentUniformValues() const;

Q_SIGNALS:
    void parameterChanged();
    void insertUniformRequested(const QString& uniformText);
    void applyRequested();
    void resetRequested();
    void copyDefaultsRequested();

private:
    struct ParamControl {
        QString type;       // "float", "int", "bool", "color"
        int slot = -1;
        QString uniformName;
        // One of these is set depending on type
        QSlider* slider = nullptr;
        QSpinBox* spinBox = nullptr;
        QCheckBox* checkBox = nullptr;
        QPushButton* colorBtn = nullptr;
        QLabel* valueLabel = nullptr;
        // For float: min/max range
        double floatMin = 0.0;
        double floatMax = 1.0;
        // Current color (for color type)
        QColor currentColor;
    };

    void clearControls();
    QWidget* createFloatControl(const QString& name, const QString& uniformName,
                                double defaultVal, double min, double max, int slot);
    QWidget* createIntControl(const QString& name, const QString& uniformName,
                              int defaultVal, int min, int max, int slot);
    QWidget* createBoolControl(const QString& name, const QString& uniformName,
                               bool defaultVal, int slot);
    QWidget* createColorControl(const QString& name, const QString& uniformName,
                                const QString& defaultColor, int slot);

    QVBoxLayout* m_groupsLayout = nullptr;
    QList<ParamControl> m_controls;
    QList<QWidget*> m_groupWidgets; // for cleanup
};

} // namespace PlasmaZones
