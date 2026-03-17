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
class QStackedWidget;

namespace PlasmaZones {

class AddParameterDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AddParameterDialog(const QSet<int>& usedScalarSlots,
                                const QSet<int>& usedColorSlots,
                                QWidget* parent = nullptr);
    ~AddParameterDialog() override;

    QJsonObject parameterJson() const;

private:
    void setupUi();
    void onTypeChanged();
    void onNameChanged(const QString& name);
    void onColorButtonClicked();
    void updateAutoSlot();

    QLineEdit* m_idEdit = nullptr;
    QLineEdit* m_nameEdit = nullptr;
    QComboBox* m_typeCombo = nullptr;
    QComboBox* m_groupCombo = nullptr;

    // Default value widgets
    QStackedWidget* m_defaultStack = nullptr;
    QDoubleSpinBox* m_defaultFloat = nullptr;
    QSpinBox* m_defaultInt = nullptr;
    QCheckBox* m_defaultBool = nullptr;
    QPushButton* m_defaultColorBtn = nullptr;
    QColor m_selectedColor;

    // Min/Max
    QDoubleSpinBox* m_minSpin = nullptr;
    QDoubleSpinBox* m_maxSpin = nullptr;
    QLabel* m_minLabel = nullptr;
    QLabel* m_maxLabel = nullptr;

    // Slot
    QSpinBox* m_slotSpin = nullptr;

    QDialogButtonBox* m_buttonBox = nullptr;

    QSet<int> m_usedScalarSlots;
    QSet<int> m_usedColorSlots;
};

} // namespace PlasmaZones
