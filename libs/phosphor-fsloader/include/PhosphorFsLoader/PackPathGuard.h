// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QString>

#include <optional>

namespace PhosphorFsLoader {

/// What to do with a declared path that is already absolute.
enum class AbsolutePathPolicy {
    /// Refuse it. Correct for anything a PACK FILE declares: a pack ships its
    /// own assets, so an absolute path can only be a mistake or an escape.
    Reject,
    /// Accept it unchecked. Correct only for a RUNTIME override the user chose
    /// through a file picker or D-Bus, where an absolute path outside any pack
    /// is the entire point.
    Trust,
};

/// Resolve @p declaredPath against @p directory, refusing anything that lands
/// outside it. Returns nullopt on refusal, the resolved absolute path on
/// acceptance.
///
/// This exists because there is exactly one correct way to write this check and
/// it is easy to write a weaker one. Three properties matter:
///
///   * `QDir::filePath` / `absoluteFilePath` return an ABSOLUTE argument
///     unchanged and never normalise `..`, so neither is a containment check.
///   * A lexical comparison (`QDir::cleanPath`) does not resolve symlinks, so a
///     link inside the directory pointing out of it passes.
///   * Canonicalisation returns empty for a path that does not exist yet, so a
///     canonical-vs-lexical comparison silently accepts everything. The two
///     domains must never be mixed: canonical is used only when BOTH sides
///     resolve, and otherwise both fall back to lexical.
///
/// Subdirectories INSIDE @p directory stay legal, because containment is
/// checked on the resolved path rather than by refusing separators. A declared
/// name that resolves to the directory itself is refused: a directory is not a
/// file any consumer here can use.
///
/// Existence is deliberately NOT checked. Callers report a missing file with
/// their own diagnostics, and conflating "escaped" with "absent" would make
/// both harder to debug.
[[nodiscard]] inline std::optional<QString> resolveWithinDirectory(const QString& declaredPath,
                                                                   const QString& directory, AbsolutePathPolicy policy)
{
    if (declaredPath.isEmpty() || directory.isEmpty()) {
        return std::nullopt;
    }

    const QDir dir(directory);
    const bool wasAbsolute = QFileInfo(declaredPath).isAbsolute();
    if (wasAbsolute && policy == AbsolutePathPolicy::Trust) {
        return declaredPath;
    }
    const QString resolved = wasAbsolute ? declaredPath : dir.filePath(declaredPath);

    const QString lexicalRoot = QDir::cleanPath(dir.absolutePath());
    const QString canonicalRoot = QFileInfo(dir.absolutePath()).canonicalFilePath();
    const QString canonicalTarget = QFileInfo(resolved).canonicalFilePath();

    // Prefer canonical (it catches a symlink escape), but only when BOTH sides
    // resolved. Mixing domains is the classic way this check fails open.
    const bool useCanonical = !canonicalTarget.isEmpty() && !canonicalRoot.isEmpty();
    const QString target = useCanonical ? canonicalTarget : QDir::cleanPath(resolved);
    const QString root = useCanonical ? canonicalRoot : lexicalRoot;

    if (target == root || !target.startsWith(root + QLatin1Char('/'))) {
        return std::nullopt;
    }
    return resolved;
}

} // namespace PhosphorFsLoader
