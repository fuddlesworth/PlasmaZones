// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShellPicker/WallpaperCandidates.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QTextStream>
#include <QVariantMap>

#include <utility>

namespace PhosphorShellPicker {

namespace {

// The raster formats Qt's image plugins decode on every Plasma install.
// SVG is left out: a vector wallpaper has no thumbnail-friendly size and
// Plasma renders it through its own path.
const QStringList& imageSuffixes()
{
    static const QStringList suffixes{
        QStringLiteral("png"),  QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("webp"),
        QStringLiteral("avif"), QStringLiteral("jxl"), QStringLiteral("bmp"),
    };
    return suffixes;
}

QString canonicalOrAbsolute(const QString& path)
{
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? info.absoluteFilePath() : canonical;
}

QVariantMap toMap(const Candidate& candidate)
{
    return {
        {QStringLiteral("path"), candidate.path},
        {QStringLiteral("name"), candidate.name},
        {QStringLiteral("source"), candidate.source},
    };
}

} // namespace

WallpaperCandidates::WallpaperCandidates(QObject* parent)
    : QObject(parent)
    , m_directories(defaultDirectories())
{
}

WallpaperCandidates::~WallpaperCandidates() = default;

QStringList WallpaperCandidates::directories() const
{
    return m_directories;
}

void WallpaperCandidates::setDirectories(const QStringList& directories)
{
    if (m_directories == directories) {
        return;
    }
    m_directories = directories;
    Q_EMIT directoriesChanged();
}

QString WallpaperCandidates::currentPath() const
{
    return m_currentPath;
}

void WallpaperCandidates::setCurrentPath(const QString& path)
{
    if (m_currentPath == path) {
        return;
    }
    m_currentPath = path;
    Q_EMIT currentPathChanged();
}

QVariantList WallpaperCandidates::candidates() const
{
    QVariantList list;
    list.reserve(m_candidates.size());
    for (const Candidate& candidate : m_candidates) {
        list.append(toMap(candidate));
    }
    return list;
}

int WallpaperCandidates::count() const
{
    return static_cast<int>(m_candidates.size());
}

QStringList WallpaperCandidates::defaultDirectories()
{
    // locateAll returns only directories that exist, user location first,
    // then each $XDG_DATA_DIRS entry in order.
    return QStandardPaths::locateAll(QStandardPaths::GenericDataLocation, QStringLiteral("wallpapers"),
                                     QStandardPaths::LocateDirectory);
}

bool WallpaperCandidates::isImageFile(const QString& fileName)
{
    const QString suffix = QFileInfo(fileName).suffix().toLower();
    return !suffix.isEmpty() && imageSuffixes().contains(suffix);
}

QString WallpaperCandidates::packageImage(const QString& packageDir)
{
    const QDir images(QDir(packageDir).filePath(QStringLiteral("contents/images")));
    if (!images.exists()) {
        return {};
    }
    const QFileInfoList entries = images.entryInfoList(QDir::Files | QDir::Readable, QDir::Name | QDir::IgnoreCase);
    static const QRegularExpression sizePattern(QStringLiteral("(\\d+)x(\\d+)"));
    QString best;
    qint64 bestArea = -1;
    for (const QFileInfo& entry : entries) {
        if (!isImageFile(entry.fileName())) {
            continue;
        }
        // Plasma packages name each rendition by its resolution
        // ("3840x2160.png"); a file without one still counts, at area 0,
        // so a package with a single unnamed image is not skipped.
        qint64 area = 0;
        const QRegularExpressionMatch match = sizePattern.match(entry.completeBaseName());
        if (match.hasMatch()) {
            area = match.captured(1).toLongLong() * match.captured(2).toLongLong();
        }
        if (area > bestArea) {
            bestArea = area;
            best = entry.absoluteFilePath();
        }
    }
    return best;
}

QString WallpaperCandidates::packageName(const QString& packageDir)
{
    const QDir dir(packageDir);
    QFile json(dir.filePath(QStringLiteral("metadata.json")));
    if (json.open(QIODevice::ReadOnly)) {
        const QJsonObject root = QJsonDocument::fromJson(json.readAll()).object();
        const QString name = root.value(QLatin1String("KPlugin")).toObject().value(QLatin1String("Name")).toString();
        if (!name.isEmpty()) {
            return name;
        }
    }
    QFile desktop(dir.filePath(QStringLiteral("metadata.desktop")));
    if (desktop.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&desktop);
        while (!in.atEnd()) {
            const QString line = in.readLine().trimmed();
            if (line.startsWith(QLatin1String("Name="))) {
                const QString name = line.mid(5).trimmed();
                if (!name.isEmpty()) {
                    return name;
                }
            }
        }
    }
    return dir.dirName();
}

QList<Candidate> WallpaperCandidates::scanDirectory(const QString& directory)
{
    QList<Candidate> result;
    const QDir dir(directory);
    if (!dir.exists()) {
        return result;
    }
    const QFileInfoList entries = dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable,
                                                    QDir::Name | QDir::IgnoreCase);
    QList<Candidate> packages;
    for (const QFileInfo& entry : entries) {
        if (entry.isDir()) {
            const QString image = packageImage(entry.absoluteFilePath());
            if (!image.isEmpty()) {
                packages.append({image, packageName(entry.absoluteFilePath()), QStringLiteral("package")});
            }
        } else if (isImageFile(entry.fileName())) {
            result.append({entry.absoluteFilePath(), entry.completeBaseName(), QStringLiteral("file")});
        }
    }
    result.append(packages);
    return result;
}

void WallpaperCandidates::rescan()
{
    QList<Candidate> scanned;
    QSet<QString> seen;
    const auto add = [&scanned, &seen](Candidate candidate) {
        const QString key = canonicalOrAbsolute(candidate.path);
        if (seen.contains(key)) {
            return;
        }
        seen.insert(key);
        scanned.append(std::move(candidate));
    };

    if (!m_currentPath.isEmpty() && QFileInfo::exists(m_currentPath)) {
        add({m_currentPath, QFileInfo(m_currentPath).completeBaseName(), QStringLiteral("current")});
    }
    for (const QString& directory : std::as_const(m_directories)) {
        const QList<Candidate> found = scanDirectory(directory);
        for (const Candidate& candidate : found) {
            add(candidate);
        }
    }

    if (scanned == m_candidates) {
        return;
    }
    m_candidates = std::move(scanned);
    Q_EMIT candidatesChanged();
}

} // namespace PhosphorShellPicker
