// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shaderpackinstaller.h"

#include "core/platform/logging.h"
#include "phosphor_i18n.h"

#include <QAtomicInteger>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUrl>

#include <array>

namespace PlasmaZones::ShaderPackInstaller {

namespace {

/// metadata.json filename constant — pack-validation gates check for
/// this file's presence and reject symlinked copies. Centralised so a
/// future schema rename happens in one place.
constexpr QLatin1String kMetadataFilename{"metadata.json"};

/// Sensitive directory prefixes that a shader-pack source must NEVER
/// canonicalise into. Resolving a symlinked source whose canonical
/// path lives under one of these would copy arbitrary readable system
/// files into the user shader dir — a privilege-disclosure footgun.
/// Listed minimally; the per-entry symlink rejection inside the
/// recursive copy is the second line of defence.
constexpr std::array<QLatin1String, 5> kSensitivePrefixes{QLatin1String{"/etc"}, QLatin1String{"/sys"},
                                                          QLatin1String{"/proc"}, QLatin1String{"/dev"},
                                                          QLatin1String{"/boot"}};

/// True iff @p canonicalPath lies under one of the sensitive system
/// prefixes (`/etc`, `/sys`, `/proc`, `/dev`, `/boot`). Exact-equality
/// to a prefix (e.g. `/etc` itself) also matches. The startsWith()
/// check uses a `/` separator so `/etcetera` doesn't trip an `/etc`
/// match.
bool isInSensitivePrefix(const QString& canonicalPath)
{
    for (const QLatin1String prefix : kSensitivePrefixes) {
        if (canonicalPath == prefix)
            return true;
        if (canonicalPath.startsWith(QString(prefix) + QLatin1Char('/')))
            return true;
    }
    return false;
}

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
/// without that, a dropped pack containing `metadata.json -> /etc/passwd`
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

    // Canonical-form validation: a drop from `/home/x/symlinked-parent/pack`
    // resolves to a different canonical path. Earlier passes rejected ANY
    // canonical/absolute mismatch, which false-positives on case-insensitive
    // filesystems (FAT/exFAT/HFS+ via FUSE) and on perfectly legitimate
    // symlinked HOME setups (e.g. `~/Downloads -> /mnt/data/Downloads`).
    //
    // The real threat is a source that canonicalises INTO a sensitive
    // system area (`/etc`, `/sys`, `/proc`, `/dev`, `/boot`) — accepting
    // such a drop would copy arbitrary readable system files into the
    // user shader dir. So we accept the canonical path as authoritative
    // and only reject when canonicalisation lands in a sensitive prefix.
    // The per-entry symlink rejection inside `copyDirRecursive` is the
    // second line of defence: even if a non-sensitive symlinked source
    // is accepted at the boundary, descendant symlinks pointing into
    // `/etc` etc. are still skipped during the copy walk.
    const QString sourceCanonical = sourceInfo.canonicalFilePath();
    if (sourceCanonical.isEmpty()) {
        // canonicalFilePath() returns empty when the path doesn't exist
        // — a hard fail, since we already passed sourceInfo.exists().
        // Treat the racing-delete case as InvalidSource.
        qCWarning(lcConfig) << "ShaderPackInstaller: rejecting source — empty canonical path (raced delete?). source="
                            << sourceInfo.absoluteFilePath();
        return Result::InvalidSource;
    }
    if (isInSensitivePrefix(sourceCanonical)) {
        qCWarning(lcConfig) << "ShaderPackInstaller: rejecting source — canonical path inside sensitive system prefix."
                               " canonical="
                            << sourceCanonical << "absolute=" << sourceInfo.absoluteFilePath();
        return Result::InvalidSource;
    }
    // From here on, work off the canonical path so subsequent checks
    // (basename, user-dir containment, recursive copy) operate on the
    // resolved location rather than the surface-form one. Without this,
    // a `/tmp/link -> /tmp/realpack` source would compute its basename as
    // "link" (and create `<userShaderDir>/link`), surprising the user.
    const QFileInfo canonicalInfo(sourceCanonical);
    const QString sourceBasename = canonicalInfo.fileName();
    if (sourceBasename.isEmpty())
        return Result::InvalidSource;

    // Validate metadata.json — without it the registry won't pick up
    // the pack, so accepting the drop would silently be a no-op.
    // Reject symlinked metadata so a malicious pack can't smuggle a
    // non-shader JSON file's content past validation.
    const QString metadataPath = sourceCanonical + QLatin1Char('/') + kMetadataFilename;
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
    const QString userDirAbs = QDir(userShaderDir).absolutePath();
    if (sourceCanonical == userDirAbs)
        return Result::InvalidSource;
    if (sourceCanonical.startsWith(userDirAbs + QLatin1Char('/')))
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

    // Two-phase install for TOCTOU safety across parallel install()
    // invocations on the same basename. Two callers racing into the same
    // <basename> dir would otherwise both pass the QFileInfo::exists()
    // check above and the second's copy walk would scribble into the
    // first's half-populated tree (QFile::copy + the pre-copy remove() on
    // existing dest paths means whoever wrote last wins per-file). By
    // copying into a unique `<basename>.installing-<pid>-<counter>` dir
    // first and atomically renaming at the end, the conflict surfaces
    // as a clean DestinationExists at rename time — symmetric with the
    // existing pre-copy QFileInfo::exists() refusal.
    //
    // The temp dir lives next to the final destination so the rename is
    // a same-filesystem mv (POSIX-atomic). A cross-FS rename would fall
    // back to copy+delete, defeating the atomicity.
    static QAtomicInteger<quint64> s_installCounter{0};
    const quint64 counterValue = s_installCounter.fetchAndAddRelaxed(1) + 1;
    const QString tempDir = userShaderDir + QLatin1Char('/') + sourceBasename
        + QStringLiteral(".installing-%1-%2").arg(QCoreApplication::applicationPid()).arg(counterValue);
    if (QFileInfo::exists(tempDir)) {
        // Stale temp dir from a prior crashed run — clean it up so
        // QFile::rename below doesn't fail on an existing target.
        // Best-effort: if removal fails we get CopyFailed downstream,
        // which surfaces the issue cleanly.
        QDir(tempDir).removeRecursively();
    }

    qint64 totalBytes = 0;
    int totalFiles = 0;
    const CopyOutcome outcome = copyDirRecursive(sourceCanonical, tempDir, totalBytes, totalFiles);
    if (outcome != CopyOutcome::Ok) {
        // Best-effort rollback of the staging dir. If removeRecursively
        // also fails the staging dir is left half-populated under its
        // unique `.installing-…` name; the next run won't collide.
        QDir(tempDir).removeRecursively();
        return outcome == CopyOutcome::TooLarge ? Result::PackTooLarge : Result::CopyFailed;
    }

    // Race re-check: another install() may have committed into our
    // destination basename while our copy was running. Fail with
    // DestinationExists rather than silently overwriting.
    if (QFileInfo::exists(destDir)) {
        QDir(tempDir).removeRecursively();
        return Result::DestinationExists;
    }
    if (!QDir().rename(tempDir, destDir)) {
        // Rename failed (e.g. a parallel install just won the race and
        // the dest now exists, or a cross-FS edge case). Roll back the
        // staging dir and surface DestinationExists when the target
        // showed up, CopyFailed otherwise.
        const bool destNowExists = QFileInfo::exists(destDir);
        QDir(tempDir).removeRecursively();
        return destNowExists ? Result::DestinationExists : Result::CopyFailed;
    }

    return Result::Success;
}

QString errorMessage(Result r)
{
    // User-facing strings — these reach the chrome toast via
    // toastRequested() when the install button surfaces the failure, so
    // route through PhosphorI18n::tr for the user's locale.
    switch (r) {
    case Result::Success:
        return PhosphorI18n::tr("Shader pack installed.");
    case Result::InvalidSource:
        return PhosphorI18n::tr("The dropped path is not a valid shader pack directory.");
    case Result::MissingMetadata:
        return PhosphorI18n::tr("The shader pack is missing metadata.json (or it is a symlink).");
    case Result::InvalidUserDir:
        return PhosphorI18n::tr("The user shader directory could not be created.");
    case Result::DestinationExists:
        return PhosphorI18n::tr("A shader pack with this name is already installed.");
    case Result::PackTooLarge:
        return PhosphorI18n::tr("The shader pack exceeds the maximum allowed size or file count.");
    case Result::CopyFailed:
        return PhosphorI18n::tr("Copying the shader pack failed. The partial install was rolled back.");
    }
    return PhosphorI18n::tr("Unknown shader pack installer error.");
}

} // namespace PlasmaZones::ShaderPackInstaller
