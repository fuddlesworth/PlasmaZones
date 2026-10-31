// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorFsLoader/ContainedFileReader.h>

#include <QFile>
#include <QFileInfo>

#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace PhosphorFsLoader {

namespace {

QString errnoText(int err)
{
    return QString::fromLocal8Bit(std::strerror(err));
}

/// Closes the descriptor on every exit path, including the early refusals.
struct FdCloser
{
    int fd;
    ~FdCloser()
    {
        ::close(fd);
    }
};

} // namespace

std::optional<ContainedFile> readFileWithinDirectory(const QString& declaredPath, const QString& directory,
                                                     AbsolutePathPolicy policy, qint64 maxBytes, QString* error)
{
    const auto refuse = [error](const QString& message) -> std::optional<ContainedFile> {
        if (error) {
            *error = message;
        }
        return std::nullopt;
    };

    if (declaredPath.isEmpty()) {
        return refuse(QStringLiteral("Empty file path"));
    }
    if (declaredPath.size() > kMaxDeclaredPathLength) {
        return refuse(QStringLiteral("File path is longer than %1 characters").arg(kMaxDeclaredPathLength));
    }
    if (declaredPath.contains(QChar::Null)) {
        return refuse(QStringLiteral("File path contains a NUL character"));
    }
    if (directory.isEmpty()) {
        return refuse(QStringLiteral("No directory to resolve %1 against").arg(declaredPath));
    }
    if (maxBytes < 0) {
        return refuse(QStringLiteral("Negative size limit for %1").arg(declaredPath));
    }
    // The read loop asks for one byte past the limit to detect growth, which
    // must stay representable in a QByteArray.
    const qint64 limit = qMin<qint64>(maxBytes, QByteArray::maxSize() - 1);

    // The lexical and pre-open canonical check. Refuses `..` escapes, symlinks
    // that already point out, dangling links and cycles, all before anything
    // is opened, and yields the cleaned path that is both opened and returned.
    const auto resolved = resolveWithinDirectory(declaredPath, directory, policy);
    if (!resolved) {
        return refuse(QStringLiteral("%1 is outside %2").arg(declaredPath, directory));
    }
    const bool contained = !(QFileInfo(declaredPath).isAbsolute() && policy == AbsolutePathPolicy::Trust);

    // Canonical root taken BEFORE the open, so the descriptor is measured
    // against the directory as it was when the caller named it, not as a swap
    // racing the open might have left it.
    QString rootPrefix;
    if (contained) {
        const QString canonicalRoot = QFileInfo(directory).canonicalFilePath();
        if (canonicalRoot.isEmpty()) {
            return refuse(QStringLiteral("Cannot resolve directory %1").arg(directory));
        }
        rootPrefix = canonicalRoot.endsWith(QLatin1Char('/')) ? canonicalRoot : canonicalRoot + QLatin1Char('/');
    }

    // O_NONBLOCK: opening a FIFO for reading otherwise waits for a writer, and
    // some devices block in open() too. It has no effect on a regular file,
    // which is the only kind that survives the fstat() below.
    const QByteArray encoded = QFile::encodeName(*resolved);
    int fd = -1;
    do {
        fd = ::open(encoded.constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    } while (fd < 0 && errno == EINTR);
    if (fd < 0) {
        return refuse(QStringLiteral("Cannot open %1: %2").arg(declaredPath, errnoText(errno)));
    }
    const FdCloser closer{fd};

    struct stat st{};
    if (::fstat(fd, &st) != 0) {
        return refuse(QStringLiteral("Cannot stat %1: %2").arg(declaredPath, errnoText(errno)));
    }
    if (!S_ISREG(st.st_mode)) {
        return refuse(QStringLiteral("%1 is not a regular file").arg(declaredPath));
    }
    if (st.st_size < 0 || st.st_size > limit) {
        return refuse(QStringLiteral("%1 is larger than %2 bytes").arg(declaredPath).arg(limit));
    }

    if (contained) {
        // The descriptor's own path, not the declared one: whatever symlinks
        // were swapped in between the check above and the open, this names the
        // file actually being read. A deleted file reads back with a
        // " (deleted)" suffix that does not resolve, so it fails closed too.
        const QString opened = QFileInfo(QStringLiteral("/proc/self/fd/%1").arg(fd)).canonicalFilePath();
        if (opened.isEmpty() || !opened.startsWith(rootPrefix)) {
            return refuse(QStringLiteral("%1 is outside %2").arg(declaredPath, directory));
        }
    }

    QByteArray bytes;
    bytes.reserve(static_cast<qsizetype>(st.st_size) + 1);
    char chunk[16384];
    while (bytes.size() <= limit) {
        const qint64 wanted = qMin<qint64>(qint64(sizeof chunk), limit + 1 - bytes.size());
        const ssize_t count = ::read(fd, chunk, static_cast<size_t>(wanted));
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            return refuse(QStringLiteral("Cannot read %1: %2").arg(declaredPath, errnoText(errno)));
        }
        if (count == 0) {
            break;
        }
        bytes.append(chunk, static_cast<qsizetype>(count));
    }
    if (bytes.size() > limit) {
        return refuse(QStringLiteral("%1 is larger than %2 bytes").arg(declaredPath).arg(limit));
    }
    return ContainedFile{std::move(bytes), *resolved};
}

} // namespace PhosphorFsLoader
