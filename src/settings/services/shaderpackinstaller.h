// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

namespace PlasmaZones {

/// Shared validate+copy helper for installing a shader pack into a user
/// directory. Used by both `AnimationsPageController::installShaderPack`
/// and `SnappingShadersPageController::installShaderPack` — the two
/// callsites were copy-paste duplicates including the symlink-rejection
/// safety logic. Centralising here keeps the security-sensitive bits
/// in one place.
///
/// A shader pack is a directory containing a `metadata.json` file plus
/// pack assets. The installer:
///   * Accepts both `file://...` URLs and bare paths.
///   * Cleans the path (strips trailing slashes / `.`/`..`).
///   * Rejects non-existent sources, regular-file sources, and
///     symlinked sources outright — drag-drop is untrusted input and a
///     symlinked source could smuggle arbitrary readable filesystem
///     content into the user shader dir under deceptive names.
///   * Rejects sources without a `metadata.json` (or a symlinked one).
///   * Refuses to overwrite an existing destination basename.
///   * Recursively copies the pack with per-entry symlink rejection.
///   * Rolls back (removes destination) on any copy failure.
namespace ShaderPackInstaller {

enum class Result {
    Success,
    InvalidSource, ///< empty / nonexistent / not-a-dir / symlink / no basename
    MissingMetadata, ///< no metadata.json, or metadata.json is a symlink
    InvalidUserDir, ///< user dir empty / mkpath failed
    DestinationExists, ///< pack already installed (refuse to overwrite)
    PackTooLarge, ///< source exceeds maximum total bytes or file count
    CopyFailed, ///< partial copy; the destination has been rolled back
};

/// Upper bound on a single shader pack — generous enough for any legitimate
/// pack while preventing a maliciously large drop (gigabyte-sized asset,
/// thousand-deep directory) from filling the user's data partition.
constexpr qint64 kMaxPackTotalBytes = 256LL * 1024 * 1024; // 256 MiB
constexpr int kMaxPackFileCount = 4096;

/// Install a shader pack from `sourceUrl` (path or `file://` URL) into
/// `userShaderDir` as a child directory matching the source basename.
/// Returns one of `Result` — callers translate into log messages with
/// their own controller-specific tag.
Result install(const QString& sourceUrl, const QString& userShaderDir);

/// Human-readable error message for `Result` — useful in log lines.
QString errorMessage(Result r);

} // namespace ShaderPackInstaller

} // namespace PlasmaZones
