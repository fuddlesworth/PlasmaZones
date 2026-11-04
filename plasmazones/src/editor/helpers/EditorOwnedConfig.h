// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QFlags>
#include <QString>
#include <QStringList>

namespace PhosphorConfig {
class IBackend;
}

namespace PlasmaZones {

/**
 * @brief The Editor.* settings as the layout editor reads and writes them.
 *
 * The editor runs in its own process and has no Settings object, so it reads
 * config.json through a fresh backend. Both directions go through the settings
 * schema rather than the raw groups, so the editor sees exactly what the
 * daemon and the settings app see for the same file: absent keys read as
 * their schema default, numbers are clamped and modifier masks checked by the
 * schema's own validators, and a default-equal value is written as absence.
 *
 * The one check the schema does not make is on the shortcuts, which it stores
 * as plain strings. read() rejects an empty or unparseable one and substitutes
 * its default, because the editor would otherwise bind a dead shortcut.
 */
struct EditorOwnedConfig
{
    /// One bit per setting, for writing only the settings the editor changed.
    enum class Key : quint16 {
        GridSnapping = 1 << 0,
        EdgeSnapping = 1 << 1,
        SnapIntervalX = 1 << 2,
        SnapIntervalY = 1 << 3,
        SnapOverrideModifier = 1 << 4,
        DuplicateShortcut = 1 << 5,
        SplitHorizontalShortcut = 1 << 6,
        SplitVerticalShortcut = 1 << 7,
        FillShortcut = 1 << 8,
        FillOnDropEnabled = 1 << 9,
        FillOnDropModifier = 1 << 10,
        BackgroundOpacity = 1 << 11,
    };
    Q_DECLARE_FLAGS(Keys, Key)
    /// Every setting, for a write that carries them all.
    static Keys allKeys();

    bool gridSnappingEnabled = false;
    bool edgeSnappingEnabled = false;
    qreal snapIntervalX = 0.0;
    qreal snapIntervalY = 0.0;
    int snapOverrideModifier = 0;
    QString duplicateShortcut;
    QString splitHorizontalShortcut;
    QString splitVerticalShortcut;
    QString fillShortcut;
    bool fillOnDropEnabled = false;
    int fillOnDropModifier = 0;
    qreal backgroundOpacity = 0.0;

    /// Every Editor.* setting as @p backend holds it. Shortcut keys whose
    /// stored value was rejected are appended to @p invalidShortcutKeys.
    static EditorOwnedConfig read(PhosphorConfig::IBackend& backend, QStringList* invalidShortcutKeys = nullptr);

    /// Write the settings named in @p keys into @p backend's in-memory
    /// document. Does not sync: the caller decides when the write lands.
    void write(PhosphorConfig::IBackend& backend, Keys keys) const;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(EditorOwnedConfig::Keys)

} // namespace PlasmaZones
