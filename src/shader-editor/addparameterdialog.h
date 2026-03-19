// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDialog>
#include <QJsonObject>
#include <QSet>

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

namespace PlasmaZones {

class AddParameterDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AddParameterDialog(const QSet<int>& usedScalarSlots,
                                const QSet<int>& usedColorSlots,
                                const QSet<int>& usedImageSlots = {},
                                QWidget* parent = nullptr);
    ~AddParameterDialog() override;

    QJsonObject parameterJson() const;

private:
    void setupUi();
    void onTypeChanged();
    void onNameChanged(const QString& text);
    void onColorButtonClicked();
    void updateAutoSlot();

    QLineEdit* m_nameEdit = nullptr;
    QComboBox* m_typeCombo = nullptr;
    QComboBox* m_groupCombo = nullptr;

    // Default value widgets (one per type, shown/hidden)
    QDoubleSpinBox* m_defaultFloat = nullptr;
    QLabel* m_defaultFloatLabel = nullptr;
    QSpinBox* m_defaultInt = nullptr;
    QLabel* m_defaultIntLabel = nullptr;
    QCheckBox* m_defaultBool = nullptr;
    QLabel* m_defaultBoolLabel = nullptr;
    QPushButton* m_defaultColorBtn = nullptr;
    QLabel* m_defaultColorLabel = nullptr;
    QWidget* m_defaultImageRow = nullptr;
    QLabel* m_defaultImageLabel = nullptr;
    QPushButton* m_defaultImageBtn = nullptr;
    QPushButton* m_imageClearBtn = nullptr;
    QLabel* m_imagePathLabel = nullptr;
    QColor m_selectedColor;
    QString m_selectedImagePath;

    // Min/Max (float/int only)
    QDoubleSpinBox* m_minFloat = nullptr;
    QDoubleSpinBox* m_maxFloat = nullptr;
    QSpinBox* m_minInt = nullptr;
    QSpinBox* m_maxInt = nullptr;
    QLabel* m_floatDash = nullptr;
    QLabel* m_intDash = nullptr;
    QLabel* m_rangeLabel = nullptr;
    QWidget* m_rangeRow = nullptr;

    // Slot (auto-assigned, read-only display)
    QLabel* m_slotLabel = nullptr;
    int m_autoSlot = 0;

    QDialogButtonBox* m_buttonBox = nullptr;

    QSet<int> m_usedScalarSlots;
    QSet<int> m_usedColorSlots;
    QSet<int> m_usedImageSlots;
};

} // namespace PlasmaZones
