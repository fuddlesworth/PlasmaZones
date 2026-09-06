// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShell/WallpaperService.h>

#include <PhosphorShaders/IWallpaperProvider.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QPointer>
#include <QSaveFile>
#include <QStandardPaths>
#include <QThreadPool>

#include <algorithm>

Q_LOGGING_CATEGORY(lcWallpaperService, "phosphorshell.wallpaper")

namespace PhosphorShell {

namespace {

// The store's keys, in one place so the reader and the writer cannot
// drift apart. The file is `{"version": 1, "wallpapers": {"": path,
// "<output>": path}}`.
constexpr int kStoreVersion = 1;

QString versionKey()
{
    return QStringLiteral("version");
}

QString wallpapersKey()
{
    return QStringLiteral("wallpapers");
}

QString storeDirectoryName()
{
    return QStringLiteral("phosphor-shell");
}

QString storeFileName()
{
    return QStringLiteral("wallpaper.json");
}

// The absolute path of `path` when it is a readable image, else empty.
// Both writers (setPath, setPreview) and the seed go through this so a
// surface never gets handed a path it cannot draw.
QString readableImagePath(const QString& path)
{
    const QFileInfo info(path);
    if (!info.isFile() || !info.isReadable()) {
        qCWarning(lcWallpaperService) << "Not a readable file:" << path;
        return {};
    }
    QImageReader probe(info.absoluteFilePath());
    if (!probe.canRead()) {
        qCWarning(lcWallpaperService) << "Not an image:" << path;
        return {};
    }
    return info.absoluteFilePath();
}

// A screen's entry, else the all-screens entry, else empty.
QString resolve(const QHash<QString, QString>& map, const QString& screenName)
{
    const auto own = map.constFind(screenName);
    if (own != map.constEnd()) {
        return own.value();
    }
    return map.value(QString());
}

} // namespace

WallpaperService::WallpaperService(QObject* parent)
    : WallpaperService(PhosphorShaders::createWallpaperProvider(), parent)
{
}

WallpaperService::WallpaperService(std::unique_ptr<PhosphorShaders::IWallpaperProvider> seedProvider, QObject* parent)
    : QObject(parent)
    , m_seedProvider(std::move(seedProvider))
{
    load();
    if (m_configured.isEmpty()) {
        seedFromDesktop();
    }
    // The seed source has done its one job. Dropping it here is what
    // makes "never read the other desktop again" structural rather than
    // a convention.
    m_seedProvider.reset();
    const QString initial = path();
    if (!initial.isEmpty()) {
        scheduleLoad(initial);
    }
}

WallpaperService::~WallpaperService() = default;

QString WallpaperService::storePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QLatin1Char('/')
        + storeDirectoryName() + QLatin1Char('/') + storeFileName();
}

QImage WallpaperService::image() const
{
    return m_image;
}

QString WallpaperService::path() const
{
    const QString shared = m_configured.value(QString());
    if (!shared.isEmpty() || m_configured.isEmpty()) {
        return shared;
    }
    // Only per-screen entries: pick deterministically, by name, so the
    // answer does not depend on hash order.
    QStringList names = m_configured.keys();
    names.removeAll(QString());
    std::sort(names.begin(), names.end());
    return names.isEmpty() ? QString() : m_configured.value(names.first());
}

bool WallpaperService::isAvailable() const
{
    return !m_image.isNull();
}

QString WallpaperService::configuredPath(const QString& screenName) const
{
    return resolve(m_configured, screenName);
}

QString WallpaperService::previewPath(const QString& screenName) const
{
    return resolve(m_preview, screenName);
}

QString WallpaperService::effectivePath(const QString& screenName) const
{
    const QString preview = previewPath(screenName);
    return preview.isEmpty() ? configuredPath(screenName) : preview;
}

bool WallpaperService::setPath(const QString& path, const QString& screenName)
{
    const QString absolute = readableImagePath(path);
    if (absolute.isEmpty()) {
        qCWarning(lcWallpaperService) << "Refusing to set wallpaper" << path;
        return false;
    }

    QHash<QString, QString> next = m_configured;
    if (screenName.isEmpty()) {
        // Every screen: the per-screen entries would otherwise keep
        // overriding what the user just asked every screen to show.
        next.clear();
        next.insert(QString(), absolute);
    } else {
        next.insert(screenName, absolute);
    }
    if (next == m_configured) {
        return true;
    }

    const QString previousPath = this->path();
    m_configured = next;
    save();

    const QString currentPath = this->path();
    if (currentPath != previousPath) {
        Q_EMIT pathChanged();
        scheduleLoad(currentPath);
    }
    Q_EMIT effectivePathChanged(screenName);
    return true;
}

bool WallpaperService::setPreview(const QString& path, const QString& screenName)
{
    const QString absolute = readableImagePath(path);
    if (absolute.isEmpty()) {
        qCWarning(lcWallpaperService) << "Refusing to preview wallpaper" << path;
        return false;
    }
    // The all-screens case is only a no-op when it is ALSO the only entry.
    // Otherwise setPreview(A, "") -> setPreview(B, "DP-1") -> setPreview(A, "")
    // returns early here and leaves DP-1 previewing B, contradicting the
    // replace-everything rule below.
    const bool alreadyShowing =
        m_preview.value(screenName) == absolute && (!screenName.isEmpty() || m_preview.size() == 1);
    if (alreadyShowing) {
        return true;
    }
    if (screenName.isEmpty()) {
        // Same rule as setPath: a preview for every screen replaces the
        // per-screen ones, or it would not be showing on every screen.
        m_preview.clear();
    }
    m_preview.insert(screenName, absolute);
    Q_EMIT effectivePathChanged(screenName);
    return true;
}

void WallpaperService::clearPreview(const QString& screenName)
{
    if (screenName.isEmpty()) {
        if (m_preview.isEmpty()) {
            return;
        }
        m_preview.clear();
    } else if (m_preview.remove(screenName) == 0) {
        return;
    }
    Q_EMIT effectivePathChanged(screenName);
}

void WallpaperService::refresh()
{
    const QString current = path();
    if (!current.isEmpty()) {
        scheduleLoad(current);
    }
}

void WallpaperService::load()
{
    m_configured.clear();
    QFile file(storePath());
    if (!file.exists()) {
        return;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(lcWallpaperService) << "Cannot read the wallpaper store" << file.fileName() << file.errorString();
        return;
    }
    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        qCWarning(lcWallpaperService) << "Ignoring the malformed wallpaper store" << file.fileName()
                                      << error.errorString();
        return;
    }
    const QJsonObject wallpapers = doc.object().value(wallpapersKey()).toObject();
    for (auto it = wallpapers.constBegin(); it != wallpapers.constEnd(); ++it) {
        const QString value = it.value().toString();
        if (!value.isEmpty()) {
            m_configured.insert(it.key(), value);
        }
    }
}

void WallpaperService::save() const
{
    const QString target = storePath();
    if (!QDir().mkpath(QFileInfo(target).absolutePath())) {
        qCWarning(lcWallpaperService) << "Cannot create the wallpaper store directory for" << target;
        return;
    }
    QJsonObject wallpapers;
    for (auto it = m_configured.constBegin(); it != m_configured.constEnd(); ++it) {
        wallpapers.insert(it.key(), it.value());
    }
    QJsonObject root;
    root.insert(versionKey(), kStoreVersion);
    root.insert(wallpapersKey(), wallpapers);

    // Atomic: a crash mid-write must not leave a half file that the next
    // start parses as an empty store and re-seeds over.
    QSaveFile file(target);
    if (!file.open(QIODevice::WriteOnly)) {
        qCWarning(lcWallpaperService) << "Cannot write the wallpaper store" << target << file.errorString();
        return;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        qCWarning(lcWallpaperService) << "Cannot commit the wallpaper store" << target << file.errorString();
    }
}

void WallpaperService::seedFromDesktop()
{
    if (!m_seedProvider) {
        return;
    }
    const QString found = m_seedProvider->wallpaperPath();
    if (found.isEmpty()) {
        qCDebug(lcWallpaperService) << "No wallpaper to seed the store from";
        return;
    }
    const QString absolute = readableImagePath(found);
    if (absolute.isEmpty()) {
        return;
    }
    qCDebug(lcWallpaperService) << "Seeding the wallpaper store from the previous desktop:" << absolute;
    m_configured.insert(QString(), absolute);
    save();
}

void WallpaperService::scheduleLoad(const QString& path)
{
    const quint64 generation = ++m_loadGeneration;
    QPointer<WallpaperService> self(this);
    // QThreadPool::globalInstance is process-wide; one-shot image
    // decodes per wallpaper change don't justify a dedicated pool.
    // QImageReader handles auto-rotation (EXIF) and incremental
    // decode of large images more efficiently than QImage's ctor.
    QThreadPool::globalInstance()->start([self, generation, path]() {
        QImageReader reader(path);
        reader.setAutoTransform(true);
        // Cap memory: a pathological wallpaper (multi-GB or a
        // decompression bomb) decoded full-resolution can OOM the
        // process. 512 MiB is generous for a 16K × 16K RGBA8888
        // wallpaper (1 GB allocation) but rejects the truly absurd.
        reader.setAllocationLimit(kMaxImageBytesMib);
        // Sanity-check pixel dimensions up front so a "valid" small
        // header pointing at a huge image still bails before decode.
        const QSize probedSize = reader.size();
        if (probedSize.isValid()) {
            const qint64 pixels = qint64(probedSize.width()) * qint64(probedSize.height());
            if (pixels > kMaxPixelCount) {
                qCWarning(lcWallpaperService)
                    << "Rejecting wallpaper at" << path << "— pixel count" << pixels << "exceeds" << kMaxPixelCount;
                return;
            }
        }
        QImage img = reader.read();
        if (!img.isNull()) {
            // Pre-convert to RGBA8888 — ShaderEffect's upload path
            // requires this format, and converting on the worker
            // thread keeps the cost off the GUI thread.
            img = img.convertToFormat(QImage::Format_RGBA8888);
        }
        // Marshal back to the GUI thread. Post to qApp (always alive
        // when there's a process) and re-check QPointer inside the
        // lambda — if `self` was destroyed between scheduling and
        // dispatch, the lambda bails before any dereference.
        QMetaObject::invokeMethod(
            qApp,
            [self, generation, img, path]() {
                if (!self || generation != self->m_loadGeneration) {
                    return;
                }
                self->installImage(img, path);
            },
            Qt::QueuedConnection);
    });
}

void WallpaperService::installImage(QImage image, const QString& path)
{
    if (image.isNull()) {
        qCWarning(lcWallpaperService) << "Failed to load wallpaper at" << path;
        return;
    }
    m_image = image;
    m_imagePath = path;
    qCDebug(lcWallpaperService) << "Loaded wallpaper" << path << image.size();
    Q_EMIT imageChanged();
}

} // namespace PhosphorShell
