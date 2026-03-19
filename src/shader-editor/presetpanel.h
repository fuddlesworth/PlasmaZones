// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QJsonObject>
#include <QVariantMap>
#include <QWidget>

class QListWidget;
class QListWidgetItem;
class QPushButton;

namespace PlasmaZones {

/**
 * Panel for saving/loading/managing named parameter presets.
 * Presets are stored in metadata.json under a "presets" key.
 */
class PresetPanel : public QWidget
{
    Q_OBJECT

public:
    explicit PresetPanel(QWidget* parent = nullptr);
    ~PresetPanel() override;

    /** Load presets from a metadata JSON object. */
    void loadFromMetadata(const QString& metadataJson);

    /** Return all presets as a JSON object (name → {uniform: value, ...}). */
    QJsonObject presetsJson() const;

Q_SIGNALS:
    /** Emitted when the user selects a preset to apply. */
    void presetSelected(const QVariantMap& uniformValues);

    /** Emitted when presets are added/removed/renamed (metadata needs saving). */
    void modified();

    /** Request current parameter values for saving. */
    void captureRequested();

public Q_SLOTS:
    /** Called by the editor window with current uniform values after captureRequested. */
    void saveCurrentValues(const QVariantMap& uniformValues);

private:
    void setupUi();
    void onSavePreset();
    void onDeletePreset();
    void onRenamePreset();
    void onPresetClicked(QListWidgetItem* item);
    void updateButtons();

    QListWidget* m_listWidget = nullptr;
    QPushButton* m_saveBtn = nullptr;
    QPushButton* m_deleteBtn = nullptr;
    QPushButton* m_renameBtn = nullptr;

    // Stored presets: name → {uniformName: value, ...}
    QMap<QString, QVariantMap> m_presets;
};

} // namespace PlasmaZones
