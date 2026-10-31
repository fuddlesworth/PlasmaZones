// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorFsLoader/PackPathGuard.h>
#include <PhosphorFsLoader/phosphorfsloader_export.h>

#include <QtCore/QByteArray>
#include <QtCore/QString>
#include <QtCore/QtGlobal>

#include <optional>

namespace PhosphorFsLoader {

/// Longest declared path `readFileWithinDirectory` accepts, in UTF-16 code
/// units. Matches Linux PATH_MAX; anything longer cannot be opened anyway and
/// is refused before it reaches the filesystem.
inline constexpr qsizetype kMaxDeclaredPathLength = 4096;

/// What `readFileWithinDirectory` hands back on success.
struct ContainedFile
{
    /// The whole file, never more than the caller's limit.
    QByteArray bytes;
    /// The cleaned absolute path that was opened, in the same lexical form
    /// `resolveWithinDirectory` returns, suitable as a watch or cache key.
    QString path;
};

/// Read @p declaredPath, resolved against @p directory, refusing anything that
/// escapes the directory or is not a regular file of at most @p maxBytes.
/// Returns nullopt on refusal and, when @p error is non-null, a human-readable
/// reason in it.
///
/// `resolveWithinDirectory` checks a path BEFORE it is opened, so a component
/// swapped for a symlink between that check and the open escapes it. This
/// closes the window by validating the descriptor that was actually opened:
///
///   * The declared path is refused when empty, longer than
///     @c kMaxDeclaredPathLength, or containing NUL. Containment is first
///     checked by `resolveWithinDirectory` under @p policy, which refuses a
///     lexical `..` escape before anything is opened.
///   * The file is opened `O_RDONLY | O_NONBLOCK | O_CLOEXEC`, so a FIFO or a
///     device swapped into place cannot block the calling thread in `open()`.
///   * `fstat()` on the descriptor requires a regular file within the limit.
///   * For a contained path (relative, or absolute under
///     `AbsolutePathPolicy::Reject`) the descriptor's real path, read back
///     through `/proc/self/fd`, must lie inside the canonical form of
///     @p directory taken before the open. That is the check a symlink swap
///     cannot get past. A symlink whose target stays inside the directory is
///     still allowed.
///   * At most @p maxBytes + 1 bytes are read, so a file that grew after the
///     `fstat()` is refused rather than truncated.
///
/// An absolute path under `AbsolutePathPolicy::Trust` skips containment only;
/// the descriptor checks and the size limit still apply.
///
/// @p directory itself is trusted: it is the caller's choice, and its canonical
/// form is what containment is measured against.
///
/// Linux only. Where `/proc/self/fd` cannot be resolved the containment check
/// fails closed and every contained read is refused.
[[nodiscard]] PHOSPHORFSLOADER_EXPORT std::optional<ContainedFile>
readFileWithinDirectory(const QString& declaredPath, const QString& directory, AbsolutePathPolicy policy,
                        qint64 maxBytes, QString* error = nullptr);

} // namespace PhosphorFsLoader
