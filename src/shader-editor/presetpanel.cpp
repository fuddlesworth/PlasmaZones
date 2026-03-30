// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "presetpanel.h"

#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QLineEdit>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include "../pz_i18n.h"

namespace PlasmaZones {

PresetPanel::PresetPanel(QWidget* parent)
    : QWidget(parent)
{
    setupUi();
}

PresetPanel::~PresetPanel() = default;

void PresetPanel::setupUi()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    auto* header = new QLabel(QStringLiteral("<b>%1</b>").arg(PzI18n::tr("Parameter Presets")), this);
    layout->addWidget(header);

    auto* desc = new QLabel(PzI18n::tr("Save and load named parameter configurations."), this);
    desc->setWordWrap(true);
    QPalette descPal = desc->palette();
    descPal.setColor(QPalette::WindowText, palette().color(QPalette::PlaceholderText));
    desc->setPalette(descPal);
    layout->addWidget(desc);

    m_listWidget = new QListWidget(this);
    m_listWidget->setAlternatingRowColors(true);
    layout->addWidget(m_listWidget, 1);

    connect(m_listWidget, &QListWidget::itemDoubleClicked, this, &PresetPanel::onPresetClicked);
    connect(m_listWidget, &QListWidget::currentRowChanged, this, [this]() {
        updateButtons();
    });

    // Buttons
    auto* buttonRow = new QHBoxLayout;
    buttonRow->setSpacing(4);

    m_applyBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("dialog-ok-apply")), PzI18n::tr("Apply"), this);
    m_saveBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("list-add")), PzI18n::tr("Save Current"), this);
    m_deleteBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("list-remove")), PzI18n::tr("Delete"), this);
    m_renameBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("edit-rename")), PzI18n::tr("Rename"), this);

    buttonRow->addWidget(m_saveBtn);
    buttonRow->addWidget(m_renameBtn);
    buttonRow->addWidget(m_deleteBtn);
    buttonRow->addStretch();
    buttonRow->addWidget(m_applyBtn);

    layout->addLayout(buttonRow);

    connect(m_applyBtn, &QPushButton::clicked, this, [this]() {
        if (auto* item = m_listWidget->currentItem()) {
            onPresetClicked(item);
        }
    });
    connect(m_saveBtn, &QPushButton::clicked, this, &PresetPanel::onSavePreset);
    connect(m_deleteBtn, &QPushButton::clicked, this, &PresetPanel::onDeletePreset);
    connect(m_renameBtn, &QPushButton::clicked, this, &PresetPanel::onRenamePreset);

    updateButtons();
}

void PresetPanel::loadFromMetadata(const QString& metadataJson)
{
    m_presets.clear();
    m_listWidget->clear();

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(metadataJson.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return;
    }

    const QJsonObject presetsObj = doc.object().value(QStringLiteral("presets")).toObject();
    for (auto it = presetsObj.begin(); it != presetsObj.end(); ++it) {
        const QJsonObject values = it.value().toObject();
        QVariantMap map;
        for (auto vit = values.begin(); vit != values.end(); ++vit) {
            map[vit.key()] = vit.value().toVariant();
        }
        m_presets[it.key()] = map;
        m_listWidget->addItem(it.key());
    }

    updateButtons();
}

QJsonObject PresetPanel::presetsJson() const
{
    QJsonObject result;
    for (auto it = m_presets.begin(); it != m_presets.end(); ++it) {
        QJsonObject values;
        for (auto vit = it.value().begin(); vit != it.value().end(); ++vit) {
            values[vit.key()] = QJsonValue::fromVariant(vit.value());
        }
        result[it.key()] = values;
    }
    return result;
}

void PresetPanel::onSavePreset()
{
    Q_EMIT captureRequested();
}

void PresetPanel::saveCurrentValues(const QVariantMap& uniformValues)
{
    QInputDialog dlg(this);
    dlg.setWindowTitle(PzI18n::tr("Save Preset"));
    dlg.setLabelText(PzI18n::tr("Preset name:"));
    dlg.setInputMode(QInputDialog::TextInput);
    dlg.resize(320, 120);
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }
    QString name = dlg.textValue();
    if (name.trimmed().isEmpty()) {
        return;
    }
    name = name.trimmed();

    if (m_presets.contains(name)) {
        const int result =
            QMessageBox::question(this, PzI18n::tr("Overwrite Preset"),
                                  PzI18n::tr("A preset named \"%1\" already exists. Overwrite?").arg(name));
        if (result != QMessageBox::Yes) {
            return;
        }
    } else {
        m_listWidget->addItem(name);
    }

    m_presets[name] = uniformValues;
    Q_EMIT modified();
}

void PresetPanel::onDeletePreset()
{
    auto* item = m_listWidget->currentItem();
    if (!item)
        return;

    const QString name = item->text();
    const int result =
        QMessageBox::question(this, PzI18n::tr("Delete Preset"), PzI18n::tr("Delete preset \"%1\"?").arg(name));
    if (result != QMessageBox::Yes)
        return;

    m_presets.remove(name);
    delete m_listWidget->takeItem(m_listWidget->row(item));
    Q_EMIT modified();
}

void PresetPanel::onRenamePreset()
{
    auto* item = m_listWidget->currentItem();
    if (!item)
        return;

    const QString oldName = item->text();
    QInputDialog dlg(this);
    dlg.setWindowTitle(PzI18n::tr("Rename Preset"));
    dlg.setLabelText(PzI18n::tr("New name:"));
    dlg.setTextValue(oldName);
    dlg.resize(320, 120);
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }
    QString newName = dlg.textValue();
    if (newName.trimmed().isEmpty() || newName.trimmed() == oldName) {
        return;
    }
    newName = newName.trimmed();

    if (m_presets.contains(newName)) {
        QMessageBox::warning(this, PzI18n::tr("Rename Preset"),
                             PzI18n::tr("A preset named \"%1\" already exists.").arg(newName));
        return;
    }

    m_presets[newName] = m_presets.take(oldName);
    item->setText(newName);
    Q_EMIT modified();
}

void PresetPanel::onPresetClicked(QListWidgetItem* item)
{
    if (!item)
        return;
    const QString name = item->text();
    if (m_presets.contains(name)) {
        Q_EMIT presetSelected(m_presets[name]);
    }
}

void PresetPanel::updateButtons()
{
    const bool hasSelection = m_listWidget->currentItem() != nullptr;
    m_applyBtn->setEnabled(hasSelection);
    m_deleteBtn->setEnabled(hasSelection);
    m_renameBtn->setEnabled(hasSelection);
}

} // namespace PlasmaZones
