// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#include <PhosphorShellPicker/AppearanceLibrary.h>
#include <PhosphorShellPicker/WallpaperCandidates.h>
#include <PhosphorTheme/AppearanceStore.h>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QSet>
#include <QFileInfo>
#include <QFile>
#include <QFontDatabase>
#include <QImageReader>
#include <QPointer>
#include <QQmlEngine>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QThreadPool>
#include <algorithm>

namespace PhosphorShellPicker {
namespace {
const QString Path = QStringLiteral("path"), Colors = QStringLiteral("colors"), Name = QStringLiteral("name");
QString defaultDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + QStringLiteral("/phosphor-shell/appearance");
}
}
AppearanceLibrary::AppearanceLibrary(QObject* parent)
    : AppearanceLibrary(PhosphorTheme::AppearanceStore::create(nullptr, nullptr), defaultDirectory(), parent)
{
}
AppearanceLibrary::AppearanceLibrary(PhosphorTheme::AppearanceStore* store, const QString& directory, QObject* parent)
    : QObject(parent)
    , m_store(store)
    , m_directory(directory)
{
    loadPresets();
    connect(m_store, &PhosphorTheme::AppearanceStore::changed, this, [this] {
        if (!m_store->editing())
            cancel();
    });
}
AppearanceLibrary* AppearanceLibrary::create(QQmlEngine*, QJSEngine*)
{
    static QPointer<AppearanceLibrary> instance;
    if (!instance)
        instance = new AppearanceLibrary(qApp);
    QQmlEngine::setObjectOwnership(instance, QQmlEngine::CppOwnership);
    return instance;
}
QStringList AppearanceLibrary::fonts() const
{
    return QFontDatabase::families();
}
bool AppearanceLibrary::fail(const QString& error)
{
    if (m_error != error) {
        m_error = error;
        Q_EMIT errorChanged();
    }
    return false;
}
void AppearanceLibrary::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    Q_EMIT busyChanged();
}
void AppearanceLibrary::cancel()
{
    ++m_generation;
    setBusy(false);
}
void AppearanceLibrary::rescan()
{
    QVariantList next;
    const QString bundled = m_directory + QStringLiteral("/collection");
    if (!QDir().mkpath(bundled)) {
        fail(tr("Cannot create the wallpaper library."));
        return;
    }
    struct Artwork
    {
        const char* id;
        const char* name;
        const char* description;
        const char* colors;
        bool nature;
    };
    const QList<Artwork> art{{"spectrum", QT_TR_NOOP("Soft orbit"), QT_TR_NOOP("A quiet horizon, a little light."),
                              "#41d4e8,#6e9cfd,#b68aee,#f390b3", false},
                             {"alpine", QT_TR_NOOP("Violet horizon"), QT_TR_NOOP("The last light of the evening."),
                              "#919dcc,#aca0d7,#c0a5bd,#d4b8b5", false},
                             {"iris", QT_TR_NOOP("After hours"), QT_TR_NOOP("Iris and peach, after the sun."),
                              "#9cafdc,#ada0df,#d998bb,#ebbc9c", false},
                             {"dunes", QT_TR_NOOP("Sundown"), QT_TR_NOOP("Warm curves and long shadows."),
                              "#e8c988,#d4b08a,#d3906c,#d27b83", false},
                             {"tide", QT_TR_NOOP("Low tide"), QT_TR_NOOP("The space between sea and sky."),
                              "#79c7c4,#78a6c4,#9eadd1,#d4c0b1", true},
                             {"moss", QT_TR_NOOP("Understory"), QT_TR_NOOP("A softer kind of green."),
                              "#9bb897,#bcc394,#93b5af,#d2bc9b", true},
                             {"graphite", QT_TR_NOOP("Fold"), QT_TR_NOOP("Light on a folded surface."),
                              "#a6b8c4,#a9abc0,#bba9bf,#ccb8b1", false},
                             {"linen", QT_TR_NOOP("First light"), QT_TR_NOOP("A pale canvas for your day."),
                              "#96afa9,#9eaec4,#c2a6b0,#d4bb98", false}};
    QSet<QString> seen;
    for (const auto& entry : art) {
        const QString filename = QString::fromLatin1(entry.id)
            + (qstrcmp(entry.id, "alpine") == 0 ? QStringLiteral(".png") : QStringLiteral(".svg"));
        const QString path = bundled + u'/' + filename;
        if (!QFileInfo::exists(path)) {
            QFile source(QStringLiteral(":/phosphor/appearance/wallpapers/") + filename);
            QSaveFile target(path);
            if (!source.open(QIODevice::ReadOnly) || !target.open(QIODevice::WriteOnly))
                continue;
            const auto bytes = source.readAll();
            if (target.write(bytes) != bytes.size() || !target.commit())
                continue;
        }
        QVariantList colors;
        for (const auto& color : QString::fromLatin1(entry.colors).split(u','))
            colors.append(color);
        next.append(QVariantMap{
            {Path, path},
            {Name, tr(entry.name)},
            {QStringLiteral("description"), tr(entry.description)},
            {QStringLiteral("collection"), entry.nature ? QStringLiteral("Nature") : QStringLiteral("Abstract")},
            {QStringLiteral("size"), qstrcmp(entry.id, "alpine") == 0 ? tr("2880 × 1800") : tr("Scalable artwork")},
            {Colors, colors}});
        seen.insert(QFileInfo(path).canonicalFilePath());
    }
    WallpaperCandidates candidates;
    auto directories = WallpaperCandidates::defaultDirectories();
    directories.prepend(m_directory + QStringLiteral("/images"));
    candidates.setDirectories(directories);
    candidates.rescan();
    auto discovered = candidates.candidates();
    const auto assignments = m_store->values().value(QStringLiteral("wallpapers")).toMap();
    for (const auto& value : assignments) {
        const auto path = value.toMap().value(Path).toString();
        discovered.prepend(QVariantMap{{Path, path}, {Name, QFileInfo(path).completeBaseName()}});
    }
    for (const auto& value : discovered) {
        auto candidate = value.toMap();
        const QFileInfo file(candidate.value(Path).toString());
        if (!file.isFile() || seen.contains(file.canonicalFilePath()))
            continue;
        seen.insert(file.canonicalFilePath());
        // Managed filenames carry a content hash for collision-free imports;
        // keep the original filename as the visible library label.
        if (file.absolutePath() == QDir(m_directory).absoluteFilePath(QStringLiteral("images"))) {
            static const QRegularExpression prefix(QStringLiteral("^[0-9a-f]{12}-"));
            candidate[Name] = file.completeBaseName().remove(prefix);
        }
        candidate[QStringLiteral("collection")] = QStringLiteral("Added");
        candidate[QStringLiteral("description")] = tr("Your own view.");
        candidate[QStringLiteral("size")] = tr("Local image");
        const auto previous = wallpaper(file.absoluteFilePath());
        if (!previous.value(Colors).toList().isEmpty()) {
            candidate[Colors] = previous.value(Colors);
            candidate[QStringLiteral("size")] = previous.value(QStringLiteral("size"));
        }
        next.append(candidate);
    }
    // A fresh shell draws its bundled fallback without a configured path.
    // Give the Appearance workspace the same durable starting point before
    // beginning a transaction, while preserving migrated per-display choices.
    if (!next.isEmpty() && !m_store->editing()
        && m_store->values().value(QStringLiteral("wallpapers")).toMap().isEmpty()) {
        const auto first = next.first().toMap();
        auto initial = m_store->values();
        initial[QStringLiteral("wallpapers")] = QVariantMap{
            {QString(), QVariantMap{{Path, first.value(Path)}, {QStringLiteral("fit"), QStringLiteral("fill")}}}};
        initial[QStringLiteral("wallpaperColors")] = first.value(Colors);
        m_store->setValues(initial);
    }
    if (next != m_wallpapers) {
        m_wallpapers = next;
        Q_EMIT wallpapersChanged();
    }
}
QVariantMap AppearanceLibrary::wallpaper(const QString& path) const
{
    for (const auto& value : m_wallpapers) {
        if (value.toMap().value(Path).toString() == path)
            return value.toMap();
    }
    return {{Path, path},
            {Name, QFileInfo(path).completeBaseName()},
            {QStringLiteral("description"), tr("Your own view.")},
            {QStringLiteral("collection"), QStringLiteral("Added")}};
}
QVariantMap AppearanceLibrary::inspectImage(const QString& path)
{
    const QFileInfo file(path);
    if (!file.isFile() || !file.isReadable() || file.size() > 64 * 1024 * 1024)
        return {};
    QImageReader reader(path);
    const QSize size = reader.size();
    if (!reader.canRead() || !size.isValid() || qint64(size.width()) * size.height() > qint64(16384) * 16384)
        return {};
    reader.setAutoTransform(true);
    reader.setScaledSize(size.scaled(64, 64, Qt::KeepAspectRatio));
    const auto image = reader.read().convertToFormat(QImage::Format_ARGB32);
    if (image.isNull())
        return {};
    struct Bucket
    {
        int count = 0;
        qint64 r = 0, g = 0, b = 0;
    };
    QMap<int, Bucket> buckets;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const auto pixel = image.pixel(x, y);
            if (qAlpha(pixel) < 128)
                continue;
            auto& bucket = buckets[(qRed(pixel) / 32 << 6) | (qGreen(pixel) / 32 << 3) | qBlue(pixel) / 32];
            ++bucket.count;
            bucket.r += qRed(pixel);
            bucket.g += qGreen(pixel);
            bucket.b += qBlue(pixel);
        }
    }
    auto ranked = buckets.values();
    // Large shadows and black clothing should not displace the wallpaper's
    // characteristic colors. Keep neutrals as a fallback for monochrome art.
    const auto bucketColor = [](const Bucket& bucket) {
        return QColor(bucket.r / bucket.count, bucket.g / bucket.count, bucket.b / bucket.count);
    };
    const auto chromatic = [bucketColor](const Bucket& bucket) {
        const auto color = bucketColor(bucket);
        return color.hslSaturationF() >= .18 && color.lightnessF() >= .18 && color.lightnessF() <= .85;
    };
    if (std::any_of(ranked.cbegin(), ranked.cend(), chromatic)) {
        ranked.removeIf([chromatic](const Bucket& bucket) {
            return !chromatic(bucket);
        });
    }
    std::sort(ranked.begin(), ranked.end(), [bucketColor](const Bucket& a, const Bucket& b) {
        const auto score = [bucketColor](const Bucket& bucket) {
            const auto color = bucketColor(bucket);
            return bucket.count * (.05 + color.hslSaturationF()) * std::max(.05f, color.lightnessF());
        };
        return score(a) > score(b);
    });
    QList<QColor> chosen;
    for (const auto& bucket : ranked) {
        const QColor color = bucketColor(bucket);
        const bool distinct = std::none_of(chosen.cbegin(), chosen.cend(), [color](const QColor& other) {
            return std::abs(color.red() - other.red()) + std::abs(color.green() - other.green())
                + std::abs(color.blue() - other.blue())
                < 64;
        });
        if (distinct)
            chosen.append(color);
        if (chosen.size() == 4)
            break;
    }
    if (chosen.isEmpty())
        chosen.append(QColor(QStringLiteral("#888888")));
    while (chosen.size() < 4)
        chosen.append(chosen.first());
    QVariantList colors;
    for (const auto& color : chosen)
        colors.append(color.name());
    return {{Colors, colors}, {QStringLiteral("size"), QStringLiteral("%1 × %2").arg(size.width()).arg(size.height())}};
}
void AppearanceLibrary::chooseWallpaper(const QString& path, const QString& screen, const QString& fit)
{
    const auto generation = ++m_generation;
    fail(QString());
    setBusy(true);
    const auto known = wallpaper(path);
    QPointer<AppearanceLibrary> guard(this);
    QThreadPool::globalInstance()->start([guard, generation, path, screen, fit, known] {
        auto details = inspectImage(path);
        if (!known.value(Colors).toList().isEmpty() && !details.isEmpty())
            details[Colors] = known.value(Colors);
        QMetaObject::invokeMethod(
            qApp,
            [guard, generation, path, screen, fit, details] {
                if (!guard || generation != guard->m_generation)
                    return;
                guard->setBusy(false);
                if (details.isEmpty()) {
                    guard->fail(
                        tr("This image could not be opened. Choose an image up to 64 MB and 16384 × 16384 pixels."));
                    return;
                }
                auto next = guard->m_store->values();
                auto wallpapers = next.value(QStringLiteral("wallpapers")).toMap();
                if (screen.isEmpty())
                    wallpapers.clear();
                wallpapers[screen] = QVariantMap{{Path, path}, {QStringLiteral("fit"), fit}};
                next[QStringLiteral("wallpapers")] = wallpapers;
                next[QStringLiteral("wallpaperColors")] = details.value(Colors);
                if (!guard->m_store->setValues(next))
                    return;
                for (auto& value : guard->m_wallpapers) {
                    auto entry = value.toMap();
                    if (entry.value(Path).toString() == path) {
                        entry[Colors] = details.value(Colors);
                        if (entry.value(QStringLiteral("size")) != tr("Scalable artwork"))
                            entry[QStringLiteral("size")] = details.value(QStringLiteral("size"));
                        value = entry;
                    }
                }
                Q_EMIT guard->wallpapersChanged();
            },
            Qt::QueuedConnection);
    });
}
void AppearanceLibrary::importImages(const QList<QUrl>& urls)
{
    if (urls.isEmpty())
        return;
    if (urls.size() > 32) {
        fail(tr("Add up to 32 images at a time."));
        return;
    }
    const QString directory = m_directory + QStringLiteral("/images");
    if (!QDir().mkpath(directory)) {
        fail(tr("Cannot create the wallpaper library."));
        return;
    }
    const auto generation = ++m_generation;
    setBusy(true);
    fail(QString());
    QPointer<AppearanceLibrary> guard(this);
    QThreadPool::globalInstance()->start([guard, generation, directory, urls] {
        QStringList errors;
        for (const auto& url : urls) {
            const auto path = url.toLocalFile();
            if (!url.isLocalFile() || inspectImage(path).isEmpty()) {
                errors.append(tr("Could not read %1.").arg(url.fileName()));
                continue;
            }
            QFile source(path);
            if (!source.open(QIODevice::ReadOnly)) {
                errors.append(source.errorString());
                continue;
            }
            const auto bytes = source.read(64 * 1024 * 1024 + 1);
            if (bytes.size() > 64 * 1024 * 1024) {
                errors.append(tr("%1 is too large.").arg(url.fileName()));
                continue;
            }
            const auto hash =
                QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex().left(12));
            QSaveFile target(directory + u'/' + hash + u'-' + QFileInfo(path).fileName());
            if (!target.open(QIODevice::WriteOnly) || target.write(bytes) != bytes.size() || !target.commit())
                errors.append(tr("Could not add %1.").arg(url.fileName()));
        }
        QMetaObject::invokeMethod(
            qApp,
            [guard, generation, errors] {
                if (!guard || generation != guard->m_generation)
                    return;
                guard->setBusy(false);
                guard->rescan();
                guard->fail(errors.join(u'\n'));
            },
            Qt::QueuedConnection);
    });
}
}
