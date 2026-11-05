// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorzones_export.h>

#include <QHash>
#include <QJsonObject>
#include <QString>

namespace PhosphorZones {

/**
 * @brief Persists per-layout SETTINGS in a sidecar, keyed by layout UUID,
 *        separately from the structural layout files.
 *
 * A layout file should describe the layout — its zones and geometry, identity,
 * and matching rules. The user-preference SETTINGS that can be tuned per layout
 * (per-zone appearance, gap/padding overrides, showZoneNumbers, overlay display
 * mode, auto-assign, full-screen geometry mode, shader binding — the canonical
 * set is `layoutSettingKeys` in the .cpp) are NOT part of that structural
 * definition and no longer live inside the layout `.json`. They live here, in a
 * single `layout-settings.json` sidecar keyed by layout UUID — the same
 * sibling-store pattern used by rules.json / quicklayouts.json.
 *
 * The split happens only at the file boundary. The in-memory Layout/Zone model
 * still carries every setting (so the editor, D-Bus wire format, and runtime
 * consumers are unchanged): @ref stripSettings is applied to Layout::toJson on
 * write, and @ref mergeSettings re-applies the stored values before
 * Layout::fromJson on read.
 */
class PHOSPHORZONES_EXPORT LayoutSettingsStore
{
public:
    /// On-disk schema version for layout-settings.json. Independent of the
    /// application config schema version.
    static constexpr int SchemaVersion = 1;

    // ── Pure split/merge helpers (no I/O) ──────────────────────────────────
    // These operate on the FULL layout JSON as produced by Layout::toJson and
    // consumed by Layout::fromJson. They are the single definition of "which
    // keys are settings vs structural".

    /// Pull the settings out of a full layout JSON. Returns a settings object
    /// (the per-layout setting keys plus a per-zone appearance map keyed by zone
    /// UUID). Returns an empty object when the layout carries no settings.
    static QJsonObject extractSettings(const QJsonObject& fullLayout);

    /// Return the structural-only layout JSON: the full layout minus every
    /// settings key (and minus each zone's appearance block). This is what gets
    /// written to the layout `.json`.
    static QJsonObject stripSettings(const QJsonObject& fullLayout);

    /// Overlay a settings object (as produced by @ref extractSettings) back onto
    /// a structural layout JSON, yielding a full layout JSON for Layout::fromJson.
    /// Keys already present in @p structural are preserved when @p settings does
    /// not override them — so a not-yet-split (full-format) layout file still
    /// round-trips correctly even with an empty store entry.
    static QJsonObject mergeSettings(QJsonObject structural, const QJsonObject& settings);

    // ── Sidecar persistence ────────────────────────────────────────────────

    /// Replace the in-memory map with the contents of @p path. A missing file
    /// is treated as an empty store (returns true). Returns false only on a
    /// parse error.
    bool loadFromFile(const QString& path);

    /// The on-disk document for the current in-memory map: stamped with
    /// SchemaVersion, layouts with empty settings omitted. @ref saveToFile
    /// writes exactly this. Exposed so a caller that must stage the sidecar
    /// alongside another file — committing both only once neither can fail —
    /// can produce the same bytes without going through saveToFile's
    /// open/write/commit in one step.
    QJsonObject toJson() const;

    /// Atomically write the in-memory map to @p path (stamped with
    /// SchemaVersion). Layouts with empty settings are omitted.
    bool saveToFile(const QString& path) const;

    /// Settings for a layout (empty object if none stored).
    QJsonObject settingsFor(const QString& layoutId) const;

    /// Store (or, when @p settings is empty, clear) the settings for a layout.
    void setSettingsFor(const QString& layoutId, const QJsonObject& settings);

    /// Drop a layout's settings entry (e.g. when the layout is deleted).
    void removeLayout(const QString& layoutId);

    bool isEmpty() const
    {
        return m_byLayout.isEmpty();
    }

private:
    QHash<QString, QJsonObject> m_byLayout;
};

} // namespace PhosphorZones
