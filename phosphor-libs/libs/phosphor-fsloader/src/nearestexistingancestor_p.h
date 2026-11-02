// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QString>

namespace PhosphorFsLoader::detail {

/// The nearest existing directory strictly ABOVE @p path: its parent when that
/// exists, otherwise the first existing directory further up. Returns the
/// cleaned path, or an empty string when nothing on the chain exists within the
/// climb bound.
///
/// Shared by `WatchedDirectorySet` (proxy watch for a registered directory that
/// does not exist yet) and `WatchedFileSet` (the directory watch that notices a
/// file being created, deleted or replaced by rename). Both need the same climb
/// and must not drift apart on it.
///
/// The climb is LEXICAL (`QFileInfo::absolutePath()`), never canonical, for two
/// reasons. `canonicalPath()` is empty for a path that does not exist, which is
/// the only case a caller asks about, and on an existing symlink it names the
/// directory of the link's TARGET rather than the directory holding the link,
/// which is where an editor's rename lands. A kernel watch on the lexical
/// directory follows any symlinked component on its own.
///
/// Bounded by @c kMaxClimb as defence in depth: every step strips one component
/// and `absolutePath()` of the root is the root, so the loop terminates anyway,
/// but 32 missing levels is no sane layout and is better refused than walked.
inline QString nearestExistingAncestor(const QString& path)
{
    constexpr int kMaxClimb = 32;
    QString ancestor = QFileInfo(QDir::cleanPath(path)).absolutePath();
    for (int climbed = 0; !ancestor.isEmpty() && !QFileInfo(ancestor).isDir(); ++climbed) {
        if (climbed >= kMaxClimb) {
            return {};
        }
        const QString next = QFileInfo(ancestor).absolutePath();
        if (next == ancestor) {
            return {}; // reached the root without finding a directory
        }
        ancestor = next;
    }
    return ancestor.isEmpty() ? QString() : QDir::cleanPath(ancestor);
}

} // namespace PhosphorFsLoader::detail
