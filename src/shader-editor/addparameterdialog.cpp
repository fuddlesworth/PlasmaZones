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
#include <QHBoxLayout>
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
    setMinimumWidth(420);
    setupUi();
    onTypeChanged();
    updateAutoSlot();
}

AddParameterDialog::~AddParameterDialog() = default;

void AddParameterDialog::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    auto* formLayout = new QFormLayout;
    formLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    // Name
    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setPlaceholderText(i18n("e.g. Animation Speed"));
    formLayout->addRow(i18n("Name:"), m_nameEdit);

    // Type
    m_typeCombo = new QComboBox(this);
    m_typeCombo->addItems({
        QStringLiteral("float"),
        QStringLiteral("int"),
        QStringLiteral("bool"),
        QStringLiteral("color"),
    });
    formLayout->addRow(i18n("Type:"), m_typeCombo);

    // Group
    m_groupCombo = new QComboBox(this);
    m_groupCombo->setEditable(true);
    m_groupCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
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

    // Index 0: float
    m_defaultFloat = new QDoubleSpinBox(this);
    m_defaultFloat->setRange(-9999.0, 9999.0);
    m_defaultFloat->setDecimals(2);
    m_defaultFloat->setValue(0.0);
    m_defaultStack->addWidget(m_defaultFloat);

    // Index 1: int
    m_defaultInt = new QSpinBox(this);
    m_defaultInt->setRange(-9999, 9999);
    m_defaultInt->setValue(0);
    m_defaultStack->addWidget(m_defaultInt);

    // Index 2: bool
    m_defaultBool = new QCheckBox(i18n("Enabled"), this);
    m_defaultStack->addWidget(m_defaultBool);

    // Index 3: color
    m_defaultColorBtn = new QPushButton(this);
    m_defaultColorBtn->setText(m_selectedColor.name());
    m_defaultColorBtn->setAutoFillBackground(true);
    {
        QPalette colorPal = m_defaultColorBtn->palette();
        colorPal.setColor(QPalette::Button, m_selectedColor);
        colorPal.setColor(QPalette::ButtonText, Qt::black);
        m_defaultColorBtn->setPalette(colorPal);
    }
    m_defaultStack->addWidget(m_defaultColorBtn);

    formLayout->addRow(i18n("Default:"), m_defaultStack);

    // Range row — float uses QDoubleSpinBox, int uses QSpinBox
    m_rangeRow = new QWidget(this);
    auto* rangeLayout = new QHBoxLayout(m_rangeRow);
    rangeLayout->setContentsMargins(0, 0, 0, 0);

    m_minFloat = new QDoubleSpinBox(this);
    m_minFloat->setRange(-9999.0, 9999.0);
    m_minFloat->setDecimals(2);
    m_minFloat->setValue(0.0);

    m_maxFloat = new QDoubleSpinBox(this);
    m_maxFloat->setRange(-9999.0, 9999.0);
    m_maxFloat->setDecimals(2);
    m_maxFloat->setValue(1.0);

    m_minInt = new QSpinBox(this);
    m_minInt->setRange(-9999, 9999);
    m_minInt->setValue(0);

    m_maxInt = new QSpinBox(this);
    m_maxInt->setRange(-9999, 9999);
    m_maxInt->setValue(100);

    m_floatDash = new QLabel(QStringLiteral("–"), this);
    m_intDash = new QLabel(QStringLiteral("–"), this);

    rangeLayout->addWidget(m_minFloat, 1);
    rangeLayout->addWidget(m_floatDash);
    rangeLayout->addWidget(m_maxFloat, 1);
    rangeLayout->addWidget(m_minInt, 1);
    rangeLayout->addWidget(m_intDash);
    rangeLayout->addWidget(m_maxInt, 1);

    m_rangeLabel = new QLabel(i18n("Range:"), this);
    formLayout->addRow(m_rangeLabel, m_rangeRow);

    // Slot (read-only, auto-assigned)
    m_slotLabel = new QLabel(this);
    formLayout->addRow(i18n("Slot:"), m_slotLabel);

    mainLayout->addLayout(formLayout);

    // Button box
    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    mainLayout->addWidget(m_buttonBox);

    connect(m_buttonBox, &QDialogButtonBox::accepted, this, [this]() {
        if (m_nameEdit->text().trimmed().isEmpty()) {
            m_nameEdit->setFocus();
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
}

void AddParameterDialog::onTypeChanged()
{
    const QString type = m_typeCombo->currentText();
    const bool isFloat = (type == QLatin1String("float"));
    const bool isInt = (type == QLatin1String("int"));
    const bool showRange = isFloat || isInt;

    if (type == QLatin1String("color")) {
        m_defaultStack->setCurrentIndex(3);
    } else if (type == QLatin1String("bool")) {
        m_defaultStack->setCurrentIndex(2);
    } else if (isInt) {
        m_defaultStack->setCurrentIndex(1);
    } else {
        m_defaultStack->setCurrentIndex(0);
    }

    m_rangeLabel->setVisible(showRange);
    m_rangeRow->setVisible(showRange);
    m_minFloat->setVisible(isFloat);
    m_floatDash->setVisible(isFloat);
    m_maxFloat->setVisible(isFloat);
    m_minInt->setVisible(isInt);
    m_intDash->setVisible(isInt);
    m_maxInt->setVisible(isInt);
}

void AddParameterDialog::onNameChanged(const QString& name)
{
    Q_UNUSED(name);
    // Name is used directly; ID is auto-generated at parameterJson() time
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
    const bool isColor = (type == QLatin1String("color"));
    const QSet<int>& usedSlots = isColor ? m_usedColorSlots : m_usedScalarSlots;
    const int maxSlot = isColor ? 15 : 31;

    m_autoSlot = 0;
    for (int s = 0; s <= maxSlot; ++s) {
        if (!usedSlots.contains(s)) {
            m_autoSlot = s;
            break;
        }
    }

    const QString uniformName = ShaderPackageIO::computeUniformName(type, m_autoSlot);
    m_slotLabel->setText(i18n("%1 (%2)", m_autoSlot, uniformName));
}

QJsonObject AddParameterDialog::parameterJson() const
{
    const QString name = m_nameEdit->text().trimmed();
    const QString type = m_typeCombo->currentText();

    // Auto-generate ID from name
    const QString id = ShaderPackageIO::sanitizeId(name);
    const QStringList parts = id.split(QLatin1Char('-'), Qt::SkipEmptyParts);
    QString camelId;
    if (!parts.isEmpty()) {
        camelId = parts.first();
        for (int i = 1; i < parts.size(); ++i) {
            QString part = parts[i];
            if (!part.isEmpty()) {
                part[0] = part[0].toUpper();
            }
            camelId += part;
        }
    }

    QJsonObject param;
    param[QStringLiteral("id")] = camelId;
    param[QStringLiteral("name")] = name;
    param[QStringLiteral("type")] = type;

    const QVariant groupData = m_groupCombo->currentData();
    param[QStringLiteral("group")] = groupData.isValid() ? groupData.toString() : m_groupCombo->currentText();
    param[QStringLiteral("slot")] = m_autoSlot;

    if (type == QLatin1String("bool")) {
        param[QStringLiteral("default")] = m_defaultBool->isChecked();
    } else if (type == QLatin1String("color")) {
        param[QStringLiteral("default")] = m_selectedColor.name();
    } else if (type == QLatin1String("int")) {
        param[QStringLiteral("default")] = m_defaultInt->value();
        param[QStringLiteral("min")] = m_minInt->value();
        param[QStringLiteral("max")] = m_maxInt->value();
    } else {
        param[QStringLiteral("default")] = m_defaultFloat->value();
        param[QStringLiteral("min")] = m_minFloat->value();
        param[QStringLiteral("max")] = m_maxFloat->value();
    }

    return param;
}

} // namespace PlasmaZones
