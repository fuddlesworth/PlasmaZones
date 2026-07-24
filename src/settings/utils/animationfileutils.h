// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

namespace PlasmaZones::animfileutil {

QString slugify(const QString& name);
QString jsonFilePath(const QString& dir, const QString& slug);

/// Ceiling on one hand-editable JSON file read (profiles, presets, sets).
/// These dirs are filesystem boundaries a user can hand-place anything at,
/// and several reads run on the GUI thread. Every reader derives its cap
/// from this single constant so a future bump cannot drift one boundary
/// away from the others.
inline constexpr qint64 kMaxJsonFileBytes = 4 * 1024 * 1024;

} // namespace PlasmaZones::animfileutil
