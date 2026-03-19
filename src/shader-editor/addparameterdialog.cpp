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
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLoggingCategory>
#include <QMessageBox>
#include <QPalette>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <KLocalizedString>

Q_LOGGING_CATEGORY(lcAddParam, "plasmazones.shadereditor.addparam")

namespace PlasmaZones {

AddParameterDialog::AddParameterDialog(const QSet<int>& usedScalarSlots,
                                       const QSet<int>& usedColorSlots,
                                       const QSet<int>& usedImageSlots,
                                       QWidget* parent)
    : QDialog(parent)
    , m_selectedColor(Qt::white)
    , m_usedScalarSlots(usedScalarSlots)
    , m_usedColorSlots(usedColorSlots)
    , m_usedImageSlots(usedImageSlots)
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
        QStringLiteral("image"),
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

    // Default value — one widget per type, shown/hidden by onTypeChanged()
    m_defaultFloat = new QDoubleSpinBox(this);
    m_defaultFloat->setRange(-9999.0, 9999.0);
    m_defaultFloat->setDecimals(2);
    m_defaultFloat->setValue(0.0);
    m_defaultFloatLabel = new QLabel(i18n("Default:"), this);
    formLayout->addRow(m_defaultFloatLabel, m_defaultFloat);

    m_defaultInt = new QSpinBox(this);
    m_defaultInt->setRange(-9999, 9999);
    m_defaultInt->setValue(0);
    m_defaultIntLabel = new QLabel(i18n("Default:"), this);
    formLayout->addRow(m_defaultIntLabel, m_defaultInt);

    m_defaultBool = new QCheckBox(i18n("Enabled"), this);
    m_defaultBoolLabel = new QLabel(i18n("Default:"), this);
    formLayout->addRow(m_defaultBoolLabel, m_defaultBool);

    m_defaultColorBtn = new QPushButton(this);
    m_defaultColorBtn->setText(m_selectedColor.name());
    m_defaultColorBtn->setAutoFillBackground(true);
    {
        QPalette colorPal = m_defaultColorBtn->palette();
        colorPal.setColor(QPalette::Button, m_selectedColor);
        colorPal.setColor(QPalette::ButtonText, Qt::black);
        m_defaultColorBtn->setPalette(colorPal);
    }
    m_defaultColorLabel = new QLabel(i18n("Default:"), this);
    formLayout->addRow(m_defaultColorLabel, m_defaultColorBtn);

    m_defaultImageRow = new QWidget(this);
    auto* imageLayout = new QHBoxLayout(m_defaultImageRow);
    imageLayout->setContentsMargins(0, 0, 0, 0);
    m_imagePathLabel = new QLabel(i18n("(none)"), m_defaultImageRow);
    m_imagePathLabel->setStyleSheet(QStringLiteral("color: palette(mid);"));
    m_defaultImageBtn = new QPushButton(i18n("Browse..."), m_defaultImageRow);
    m_defaultImageBtn->setFixedWidth(80);
    m_imageClearBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("edit-clear")), QString(), m_defaultImageRow);
    m_imageClearBtn->setFixedSize(22, 22);
    m_imageClearBtn->setToolTip(i18n("Clear"));
    m_imageClearBtn->setVisible(false);
    imageLayout->addWidget(m_defaultImageBtn);
    imageLayout->addWidget(m_imagePathLabel, 1);
    imageLayout->addWidget(m_imageClearBtn);
    m_defaultImageLabel = new QLabel(i18n("Default:"), this);
    formLayout->addRow(m_defaultImageLabel, m_defaultImageRow);

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
    connect(m_defaultImageBtn, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(
            this, i18n("Select Default Image"), QString(),
            i18n("Images") + QStringLiteral(" (*.png *.jpg *.jpeg *.bmp *.webp *.tga)"));
        if (!path.isEmpty()) {
            m_selectedImagePath = path;
            m_imagePathLabel->setText(QFileInfo(path).fileName());
            m_imagePathLabel->setToolTip(path);
            m_imagePathLabel->setStyleSheet(QString());
            m_imageClearBtn->setVisible(true);
        }
    });
    connect(m_imageClearBtn, &QPushButton::clicked, this, [this]() {
        m_selectedImagePath.clear();
        m_imagePathLabel->setText(i18n("(none)"));
        m_imagePathLabel->setToolTip(QString());
        m_imagePathLabel->setStyleSheet(QStringLiteral("color: palette(mid);"));
        m_imageClearBtn->setVisible(false);
    });
}

void AddParameterDialog::onTypeChanged()
{
    const QString type = m_typeCombo->currentText();
    const bool isFloat = (type == QLatin1String("float"));
    const bool isInt = (type == QLatin1String("int"));
    const bool isBool = (type == QLatin1String("bool"));
    const bool isColor = (type == QLatin1String("color"));
    const bool isImage = (type == QLatin1String("image"));
    const bool showRange = isFloat || isInt;

    // Show only the default row for the selected type
    m_defaultFloatLabel->setVisible(isFloat);
    m_defaultFloat->setVisible(isFloat);
    m_defaultInt->setVisible(isInt);
    m_defaultIntLabel->setVisible(isInt);
    m_defaultBool->setVisible(isBool);
    m_defaultBoolLabel->setVisible(isBool);
    m_defaultColorBtn->setVisible(isColor);
    m_defaultColorLabel->setVisible(isColor);
    m_defaultImageRow->setVisible(isImage);
    m_defaultImageLabel->setVisible(isImage);

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
    const bool isImage = (type == QLatin1String("image"));
    const QSet<int>& usedSlots = isImage ? m_usedImageSlots : (isColor ? m_usedColorSlots : m_usedScalarSlots);
    const int maxSlot = isImage ? 3 : (isColor ? 15 : 31);

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

    if (type == QLatin1String("image")) {
        if (!m_selectedImagePath.isEmpty()) {
            param[QStringLiteral("default")] = m_selectedImagePath;
        }
    } else if (type == QLatin1String("bool")) {
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
