// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "metadataeditorwidget.h"
#include "addparameterdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QLoggingCategory>
#include <QPushButton>
#include <QScrollArea>
#include <QTableWidget>
#include <QTextEdit>
#include <QVBoxLayout>

#include <KLocalizedString>

Q_LOGGING_CATEGORY(lcMetadataEditor, "plasmazones.shadereditor.metadata")

namespace PlasmaZones {

MetadataEditorWidget::MetadataEditorWidget(QWidget* parent)
    : QWidget(parent)
{
    setupUi();
    connectSignals();
}

MetadataEditorWidget::~MetadataEditorWidget() = default;

void MetadataEditorWidget::setupUi()
{
    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    auto* contentWidget = new QWidget(scrollArea);
    auto* mainLayout = new QVBoxLayout(contentWidget);

    // --- Section 1: Shader Info ---
    auto* infoGroup = new QGroupBox(i18n("Shader Info"), contentWidget);
    auto* infoLayout = new QFormLayout(infoGroup);

    m_idEdit = new QLineEdit(infoGroup);
    m_idEdit->setPlaceholderText(i18n("e.g. my-custom-shader"));
    infoLayout->addRow(i18n("ID:"), m_idEdit);

    m_nameEdit = new QLineEdit(infoGroup);
    m_nameEdit->setPlaceholderText(i18n("e.g. My Custom Shader"));
    infoLayout->addRow(i18n("Name:"), m_nameEdit);

    m_categoryCombo = new QComboBox(infoGroup);
    m_categoryCombo->setEditable(true);
    m_categoryCombo->addItems({
        QStringLiteral("Custom"),
        QStringLiteral("Organic"),
        QStringLiteral("Energy"),
        QStringLiteral("Geometric"),
        QStringLiteral("Audio Visualizer"),
        QStringLiteral("Abstract"),
        QStringLiteral("Retro"),
        QStringLiteral("Minimal"),
    });
    infoLayout->addRow(i18n("Category:"), m_categoryCombo);

    m_authorEdit = new QLineEdit(infoGroup);
    infoLayout->addRow(i18n("Author:"), m_authorEdit);

    m_versionEdit = new QLineEdit(infoGroup);
    m_versionEdit->setPlaceholderText(QStringLiteral("1.0"));
    infoLayout->addRow(i18n("Version:"), m_versionEdit);

    m_descriptionEdit = new QTextEdit(infoGroup);
    m_descriptionEdit->setMaximumHeight(80);
    m_descriptionEdit->setPlaceholderText(i18n("Short description of the shader effect"));
    infoLayout->addRow(i18n("Description:"), m_descriptionEdit);

    m_fragShaderLabel = new QLabel(QStringLiteral("effect.frag"), infoGroup);
    m_fragShaderLabel->setEnabled(false);
    infoLayout->addRow(i18n("Fragment Shader:"), m_fragShaderLabel);

    m_vertShaderLabel = new QLabel(QStringLiteral("zone.vert"), infoGroup);
    m_vertShaderLabel->setEnabled(false);
    infoLayout->addRow(i18n("Vertex Shader:"), m_vertShaderLabel);

    m_multipassCheck = new QCheckBox(i18n("Enable multipass rendering"), infoGroup);
    infoLayout->addRow(i18n("Multipass:"), m_multipassCheck);

    mainLayout->addWidget(infoGroup);

    // --- Section 2: Parameters ---
    auto* paramsGroup = new QGroupBox(i18n("Parameters"), contentWidget);
    auto* paramsLayout = new QVBoxLayout(paramsGroup);

    m_paramTable = new QTableWidget(0, 8, paramsGroup);
    m_paramTable->setHorizontalHeaderLabels({
        i18n("Name"),
        i18n("ID"),
        i18n("Type"),
        i18n("Slot"),
        i18n("Default"),
        i18n("Min"),
        i18n("Max"),
        i18n("Group"),
    });
    m_paramTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_paramTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_paramTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_paramTable->horizontalHeader()->setStretchLastSection(true);
    m_paramTable->verticalHeader()->setVisible(false);
    m_paramTable->setAlternatingRowColors(true);
    paramsLayout->addWidget(m_paramTable);

    auto* buttonLayout = new QHBoxLayout;
    auto* addBtn = new QPushButton(i18n("Add Parameter"), paramsGroup);
    auto* removeBtn = new QPushButton(i18n("Remove Parameter"), paramsGroup);
    auto* insertBtn = new QPushButton(i18n("Insert Uniform"), paramsGroup);

    buttonLayout->addWidget(addBtn);
    buttonLayout->addWidget(removeBtn);
    buttonLayout->addStretch();
    buttonLayout->addWidget(insertBtn);
    paramsLayout->addLayout(buttonLayout);

    mainLayout->addWidget(paramsGroup);
    mainLayout->addStretch();

    scrollArea->setWidget(contentWidget);

    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->addWidget(scrollArea);

    connect(addBtn, &QPushButton::clicked, this, &MetadataEditorWidget::onAddParameter);
    connect(removeBtn, &QPushButton::clicked, this, &MetadataEditorWidget::onRemoveParameter);
    connect(insertBtn, &QPushButton::clicked, this, &MetadataEditorWidget::onInsertUniform);
}

void MetadataEditorWidget::connectSignals()
{
    // Track modifications from all form fields
    connect(m_idEdit, &QLineEdit::textChanged, this, &MetadataEditorWidget::markModified);
    connect(m_nameEdit, &QLineEdit::textChanged, this, &MetadataEditorWidget::markModified);
    connect(m_categoryCombo, &QComboBox::currentTextChanged, this, &MetadataEditorWidget::markModified);
    connect(m_authorEdit, &QLineEdit::textChanged, this, &MetadataEditorWidget::markModified);
    connect(m_versionEdit, &QLineEdit::textChanged, this, &MetadataEditorWidget::markModified);
    connect(m_descriptionEdit, &QTextEdit::textChanged, this, &MetadataEditorWidget::markModified);
    connect(m_multipassCheck, &QCheckBox::toggled, this, &MetadataEditorWidget::markModified);
}

void MetadataEditorWidget::loadFromJson(const QString& json)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        qCWarning(lcMetadataEditor) << "Failed to parse metadata JSON:" << parseError.errorString();
        return;
    }

    const QJsonObject obj = doc.object();
    m_originalJson = obj;

    m_loading = true;

    const QString id = obj.value(QStringLiteral("id")).toString();
    m_idEdit->setText(id);

    // If we have a non-empty ID, this is an existing shader — make ID read-only
    m_isExistingShader = !id.isEmpty();
    m_idEdit->setReadOnly(m_isExistingShader);

    m_nameEdit->setText(obj.value(QStringLiteral("name")).toString());

    const QString category = obj.value(QStringLiteral("category")).toString();
    const int catIdx = m_categoryCombo->findText(category);
    if (catIdx >= 0) {
        m_categoryCombo->setCurrentIndex(catIdx);
    } else {
        m_categoryCombo->setCurrentText(category);
    }

    m_authorEdit->setText(obj.value(QStringLiteral("author")).toString());
    m_versionEdit->setText(obj.value(QStringLiteral("version")).toString());
    m_descriptionEdit->setPlainText(obj.value(QStringLiteral("description")).toString());

    const QString fragShader = obj.value(QStringLiteral("fragmentShader")).toString();
    if (!fragShader.isEmpty()) {
        m_fragShaderLabel->setText(fragShader);
    }
    const QString vertShader = obj.value(QStringLiteral("vertexShader")).toString();
    if (!vertShader.isEmpty()) {
        m_vertShaderLabel->setText(vertShader);
    }

    m_multipassCheck->setChecked(obj.value(QStringLiteral("multipass")).toBool(false));

    // Load parameters
    m_paramTable->setRowCount(0);
    const QJsonArray params = obj.value(QStringLiteral("parameters")).toArray();
    for (const QJsonValue& paramVal : params) {
        const QJsonObject param = paramVal.toObject();
        const int row = m_paramTable->rowCount();
        m_paramTable->insertRow(row);

        const QString type = param.value(QStringLiteral("type")).toString();

        m_paramTable->setItem(row, 0, new QTableWidgetItem(param.value(QStringLiteral("name")).toString()));
        m_paramTable->setItem(row, 1, new QTableWidgetItem(param.value(QStringLiteral("id")).toString()));
        m_paramTable->setItem(row, 2, new QTableWidgetItem(type));
        m_paramTable->setItem(row, 3, new QTableWidgetItem(QString::number(param.value(QStringLiteral("slot")).toInt())));

        // Default value display
        QString defaultStr;
        if (type == QLatin1String("bool")) {
            defaultStr = param.value(QStringLiteral("default")).toBool() ? QStringLiteral("true") : QStringLiteral("false");
        } else if (type == QLatin1String("color")) {
            defaultStr = param.value(QStringLiteral("default")).toString();
        } else {
            defaultStr = QString::number(param.value(QStringLiteral("default")).toDouble());
        }
        m_paramTable->setItem(row, 4, new QTableWidgetItem(defaultStr));

        // Min/Max (empty for bool/color)
        QString minStr, maxStr;
        if (type != QLatin1String("bool") && type != QLatin1String("color")) {
            if (param.contains(QStringLiteral("min"))) {
                minStr = QString::number(param.value(QStringLiteral("min")).toDouble());
            }
            if (param.contains(QStringLiteral("max"))) {
                maxStr = QString::number(param.value(QStringLiteral("max")).toDouble());
            }
        }
        m_paramTable->setItem(row, 5, new QTableWidgetItem(minStr));
        m_paramTable->setItem(row, 6, new QTableWidgetItem(maxStr));

        m_paramTable->setItem(row, 7, new QTableWidgetItem(param.value(QStringLiteral("group")).toString()));

        // Store full JSON object in UserRole on the first column item
        m_paramTable->item(row, 0)->setData(Qt::UserRole, QJsonDocument(param).toJson(QJsonDocument::Compact));
    }

    m_paramTable->resizeColumnsToContents();

    m_loading = false;
    m_modified = false;
}

QString MetadataEditorWidget::toJson() const
{
    QJsonObject obj = m_originalJson;
    obj[QStringLiteral("id")] = m_idEdit->text().trimmed();
    obj[QStringLiteral("name")] = m_nameEdit->text().trimmed();
    obj[QStringLiteral("category")] = m_categoryCombo->currentText().trimmed();
    obj[QStringLiteral("description")] = m_descriptionEdit->toPlainText().trimmed();
    obj[QStringLiteral("author")] = m_authorEdit->text().trimmed();
    obj[QStringLiteral("version")] = m_versionEdit->text().trimmed();
    obj[QStringLiteral("fragmentShader")] = m_fragShaderLabel->text();
    obj[QStringLiteral("vertexShader")] = m_vertShaderLabel->text();

    obj[QStringLiteral("multipass")] = m_multipassCheck->isChecked();

    // Build parameters array from stored JSON in table rows
    QJsonArray params;
    for (int row = 0; row < m_paramTable->rowCount(); ++row) {
        const QTableWidgetItem* nameItem = m_paramTable->item(row, 0);
        if (!nameItem) {
            continue;
        }

        const QByteArray storedJson = nameItem->data(Qt::UserRole).toByteArray();
        if (!storedJson.isEmpty()) {
            QJsonDocument paramDoc = QJsonDocument::fromJson(storedJson);
            if (paramDoc.isObject()) {
                params.append(paramDoc.object());
            }
        }
    }

    obj[QStringLiteral("parameters")] = params;

    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Indented));
}

bool MetadataEditorWidget::isModified() const
{
    return m_modified;
}

void MetadataEditorWidget::setModified(bool modified)
{
    m_modified = modified;
}

void MetadataEditorWidget::markModified()
{
    if (m_loading) return;
    m_modified = true;
    Q_EMIT this->modified();
}

void MetadataEditorWidget::onAddParameter()
{
    // Collect used slots
    QSet<int> usedScalarSlots;
    QSet<int> usedColorSlots;

    for (int row = 0; row < m_paramTable->rowCount(); ++row) {
        const QTableWidgetItem* typeItem = m_paramTable->item(row, 2);
        const QTableWidgetItem* slotItem = m_paramTable->item(row, 3);
        if (!typeItem || !slotItem) {
            continue;
        }

        const int slot = slotItem->text().toInt();
        if (typeItem->text() == QLatin1String("color")) {
            usedColorSlots.insert(slot);
        } else {
            usedScalarSlots.insert(slot);
        }
    }

    AddParameterDialog dialog(usedScalarSlots, usedColorSlots, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const QJsonObject param = dialog.parameterJson();
    const int row = m_paramTable->rowCount();
    m_paramTable->insertRow(row);

    const QString type = param.value(QStringLiteral("type")).toString();

    m_paramTable->setItem(row, 0, new QTableWidgetItem(param.value(QStringLiteral("name")).toString()));
    m_paramTable->setItem(row, 1, new QTableWidgetItem(param.value(QStringLiteral("id")).toString()));
    m_paramTable->setItem(row, 2, new QTableWidgetItem(type));
    m_paramTable->setItem(row, 3, new QTableWidgetItem(QString::number(param.value(QStringLiteral("slot")).toInt())));

    QString defaultStr;
    if (type == QLatin1String("bool")) {
        defaultStr = param.value(QStringLiteral("default")).toBool() ? QStringLiteral("true") : QStringLiteral("false");
    } else if (type == QLatin1String("color")) {
        defaultStr = param.value(QStringLiteral("default")).toString();
    } else {
        defaultStr = QString::number(param.value(QStringLiteral("default")).toDouble());
    }
    m_paramTable->setItem(row, 4, new QTableWidgetItem(defaultStr));

    QString minStr, maxStr;
    if (type != QLatin1String("bool") && type != QLatin1String("color")) {
        if (param.contains(QStringLiteral("min"))) {
            minStr = QString::number(param.value(QStringLiteral("min")).toDouble());
        }
        if (param.contains(QStringLiteral("max"))) {
            maxStr = QString::number(param.value(QStringLiteral("max")).toDouble());
        }
    }
    m_paramTable->setItem(row, 5, new QTableWidgetItem(minStr));
    m_paramTable->setItem(row, 6, new QTableWidgetItem(maxStr));

    m_paramTable->setItem(row, 7, new QTableWidgetItem(param.value(QStringLiteral("group")).toString()));

    // Store full JSON
    m_paramTable->item(row, 0)->setData(Qt::UserRole, QJsonDocument(param).toJson(QJsonDocument::Compact));

    m_paramTable->resizeColumnsToContents();
    markModified();
}

void MetadataEditorWidget::onRemoveParameter()
{
    const int row = m_paramTable->currentRow();
    if (row < 0) {
        return;
    }

    m_paramTable->removeRow(row);
    markModified();
}

void MetadataEditorWidget::onInsertUniform()
{
    const int row = m_paramTable->currentRow();
    if (row < 0) {
        return;
    }

    const QTableWidgetItem* typeItem = m_paramTable->item(row, 2);
    const QTableWidgetItem* slotItem = m_paramTable->item(row, 3);
    if (!typeItem || !slotItem) {
        return;
    }

    const QString uniformName = computeUniformName(typeItem->text(), slotItem->text().toInt());
    if (!uniformName.isEmpty()) {
        Q_EMIT insertUniformRequested(uniformName);
    }
}

QString MetadataEditorWidget::computeUniformName(const QString& type, int slot)
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
