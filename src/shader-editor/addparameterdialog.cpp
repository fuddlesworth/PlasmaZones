// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "addparameterdialog.h"
#include "shaderpackageio.h"

#include <QCheckBox>
#include <QColor>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLoggingCategory>
#include <QMessageBox>
#include <QPalette>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <KLocalizedString>

Q_LOGGING_CATEGORY(lcAddParam, "plasmazones.shadereditor.addparam")

namespace PlasmaZones {

AddParameterDialog::AddParameterDialog(const QSet<int>& usedScalarSlots,
                                       const QSet<int>& usedColorSlots,
                                       QWidget* parent)
    : QDialog(parent)
    , m_selectedColor(Qt::white)
    , m_usedScalarSlots(usedScalarSlots)
    , m_usedColorSlots(usedColorSlots)
{
    setWindowTitle(i18n("Add Parameter"));
    setupUi();
    onTypeChanged(); // set initial visibility
    updateAutoSlot();
}

AddParameterDialog::~AddParameterDialog() = default;

void AddParameterDialog::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    auto* formLayout = new QFormLayout;

    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setPlaceholderText(i18n("e.g. Animation Speed"));
    formLayout->addRow(i18n("Name:"), m_nameEdit);

    m_idEdit = new QLineEdit(this);
    m_idEdit->setPlaceholderText(i18n("auto-generated from name"));
    formLayout->addRow(i18n("ID:"), m_idEdit);

    m_typeCombo = new QComboBox(this);
    m_typeCombo->addItems({
        QStringLiteral("float"),
        QStringLiteral("int"),
        QStringLiteral("bool"),
        QStringLiteral("color"),
    });
    formLayout->addRow(i18n("Type:"), m_typeCombo);

    m_groupCombo = new QComboBox(this);
    m_groupCombo->setEditable(true);
    // Store English values as data for serialization, i18n'd text for display
    m_groupCombo->addItem(i18n("Animation"), QStringLiteral("Animation"));
    m_groupCombo->addItem(i18n("Colors"), QStringLiteral("Colors"));
    m_groupCombo->addItem(i18n("Pattern"), QStringLiteral("Pattern"));
    m_groupCombo->addItem(i18n("Appearance"), QStringLiteral("Appearance"));
    m_groupCombo->addItem(i18n("Zone"), QStringLiteral("Zone"));
    m_groupCombo->addItem(i18n("Audio"), QStringLiteral("Audio"));
    m_groupCombo->addItem(i18n("Labels"), QStringLiteral("Labels"));
    formLayout->addRow(i18n("Group:"), m_groupCombo);

    // Default value — stacked widget for different types
    m_defaultStack = new QStackedWidget(this);

    // Index 0: float default
    m_defaultFloat = new QDoubleSpinBox(this);
    m_defaultFloat->setRange(-9999.0, 9999.0);
    m_defaultFloat->setDecimals(4);
    m_defaultFloat->setValue(0.0);
    m_defaultStack->addWidget(m_defaultFloat);

    // Index 1: int default
    m_defaultInt = new QSpinBox(this);
    m_defaultInt->setRange(-9999, 9999);
    m_defaultInt->setValue(0);
    m_defaultStack->addWidget(m_defaultInt);

    // Index 2: bool default
    m_defaultBool = new QCheckBox(i18n("Enabled"), this);
    m_defaultStack->addWidget(m_defaultBool);

    // Index 3: color default
    m_defaultColorBtn = new QPushButton(this);
    m_defaultColorBtn->setText(m_selectedColor.name());
    m_defaultColorBtn->setAutoFillBackground(true);
    {
        QPalette colorPal = m_defaultColorBtn->palette();
        colorPal.setColor(QPalette::Button, m_selectedColor);
        colorPal.setColor(QPalette::ButtonText, m_selectedColor.lightnessF() > 0.5 ? Qt::black : Qt::white);
        m_defaultColorBtn->setPalette(colorPal);
    }
    m_defaultStack->addWidget(m_defaultColorBtn);

    formLayout->addRow(i18n("Default:"), m_defaultStack);

    // Min/Max
    m_minLabel = new QLabel(i18n("Min:"), this);
    m_minSpin = new QDoubleSpinBox(this);
    m_minSpin->setRange(-9999.0, 9999.0);
    m_minSpin->setDecimals(4);
    m_minSpin->setValue(0.0);
    formLayout->addRow(m_minLabel, m_minSpin);

    m_maxLabel = new QLabel(i18n("Max:"), this);
    m_maxSpin = new QDoubleSpinBox(this);
    m_maxSpin->setRange(-9999.0, 9999.0);
    m_maxSpin->setDecimals(4);
    m_maxSpin->setValue(1.0);
    formLayout->addRow(m_maxLabel, m_maxSpin);

    // Slot
    m_slotSpin = new QSpinBox(this);
    m_slotSpin->setRange(0, 31);
    formLayout->addRow(i18n("Slot:"), m_slotSpin);

    mainLayout->addLayout(formLayout);

    // Button box
    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    mainLayout->addWidget(m_buttonBox);

    connect(m_buttonBox, &QDialogButtonBox::accepted, this, [this]() {
        if (m_nameEdit->text().trimmed().isEmpty()) {
            m_nameEdit->setFocus();
            return;
        }
        const QString type = m_typeCombo->currentText();
        const int slot = m_slotSpin->value();
        const QSet<int>& usedSlots = (type == QLatin1String("color")) ? m_usedColorSlots : m_usedScalarSlots;
        if (usedSlots.contains(slot)) {
            QMessageBox::warning(this, i18n("Slot Conflict"),
                i18n("Slot %1 is already in use by another %2 parameter. Choose a different slot.", slot, type));
            m_slotSpin->setFocus();
            return;
        }
        accept();
    });
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_nameEdit, &QLineEdit::textChanged, this, &AddParameterDialog::onNameChanged);
    connect(m_typeCombo, &QComboBox::currentTextChanged, this, [this]() {
        onTypeChanged();
        updateAutoSlot();
    });
    connect(m_defaultColorBtn, &QPushButton::clicked, this, &AddParameterDialog::onColorButtonClicked);

    resize(400, 350);
}

void AddParameterDialog::onTypeChanged()
{
    const QString type = m_typeCombo->currentText();
    const bool isBool = (type == QLatin1String("bool"));
    const bool isColor = (type == QLatin1String("color"));
    const bool isInt = (type == QLatin1String("int"));
    const bool hideMinMax = isBool || isColor;

    if (isColor) {
        m_defaultStack->setCurrentIndex(3);
        m_slotSpin->setRange(0, 15);
    } else if (isBool) {
        m_defaultStack->setCurrentIndex(2);
        m_slotSpin->setRange(0, 31);
    } else if (isInt) {
        m_defaultStack->setCurrentIndex(1);
        m_slotSpin->setRange(0, 31);
    } else {
        m_defaultStack->setCurrentIndex(0);
        m_slotSpin->setRange(0, 31);
    }

    m_minLabel->setVisible(!hideMinMax);
    m_minSpin->setVisible(!hideMinMax);
    m_maxLabel->setVisible(!hideMinMax);
    m_maxSpin->setVisible(!hideMinMax);
}

void AddParameterDialog::onNameChanged(const QString& name)
{
    // Auto-generate ID from name using shared sanitizer, then convert to camelCase
    const QString id = ShaderPackageIO::sanitizeId(name.trimmed());

    // Convert to camelCase: split on hyphens, capitalize first letter of each part after the first
    const QStringList parts = id.split(QLatin1Char('-'), Qt::SkipEmptyParts);
    if (!parts.isEmpty()) {
        QString camelId = parts.first();
        for (int i = 1; i < parts.size(); ++i) {
            QString part = parts[i];
            if (!part.isEmpty()) {
                part[0] = part[0].toUpper();
            }
            camelId += part;
        }
        m_idEdit->setText(camelId);
    } else {
        m_idEdit->clear();
    }
}

void AddParameterDialog::onColorButtonClicked()
{
    const QColor color = QColorDialog::getColor(m_selectedColor, this, i18n("Select Default Color"));
    if (color.isValid()) {
        m_selectedColor = color;
        m_defaultColorBtn->setText(color.name());
        {
            QPalette colorPal = m_defaultColorBtn->palette();
            colorPal.setColor(QPalette::Button, color);
            colorPal.setColor(QPalette::ButtonText, color.lightnessF() > 0.5 ? Qt::black : Qt::white);
            m_defaultColorBtn->setPalette(colorPal);
        }
    }
}

void AddParameterDialog::updateAutoSlot()
{
    const QString type = m_typeCombo->currentText();

    if (type == QLatin1String("color")) {
        for (int s = 0; s < 16; ++s) {
            if (!m_usedColorSlots.contains(s)) {
                m_slotSpin->setValue(s);
                return;
            }
        }
        m_slotSpin->setValue(0);
    } else {
        for (int s = 0; s < 32; ++s) {
            if (!m_usedScalarSlots.contains(s)) {
                m_slotSpin->setValue(s);
                return;
            }
        }
        m_slotSpin->setValue(0);
    }
}

QJsonObject AddParameterDialog::parameterJson() const
{
    QJsonObject param;
    param[QStringLiteral("id")] = m_idEdit->text().trimmed();
    param[QStringLiteral("name")] = m_nameEdit->text().trimmed();
    param[QStringLiteral("type")] = m_typeCombo->currentText();
    // Use data role (English) if available, otherwise use edited text
    const QVariant groupData = m_groupCombo->currentData();
    param[QStringLiteral("group")] = groupData.isValid() ? groupData.toString() : m_groupCombo->currentText();
    param[QStringLiteral("slot")] = m_slotSpin->value();

    const QString type = m_typeCombo->currentText();
    if (type == QLatin1String("bool")) {
        param[QStringLiteral("default")] = m_defaultBool->isChecked();
    } else if (type == QLatin1String("color")) {
        param[QStringLiteral("default")] = m_selectedColor.name();
    } else if (type == QLatin1String("int")) {
        param[QStringLiteral("default")] = m_defaultInt->value();
        param[QStringLiteral("min")] = static_cast<int>(m_minSpin->value());
        param[QStringLiteral("max")] = static_cast<int>(m_maxSpin->value());
    } else {
        // float
        param[QStringLiteral("default")] = m_defaultFloat->value();
        param[QStringLiteral("min")] = m_minSpin->value();
        param[QStringLiteral("max")] = m_maxSpin->value();
    }

    return param;
}

} // namespace PlasmaZones
