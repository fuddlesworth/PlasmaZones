// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shaderpackinstaller.h"

#include "../core/logging.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUrl>

namespace PlasmaZones::ShaderPackInstaller {

namespace {

/// Result of a copy walk that may surface a soft "pack too large" error
/// distinct from a hard "copy I/O failed" error so the caller can map to
/// the right user-facing Result.
enum class CopyOutcome {
    Ok,
    TooLarge,
    IoFailed
};

/// Recursive directory copy with symlink protection. Returns OK on success,
/// TooLarge if the source exceeds the byte / file caps, IoFailed on any
/// disk-level failure. Symlinks (file or dir) are explicitly skipped —
/// without that, a dropped pack containing `metadata.json -> /etc/shadow`
/// or `assets -> /etc` would silently follow the link during traversal
/// and smuggle arbitrary readable filesystem content into the user
/// shader dir under deceptive names. A shader pack contains regular
/// files only; anything exotic is suspect and refused.
///
/// `totalBytes` / `totalFiles` accumulate across recursive calls; the
/// caller seeds them at zero. We trip TooLarge BEFORE the actual copy
/// to abort early on adversarial drops, then again AFTER each file copy
/// to catch cases where a per-file growth pushes us over.
CopyOutcome copyDirRecursive(const QString& sourcePath, const QString& destPath, qint64& totalBytes, int& totalFiles)
{
    QDir sourceDir(sourcePath);
    if (!sourceDir.exists())
        return CopyOutcome::IoFailed;
    if (!QDir().mkpath(destPath))
        return CopyOutcome::IoFailed;

    const QFileInfoList entries =
        sourceDir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoSymLinks | QDir::NoDotAndDotDot);
    for (const QFileInfo& entry : entries) {
        // QDir::NoSymLinks already filters at the entryInfoList layer,
        // but recheck per-entry: filesystem races (entry replaced by a
        // symlink between enumeration and this iteration) and the
        // historical leniency of QDir::NoSymLinks across Qt versions
        // both argue for an explicit guard at the copy boundary.
        if (entry.isSymLink())
            continue;

        const QString destEntryPath = destPath + QLatin1Char('/') + entry.fileName();
        if (entry.isDir()) {
            const CopyOutcome sub = copyDirRecursive(entry.absoluteFilePath(), destEntryPath, totalBytes, totalFiles);
            if (sub != CopyOutcome::Ok)
                return sub;
        } else if (entry.isFile()) {
            const qint64 srcSizeAtEnumeration = entry.size();
            ++totalFiles;
            if (totalFiles > kMaxPackFileCount)
                return CopyOutcome::TooLarge;
            // QFile::copy refuses to overwrite — caller's collision
            // check already guarantees a clean destination, but
            // defend against a partial failed previous run leaving
            // stale files.
            if (QFile::exists(destEntryPath))
                QFile::remove(destEntryPath);
            if (!QFile::copy(entry.absoluteFilePath(), destEntryPath))
                return CopyOutcome::IoFailed;
            // TOCTOU: the entrySize captured at enumeration could
            // have been swapped under us between size-check and copy
            // — a malicious source could replace the file with a
            // larger one to evade kMaxPackTotalBytes. Re-check from
            // the destination after copy and roll back if the budget
            // is exceeded.
            const qint64 destSize = QFile(destEntryPath).size();
            const qint64 chargedSize = destSize > 0 ? destSize : srcSizeAtEnumeration;
            totalBytes += chargedSize;
            if (totalBytes > kMaxPackTotalBytes) {
                QFile::remove(destEntryPath);
                return CopyOutcome::TooLarge;
            }
        }
        // Devices, FIFOs, sockets, etc. fall through silently — same
        // intent as the symlink skip above.
    }
    return CopyOutcome::Ok;
}

} // namespace

Result install(const QString& sourceUrl, const QString& userShaderDir)
{
    if (sourceUrl.isEmpty())
        return Result::InvalidSource;

    // Accept both `file://...` URLs (drag-drop from a file manager)
    // and bare paths (programmatic callers).
    QString sourcePath = sourceUrl;
    if (sourcePath.startsWith(QLatin1String("file://")))
        sourcePath = QUrl(sourceUrl).toLocalFile();

    // Normalise trailing slashes and `.`/`..` components — without
    // this, a drop URL like `file:///path/to/pack/` produces an empty
    // fileName() below and the destDir collapses onto the user shader
    // dir itself, surfacing as a confusing "destination already exists"
    // rather than a clean parse failure.
    sourcePath = QDir::cleanPath(sourcePath);

    const QFileInfo sourceInfo(sourcePath);
    if (!sourceInfo.exists() || !sourceInfo.isDir() || sourceInfo.isSymLink())
        return Result::InvalidSource;
    // Detect ANCESTOR symlinks too — `sourceInfo.isSymLink()` only checks
    // the leaf, but a drop from `/home/x/symlinked-parent/pack` resolves
    // to a path whose canonical form differs from its absolute form, and
    // following the link smuggles in arbitrary content under the linked
    // parent's resolved location. Reject when canonical != absolute.
    //
    // KNOWN EDGE: case-insensitive filesystems (FAT/exFAT/HFS+ via FUSE)
    // can also cause canonical/absolute mismatch via case normalisation
    // — a user drop from a USB stick may trip this even though no
    // symlink exists. Log both forms so a maintainer can distinguish the
    // case-normalisation false positive from a real symlink rejection.
    if (sourceInfo.canonicalFilePath() != sourceInfo.absoluteFilePath()) {
        qCWarning(lcConfig) << "ShaderPackInstaller: rejecting source — canonical/absolute mismatch (symlinked"
                               " ancestor or case-insensitive filesystem). canonical="
                            << sourceInfo.canonicalFilePath() << "absolute=" << sourceInfo.absoluteFilePath();
        return Result::InvalidSource;
    }
    const QString sourceBasename = sourceInfo.fileName();
    if (sourceBasename.isEmpty())
        return Result::InvalidSource;

    // Validate metadata.json — without it the registry won't pick up
    // the pack, so accepting the drop would silently be a no-op.
    // Reject symlinked metadata so a malicious pack can't smuggle a
    // non-shader JSON file's content past validation.
    const QString metadataPath = sourceInfo.absoluteFilePath() + QLatin1String("/metadata.json");
    const QFileInfo metadataInfo(metadataPath);
    if (!metadataInfo.exists() || !metadataInfo.isFile() || metadataInfo.isSymLink())
        return Result::MissingMetadata;

    if (userShaderDir.isEmpty())
        return Result::InvalidUserDir;
    if (!QDir().mkpath(userShaderDir))
        return Result::InvalidUserDir;

    // Refuse a source that lives INSIDE the user shader directory — a
    // re-drop of an already-installed pack would otherwise trip
    // DestinationExists by accident, and a symlinked path back into the
    // user dir would copy onto self.
    //
    // Also refuse when the source IS the user shader directory itself
    // (a drag-drop of the user dir onto the install target). The
    // startsWith check below uses a trailing slash and would miss the
    // exact-equality case, leaving the recursive copy to walk into the
    // freshly-created destination subdir on enumerate.
    const QString sourceAbs = sourceInfo.absoluteFilePath();
    const QString userDirAbs = QDir(userShaderDir).absolutePath();
    if (sourceAbs == userDirAbs)
        return Result::InvalidSource;
    if (sourceAbs.startsWith(userDirAbs + QLatin1Char('/')))
        return Result::InvalidSource;

    // Belt-and-braces against a separator-bearing basename. QDir::clean
    // Path + QFileInfo::fileName strips path components from a cleaned
    // input, so today `sourceBasename` never contains a '/' or '\\'.
    // Defending in depth keeps the helper safe if a future refactor
    // moves the cleanPath step or accepts a pre-trusted basename.
    if (sourceBasename.contains(QLatin1Char('/')) || sourceBasename.contains(QLatin1Char('\\'))
        || sourceBasename == QStringLiteral("..") || sourceBasename == QStringLiteral(".")) {
        return Result::InvalidSource;
    }
    const QString destDir = userShaderDir + QLatin1Char('/') + sourceBasename;
    if (QFileInfo::exists(destDir))
        return Result::DestinationExists;

    qint64 totalBytes = 0;
    int totalFiles = 0;
    const CopyOutcome outcome = copyDirRecursive(sourceInfo.absoluteFilePath(), destDir, totalBytes, totalFiles);
    if (outcome != CopyOutcome::Ok) {
        // Best-effort rollback. If removeRecursively also fails the
        // destination is left half-populated — that's still better
        // than leaving the user without a clear error, and the next
        // install attempt will get DestinationExists.
        QDir(destDir).removeRecursively();
        return outcome == CopyOutcome::TooLarge ? Result::PackTooLarge : Result::CopyFailed;
    }

    return Result::Success;
}

QString errorMessage(Result r)
{
    switch (r) {
    case Result::Success:
        return QStringLiteral("success");
    case Result::InvalidSource:
        return QStringLiteral(
            "source is not a regular existing directory (rejected: empty path, missing, "
            "regular-file, symlinked, or path with no basename)");
    case Result::MissingMetadata:
        return QStringLiteral("source is missing a regular-file metadata.json (or metadata.json is a symlink)");
    case Result::InvalidUserDir:
        return QStringLiteral("user shader directory is empty or could not be created");
    case Result::DestinationExists:
        return QStringLiteral("destination basename already exists; refusing to overwrite");
    case Result::PackTooLarge:
        return QStringLiteral("pack exceeds maximum allowed total bytes or file count; destination rolled back");
    case Result::CopyFailed:
        return QStringLiteral("recursive copy failed mid-flight; destination rolled back");
    }
    return QStringLiteral("unknown ShaderPackInstaller error");
}

} // namespace PlasmaZones::ShaderPackInstaller
