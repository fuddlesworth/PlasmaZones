// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "metadataeditorwidget.h"
#include "addparameterdialog.h"
#include "shaderpackageio.h"

#include <QCheckBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QLoggingCategory>
#include <QMenu>
#include <QPushButton>
#include <QScrollArea>
#include <QTreeWidget>
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
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(12);

    // ── Shader Info ──
    auto* infoLayout = new QFormLayout;
    infoLayout->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    infoLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    // ID: shown as read-only label for existing shaders, editable for new
    m_idLabel = new QLabel(this);
    m_idLabel->setTextFormat(Qt::PlainText);
    m_idLabel->setVisible(false);
    m_idEdit = new QLineEdit(this);
    m_idEdit->setPlaceholderText(i18nc("@info:placeholder example shader ID", "my-custom-shader"));
    m_idEdit->setVisible(false);
    // Stack both in a container — only one is visible at a time
    auto* idContainer = new QWidget(this);
    auto* idLayout = new QHBoxLayout(idContainer);
    idLayout->setContentsMargins(0, 0, 0, 0);
    idLayout->addWidget(m_idLabel);
    idLayout->addWidget(m_idEdit);
    infoLayout->addRow(i18n("ID:"), idContainer);

    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setPlaceholderText(i18n("Shader display name"));
    infoLayout->addRow(i18n("Name:"), m_nameEdit);

    m_categoryEdit = new QLineEdit(this);
    m_categoryEdit->setPlaceholderText(i18n("e.g. Organic, Energy, Audio Visualizer"));
    infoLayout->addRow(i18n("Category:"), m_categoryEdit);

    auto* authorVersionRow = new QHBoxLayout;
    m_authorEdit = new QLineEdit(this);
    m_authorEdit->setPlaceholderText(i18n("Author name"));
    m_versionEdit = new QLineEdit(this);
    m_versionEdit->setPlaceholderText(QStringLiteral("1.0"));
    m_versionEdit->setMaximumWidth(60);
    authorVersionRow->addWidget(m_authorEdit, 1);
    authorVersionRow->addWidget(new QLabel(i18n("Version:"), this));
    authorVersionRow->addWidget(m_versionEdit);
    infoLayout->addRow(i18n("Author:"), authorVersionRow);

    m_descriptionEdit = new QLineEdit(this);
    m_descriptionEdit->setPlaceholderText(i18n("Short description of the shader effect"));
    infoLayout->addRow(i18n("Description:"), m_descriptionEdit);

    m_multipassCheck = new QCheckBox(i18n("Enable multipass rendering"), this);
    infoLayout->addRow(QString(), m_multipassCheck);

    m_wallpaperCheck = new QCheckBox(i18n("Enable wallpaper texture (uWallpaper, binding 11)"), this);
    infoLayout->addRow(QString(), m_wallpaperCheck);

    mainLayout->addLayout(infoLayout);

    // ── Parameters ──
    auto* paramHeader = new QHBoxLayout;
    paramHeader->addWidget(new QLabel(QStringLiteral("<b>%1</b>").arg(i18n("Parameters")), this));
    paramHeader->addStretch();

    auto* addBtn = new QPushButton(i18n("Add..."), this);
    addBtn->setIcon(QIcon::fromTheme(QStringLiteral("list-add")));
    paramHeader->addWidget(addBtn);

    mainLayout->addLayout(paramHeader);

    // QTreeWidget — cleaner than QTableWidget, supports variable row height
    m_paramTree = new QTreeWidget(this);
    m_paramTree->setHeaderLabels({
        i18n("Name"),
        i18n("Type"),
        i18n("Slot"),
        i18n("Default"),
        i18n("Group"),
    });
    m_paramTree->setRootIsDecorated(false);
    m_paramTree->setAlternatingRowColors(true);
    m_paramTree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_paramTree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_paramTree->header()->setStretchLastSection(true);
    m_paramTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_paramTree->setMinimumHeight(150);

    mainLayout->addWidget(m_paramTree, 1);
    mainLayout->addStretch();

    scrollArea->setWidget(contentWidget);

    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->addWidget(scrollArea);

    // Context menu for parameter tree
    m_paramContextMenu = new QMenu(this);
    m_paramContextMenu->addAction(QIcon::fromTheme(QStringLiteral("insert-text")),
                                  i18n("Insert Uniform"), this, &MetadataEditorWidget::onInsertUniform);
    m_paramContextMenu->addSeparator();
    m_paramContextMenu->addAction(QIcon::fromTheme(QStringLiteral("go-up")),
                                  i18n("Move Up"), this, &MetadataEditorWidget::onMoveParameterUp);
    m_paramContextMenu->addAction(QIcon::fromTheme(QStringLiteral("go-down")),
                                  i18n("Move Down"), this, &MetadataEditorWidget::onMoveParameterDown);
    m_paramContextMenu->addSeparator();
    m_paramContextMenu->addAction(QIcon::fromTheme(QStringLiteral("edit-delete")),
                                  i18n("Remove"), this, &MetadataEditorWidget::onRemoveParameter);

    connect(addBtn, &QPushButton::clicked, this, &MetadataEditorWidget::onAddParameter);
    connect(m_paramTree, &QTreeWidget::customContextMenuRequested, this, &MetadataEditorWidget::showParameterContextMenu);
}

void MetadataEditorWidget::connectSignals()
{
    connect(m_idEdit, &QLineEdit::textChanged, this, &MetadataEditorWidget::markModified);
    connect(m_nameEdit, &QLineEdit::textChanged, this, &MetadataEditorWidget::markModified);
    connect(m_categoryEdit, &QLineEdit::textChanged, this, &MetadataEditorWidget::markModified);
    connect(m_authorEdit, &QLineEdit::textChanged, this, &MetadataEditorWidget::markModified);
    connect(m_versionEdit, &QLineEdit::textChanged, this, &MetadataEditorWidget::markModified);
    connect(m_descriptionEdit, &QLineEdit::textChanged, this, &MetadataEditorWidget::markModified);
    connect(m_multipassCheck, &QCheckBox::toggled, this, &MetadataEditorWidget::markModified);
    connect(m_wallpaperCheck, &QCheckBox::toggled, this, &MetadataEditorWidget::markModified);
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
    m_isExistingShader = !id.isEmpty();

    if (m_isExistingShader) {
        m_idLabel->setText(id);
        m_idLabel->setVisible(true);
        m_idEdit->setVisible(false);
    } else {
        m_idEdit->setText(id);
        m_idLabel->setVisible(false);
        m_idEdit->setVisible(true);
    }

    m_nameEdit->setText(obj.value(QStringLiteral("name")).toString());

    m_categoryEdit->setText(obj.value(QStringLiteral("category")).toString());

    m_authorEdit->setText(obj.value(QStringLiteral("author")).toString());
    m_versionEdit->setText(obj.value(QStringLiteral("version")).toString());
    m_descriptionEdit->setText(obj.value(QStringLiteral("description")).toString());
    m_multipassCheck->setChecked(obj.value(QStringLiteral("multipass")).toBool(false));
    m_wallpaperCheck->setChecked(obj.value(QStringLiteral("wallpaper")).toBool(false));

    // Load parameters
    m_paramTree->clear();
    const QJsonArray params = obj.value(QStringLiteral("parameters")).toArray();
    for (const QJsonValue& paramVal : params) {
        addParameterRow(paramVal.toObject());
    }

    for (int i = 0; i < m_paramTree->columnCount(); ++i) {
        m_paramTree->resizeColumnToContents(i);
    }

    m_loading = false;
    m_modified = false;
}

void MetadataEditorWidget::addParameterRow(const QJsonObject& param)
{
    const QString name = param.value(QStringLiteral("name")).toString();
    const QString type = param.value(QStringLiteral("type")).toString();
    const int slot = param.value(QStringLiteral("slot")).toInt(-1);
    const QString group = param.value(QStringLiteral("group")).toString();

    auto* item = new QTreeWidgetItem(m_paramTree);
    item->setText(0, name);
    item->setText(1, type);
    item->setText(2, QString::number(slot));
    item->setText(3, formatDefaultValue(param));
    item->setText(4, group);

    // Store full JSON in UserRole for round-trip fidelity
    item->setData(0, Qt::UserRole, QJsonDocument(param).toJson(QJsonDocument::Compact));

    // Tooltip shows ID and uniform name
    const QString id = param.value(QStringLiteral("id")).toString();
    const QString uniformName = ShaderPackageIO::computeUniformName(type, slot);
    QString tooltip = QStringLiteral("ID: %1").arg(id);
    if (!uniformName.isEmpty()) {
        tooltip += QStringLiteral("\nUniform: %1").arg(uniformName);
    }
    if (param.contains(QStringLiteral("min")) && param.contains(QStringLiteral("max"))) {
        tooltip += QStringLiteral("\nRange: %1 – %2")
                       .arg(param.value(QStringLiteral("min")).toDouble())
                       .arg(param.value(QStringLiteral("max")).toDouble());
    }
    item->setToolTip(0, tooltip);
    item->setToolTip(1, tooltip);
    item->setToolTip(2, tooltip);
}

QString MetadataEditorWidget::formatDefaultValue(const QJsonObject& param)
{
    const QString type = param.value(QStringLiteral("type")).toString();
    const QJsonValue def = param.value(QStringLiteral("default"));

    if (type == QLatin1String("bool")) {
        return def.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    }
    if (type == QLatin1String("color")) {
        return def.toString();
    }
    return QString::number(def.toDouble());
}

QString MetadataEditorWidget::toJson() const
{
    QJsonObject obj = m_originalJson;

    // ID: from label (existing) or edit field (new)
    if (m_isExistingShader) {
        // Preserve original ID
    } else {
        obj[QStringLiteral("id")] = m_idEdit->text().trimmed();
    }

    obj[QStringLiteral("name")] = m_nameEdit->text().trimmed();
    obj[QStringLiteral("category")] = m_categoryEdit->text().trimmed();
    obj[QStringLiteral("description")] = m_descriptionEdit->text().trimmed();
    obj[QStringLiteral("author")] = m_authorEdit->text().trimmed();
    obj[QStringLiteral("version")] = m_versionEdit->text().trimmed();
    obj[QStringLiteral("multipass")] = m_multipassCheck->isChecked();
    if (m_wallpaperCheck->isChecked()) {
        obj[QStringLiteral("wallpaper")] = true;
    } else {
        obj.remove(QStringLiteral("wallpaper"));
    }

    // Build parameters array from tree items
    QJsonArray params;
    for (int i = 0; i < m_paramTree->topLevelItemCount(); ++i) {
        const auto* item = m_paramTree->topLevelItem(i);
        const QByteArray storedJson = item->data(0, Qt::UserRole).toByteArray();
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

void MetadataEditorWidget::showParameterContextMenu(const QPoint& pos)
{
    if (m_paramTree->topLevelItemCount() == 0) {
        return;
    }
    m_paramContextMenu->exec(m_paramTree->viewport()->mapToGlobal(pos));
}

void MetadataEditorWidget::onAddParameter()
{
    // Collect used slots
    QSet<int> usedScalarSlots;
    QSet<int> usedColorSlots;
    QSet<int> usedImageSlots;

    for (int i = 0; i < m_paramTree->topLevelItemCount(); ++i) {
        const auto* item = m_paramTree->topLevelItem(i);
        const QString type = item->text(1);
        const int slot = item->text(2).toInt();
        if (type == QLatin1String("color")) {
            usedColorSlots.insert(slot);
        } else if (type == QLatin1String("image")) {
            usedImageSlots.insert(slot);
        } else {
            usedScalarSlots.insert(slot);
        }
    }

    AddParameterDialog dialog(usedScalarSlots, usedColorSlots, usedImageSlots, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    addParameterRow(dialog.parameterJson());

    for (int i = 0; i < m_paramTree->columnCount(); ++i) {
        m_paramTree->resizeColumnToContents(i);
    }

    markModified();
}

void MetadataEditorWidget::onRemoveParameter()
{
    const auto* item = m_paramTree->currentItem();
    if (!item) {
        return;
    }
    delete item;
    markModified();
}

void MetadataEditorWidget::onInsertUniform()
{
    const auto* item = m_paramTree->currentItem();
    if (!item) {
        return;
    }

    const QString type = item->text(1);
    const int slot = item->text(2).toInt();
    const QString uniformName = ShaderPackageIO::computeUniformName(type, slot);
    if (!uniformName.isEmpty()) {
        Q_EMIT insertUniformRequested(uniformName);
    }
}

void MetadataEditorWidget::onMoveParameterUp()
{
    const int index = m_paramTree->indexOfTopLevelItem(m_paramTree->currentItem());
    if (index <= 0) {
        return;
    }
    auto* item = m_paramTree->takeTopLevelItem(index);
    m_paramTree->insertTopLevelItem(index - 1, item);
    m_paramTree->setCurrentItem(item);
    markModified();
}

void MetadataEditorWidget::onMoveParameterDown()
{
    const int index = m_paramTree->indexOfTopLevelItem(m_paramTree->currentItem());
    if (index < 0 || index >= m_paramTree->topLevelItemCount() - 1) {
        return;
    }
    auto* item = m_paramTree->takeTopLevelItem(index);
    m_paramTree->insertTopLevelItem(index + 1, item);
    m_paramTree->setCurrentItem(item);
    markModified();
}

void MetadataEditorWidget::updateParameterDefaults(const QVariantMap& uniformValues)
{
    bool changed = false;

    for (int i = 0; i < m_paramTree->topLevelItemCount(); ++i) {
        auto* item = m_paramTree->topLevelItem(i);
        const QByteArray storedJson = item->data(0, Qt::UserRole).toByteArray();
        QJsonDocument paramDoc = QJsonDocument::fromJson(storedJson);
        if (!paramDoc.isObject()) continue;

        QJsonObject param = paramDoc.object();
        const QString type = param.value(QStringLiteral("type")).toString();
        const int slot = param.value(QStringLiteral("slot")).toInt(-1);
        const QString uniformName = ShaderPackageIO::computeUniformName(type, slot);
        if (uniformName.isEmpty()) continue;

        auto it = uniformValues.find(uniformName);
        if (it == uniformValues.end()) continue;

        if (type == QLatin1String("float") || type == QLatin1String("int")) {
            param[QStringLiteral("default")] = it->toDouble();
        } else if (type == QLatin1String("bool")) {
            param[QStringLiteral("default")] = it->toDouble() > 0.5;
        } else if (type == QLatin1String("color")) {
            param[QStringLiteral("default")] = it->toString();
        }

        item->setData(0, Qt::UserRole, QJsonDocument(param).toJson(QJsonDocument::Compact));
        item->setText(3, formatDefaultValue(param));
        changed = true;
    }

    if (changed) {
        markModified();
    }
}

} // namespace PlasmaZones
