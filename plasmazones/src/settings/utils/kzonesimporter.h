// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QJsonArray>
#include <QString>

namespace PlasmaZones::KZonesImporter {

/// Result of a KZones → PlasmaZones import operation. Emitted back to
/// QML via `SettingsController::kzonesImportFinished(count, message)`;
/// the `pendingSelectLayoutId` is stashed on SettingsController so the
/// next D-Bus-driven layout refresh selects the first imported layout.
struct ImportResult
{
    int imported = 0;
    QString message;
    QString pendingSelectLayoutId;
};

/// True if the user's kwinrc has a Script-kzones layoutsJson entry.
bool hasKZonesConfig();

/// Read layouts from `~/.config/kwinrc` → Script-kzones → layoutsJson,
/// convert each to a PlasmaZones layout, and create it via D-Bus
/// (LayoutRegistry::createLayoutFromJson). Returns the count + user-
/// facing status message + the id of the first successfully created
/// layout (for auto-selection in the layouts list).
ImportResult importFromKwinrc();

/// Same as importFromKwinrc but reads from an arbitrary JSON file.
/// Accepts either a JSON array of layouts or a single layout object.
ImportResult importFromFile(const QString& filePath);

/// Pure conversion + D-Bus create. Callers are
/// `importFromKwinrc` / `importFromFile`; exposed here only for
/// direct-array callers (there are none right now — keep internal).
ImportResult importLayouts(const QJsonArray& kzonesArray);

} // namespace PlasmaZones::KZonesImporter
