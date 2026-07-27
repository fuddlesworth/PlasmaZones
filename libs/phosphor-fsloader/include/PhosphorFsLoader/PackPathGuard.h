// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QString>
#include <QtCore/QStringList>

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
///   * Canonicalisation returns empty for a path that does not exist YET. Simply
///     falling back to lexical there fails open, because a symlinked
///     intermediate component is invisible to a lexical compare — and a
///     not-yet-existing leaf is the live-reload case, not an edge case. So the
///     deepest existing ancestor is canonicalised and the missing tail
///     re-appended. The two domains are never mixed.
///
/// Subdirectories INSIDE @p directory stay legal, because containment is
/// checked on the resolved path rather than by refusing separators. A declared
/// name that resolves to the directory itself is refused: a directory is not a
/// file any consumer here can use.
///
/// Existence is deliberately NOT checked. Callers report a missing file with
/// their own diagnostics, and conflating "escaped" with "absent" would make
/// both harder to debug.
///
/// Returns the CLEANED resolved path, so two spellings of one file (`./x` and
/// `sub//x`) cannot reach watch keys, content signatures or path-keyed caches as
/// distinct strings.
[[nodiscard]] inline std::optional<QString> resolveWithinDirectory(const QString& declaredPath,
                                                                   const QString& directory, AbsolutePathPolicy policy)
{
    if (declaredPath.isEmpty() || directory.isEmpty()) {
        return std::nullopt;
    }

    const QDir dir(directory);
    const bool wasAbsolute = QFileInfo(declaredPath).isAbsolute();
    if (wasAbsolute && policy == AbsolutePathPolicy::Trust) {
        // Cleaned like every other accepted return, so a trusted override cannot
        // reach watch keys and path-keyed caches in two spellings of one file.
        return QDir::cleanPath(declaredPath);
    }
    // `absoluteFilePath`, not `filePath`: a RELATIVE @p directory would otherwise
    // yield a relative result, breaking both the documented return contract and
    // the cache-dedupe rationale.
    const QString resolved = wasAbsolute ? declaredPath : dir.absoluteFilePath(declaredPath);

    const QString lexicalRoot = QDir::cleanPath(dir.absolutePath());
    const QString canonicalRoot = QFileInfo(dir.absolutePath()).canonicalFilePath();

    // Canonicalise as much of the target as EXISTS, then re-append the rest.
    //
    // A plain `QFileInfo(resolved).canonicalFilePath()` returns empty for a path
    // whose leaf does not exist yet, and falling back to a purely lexical
    // compare there fails OPEN: `pack/link/future.frag`, where `link` is a
    // symlink out of the pack, is lexically inside and canonically outside, and
    // the leaf not existing is exactly the live-reload case (the watcher arms on
    // the path and the file materialises later). So walk up to the deepest
    // existing ancestor, canonicalise THAT, and rebuild — which resolves every
    // symlinked intermediate component even when the leaf is absent.
    //
    // The climb is PURE STRING WORK via `QFileInfo::absolutePath()`. It must not
    // use `QDir::cdUp()`: that is `cd("..")`, which FAILS on a parent that does
    // not exist, so it stops at the first missing component rather than at the
    // deepest existing ancestor. With two or more missing components below a
    // symlink (`pack/link/sub/future.frag`) the old loop bailed out to a lexical
    // path and handed it to a CANONICAL root comparison — the exact domain mixing
    // this function claims never to do, failing open on a real escape.
    //
    // Returns nullopt when NOTHING on the chain exists up to the filesystem root.
    // That case fails CLOSED rather than degrading to lexical, because a lexical
    // target compared against a canonical root is unsound in both directions.
    const auto canonicalise = [](const QString& path) -> std::optional<QString> {
        QString current = QDir::cleanPath(path);
        QStringList tail;
        // Bounded by the component count: each iteration strips exactly one, and
        // `absolutePath()` of the filesystem root is the root itself, which ends it.
        for (;;) {
            const QString canonical = QFileInfo(current).canonicalFilePath();
            if (!canonical.isEmpty()) {
                if (tail.isEmpty()) {
                    return canonical;
                }
                // Cleaned, so a deepest-existing-ancestor of "/" does not produce
                // a doubled separator.
                return QDir::cleanPath(canonical + QLatin1Char('/') + tail.join(QLatin1Char('/')));
            }
            const QFileInfo info(current);
            const QString parent = info.absolutePath();
            const QString name = info.fileName();
            if (name.isEmpty() || parent.isEmpty() || parent == current) {
                return std::nullopt;
            }
            tail.prepend(name);
            current = parent;
        }
    };

    // Never mix domains: compare canonical against canonical, and only fall back
    // to lexical-vs-lexical when the ROOT itself cannot be canonicalised.
    const bool useCanonical = !canonicalRoot.isEmpty();
    QString target;
    if (useCanonical) {
        const auto canonicalTarget = canonicalise(resolved);
        if (!canonicalTarget) {
            return std::nullopt;
        }
        target = *canonicalTarget;
    } else {
        target = QDir::cleanPath(resolved);
    }
    const QString root = useCanonical ? canonicalRoot : lexicalRoot;

    // `root + '/'` unless root already ends in one, so a directory of "/" does
    // not become "//" and refuse everything.
    const QString prefix = root.endsWith(QLatin1Char('/')) ? root : root + QLatin1Char('/');
    if (target == root || !target.startsWith(prefix)) {
        return std::nullopt;
    }
    // The CLEANED form, so `./x.frag` and `sub//x.frag` do not reach watch keys,
    // content signatures and path-keyed caches as distinct strings for one file.
    return QDir::cleanPath(resolved);
}

} // namespace PhosphorFsLoader
