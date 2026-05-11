// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServices/IconThemeResolver.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QImageReader>
#include <QMutex>
#include <QMutexLocker>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>
#include <QtEndian>

namespace PhosphorServices {

// DirectoryEntry / ThemeIndex live at namespace scope (not in an
// anonymous namespace) because Private caches a QHash<QString,
// ThemeIndex> as a member — anonymous-namespace types pulled into
// the data members of a class with external linkage trip
// -Wsubobject-linkage. They're only used inside this TU regardless.
struct DirectoryEntry
{
    QString path; ///< relative to theme root, e.g. "32x32/apps"
    int size = 0;
    int scale = 1;
    int minSize = 0;
    int maxSize = 0;
    int threshold = 2;
    QString type = QStringLiteral("Threshold"); ///< "Fixed", "Scalable", "Threshold"
    QString context;
};

struct ThemeIndex
{
    QString name;
    QStringList inherits;
    QList<DirectoryEntry> directories;
};

namespace {

// XDG Icon Theme Spec, section "Icon Lookup":
//
//   Threshold:  fixed-size icons match within ± Threshold of Size.
//   Scaled:     accepts MinSize..MaxSize directly.
//   Fixed:      accepts Size exactly.
//
// "DirectoryMatchesSize" returns true if a directory should be
// considered a hit for the requested size at the requested scale.
// "DirectorySizeDistance" provides the tie-breaker score when none of
// the directories match exactly.

bool directoryMatchesSize(const DirectoryEntry& d, int size, int scale)
{
    if (d.scale != scale)
        return false;
    if (d.type == QLatin1String("Fixed")) {
        return d.size == size;
    }
    if (d.type == QLatin1String("Scalable")) {
        return size >= d.minSize && size <= d.maxSize;
    }
    // Default "Threshold"
    return (d.size - d.threshold) <= size && size <= (d.size + d.threshold);
}

int directorySizeDistance(const DirectoryEntry& d, int size, int scale)
{
    if (d.type == QLatin1String("Fixed")) {
        return qAbs(d.size * d.scale - size * scale);
    }
    if (d.type == QLatin1String("Scalable")) {
        if (size * scale < d.minSize * d.scale) {
            return d.minSize * d.scale - size * scale;
        }
        if (size * scale > d.maxSize * d.scale) {
            return size * scale - d.maxSize * d.scale;
        }
        return 0;
    }
    // Threshold
    if (size * scale < (d.size - d.threshold) * d.scale) {
        return d.minSize * d.scale - size * scale;
    }
    if (size * scale > (d.size + d.threshold) * d.scale) {
        return size * scale - d.maxSize * d.scale;
    }
    return 0;
}

QStringList xdgIconSearchPath()
{
    // Per the spec: $HOME/.icons, $XDG_DATA_DIRS/icons, /usr/share/pixmaps.
    QStringList paths;
    const auto home = QDir::homePath();
    paths << home + QStringLiteral("/.icons");

    // Use Qt's GenericDataLocation list — that's $XDG_DATA_HOME +
    // $XDG_DATA_DIRS, which covers /usr/local/share + /usr/share + flatpak +
    // snap + etc. We append "/icons" to each.
    const auto dataDirs = QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation);
    for (const auto& dir : dataDirs) {
        paths << dir + QStringLiteral("/icons");
    }
    paths << QStringLiteral("/usr/share/pixmaps");

    // Strip duplicates (Qt occasionally returns dupes when HOME is
    // under /usr/share for nixos-style setups).
    QStringList unique;
    for (const auto& p : paths) {
        if (!unique.contains(p))
            unique.append(p);
    }
    return unique;
}

} // namespace

class IconThemeResolver::Private
{
public:
    explicit Private(IconThemeResolver* q)
        : q(q)
    {
    }

    IconThemeResolver* q;

    mutable QMutex mutex;
    QString configuredTheme; ///< empty => autodetect
    QStringList searchPath; ///< from xdgIconSearchPath()
    QHash<QString, ThemeIndex> themeCache; ///< parsed index.theme per theme name

    // Resolved-icon cache. Tray icons are looked up many times per
    // frame (every paintEvent for some widget setups), so caching
    // the final QImage by (theme, name, size, scale, extraDir) is
    // worth the memory. Capped to avoid pathological tray growth.
    struct CacheKey
    {
        QString theme, name, extraDir;
        int size, scale;
        bool operator==(const CacheKey& other) const noexcept
        {
            return theme == other.theme && name == other.name && extraDir == other.extraDir && size == other.size
                && scale == other.scale;
        }
    };
    struct CacheKeyHash
    {
        std::size_t operator()(const CacheKey& k) const noexcept
        {
            return qHash(k.theme) ^ qHash(k.name) ^ qHash(k.extraDir) ^ k.size ^ (k.scale << 16);
        }
    };
    mutable QHash<QString, QImage> resolvedCache;
    static constexpr int kCacheLimit = 256;

    [[nodiscard]] QString detectThemeName() const;
    // Non-const because parseThemeIndex memoises into themeCache;
    // marking it const would force `mutable QHash` (ugly because the
    // mutex above already guards the same data).
    [[nodiscard]] const ThemeIndex& parseThemeIndex(const QString& themeName);
    [[nodiscard]] QString findIconHelper(const QString& iconName, int size, int scale, const QString& themeName);
    [[nodiscard]] QString lookupIcon(const QString& iconName, int size, int scale, const QString& themeName);
    [[nodiscard]] QString lookupFallbackIcon(const QString& iconName) const;
    [[nodiscard]] QString themeIconPath(const QString& iconName, int size, int scale, const QString& themeName,
                                        const QString& extraDir);
};

QString IconThemeResolver::Private::detectThemeName() const
{
    // Qt sets QIcon::themeName() from the platform integration —
    // works on Wayland too, sourced from xdg-portal / xsettings /
    // GSettings as the platform supports. Falls back to "hicolor"
    // which is always present.
    auto name = QIcon::themeName();
    if (name.isEmpty())
        name = QStringLiteral("hicolor");
    return name;
}

const ThemeIndex& IconThemeResolver::Private::parseThemeIndex(const QString& themeName)
{
    if (auto it = themeCache.constFind(themeName); it != themeCache.constEnd()) {
        return *it;
    }

    ThemeIndex idx;
    idx.name = themeName;

    // Walk every search path looking for <path>/<theme>/index.theme.
    // The first one wins; the rest are checked only for their
    // Directories sections in case the theme is split across roots
    // (KDE does this for "breeze" — half is in /usr/share, half in
    // /usr/share/kf5 etc.).
    bool foundHeader = false;
    for (const auto& root : searchPath) {
        const QString indexPath = root + QLatin1Char('/') + themeName + QStringLiteral("/index.theme");
        if (!QFile::exists(indexPath))
            continue;

        QSettings settings(indexPath, QSettings::IniFormat);
        if (!foundHeader) {
            foundHeader = true;
            settings.beginGroup(QStringLiteral("Icon Theme"));
            const auto inheritsRaw = settings.value(QStringLiteral("Inherits")).toString();
            for (const auto& parent : inheritsRaw.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
                idx.inherits.append(parent.trimmed());
            }
            settings.endGroup();
        }

        const auto groups = settings.childGroups();
        for (const auto& group : groups) {
            if (group == QLatin1String("Icon Theme"))
                continue;
            settings.beginGroup(group);
            DirectoryEntry e;
            e.path = group;
            e.size = settings.value(QStringLiteral("Size")).toInt();
            e.scale = settings.value(QStringLiteral("Scale"), 1).toInt();
            e.context = settings.value(QStringLiteral("Context")).toString();
            e.type = settings.value(QStringLiteral("Type"), QStringLiteral("Threshold")).toString();
            e.minSize = settings.value(QStringLiteral("MinSize"), e.size).toInt();
            e.maxSize = settings.value(QStringLiteral("MaxSize"), e.size).toInt();
            e.threshold = settings.value(QStringLiteral("Threshold"), 2).toInt();
            idx.directories.append(e);
            settings.endGroup();
        }
    }

    // Every theme inherits hicolor at the end of its parent chain.
    if (!idx.inherits.contains(QStringLiteral("hicolor")) && themeName != QLatin1String("hicolor")) {
        idx.inherits.append(QStringLiteral("hicolor"));
    }

    return *themeCache.insert(themeName, idx);
}

QString IconThemeResolver::Private::themeIconPath(const QString& iconName, int size, int scale,
                                                  const QString& themeName, const QString& extraDir)
{
    const auto& idx = parseThemeIndex(themeName);

    // Try every directory in the theme. First pass: directories whose
    // size matches. Second pass: closest by size distance.
    QStringList exts = {QStringLiteral(".png"), QStringLiteral(".svg"), QStringLiteral(".xpm")};

    // Prepend the per-item override dir if any. The override is
    // searched at the "theme root" level — the directory inside is
    // expected to follow the same NN/apps layout as a real theme.
    QStringList roots;
    if (!extraDir.isEmpty())
        roots.append(extraDir);
    for (const auto& r : searchPath) {
        roots.append(r + QLatin1Char('/') + themeName);
    }

    // Pass 1: exact size match per the directory descriptor.
    for (const auto& d : idx.directories) {
        if (!directoryMatchesSize(d, size, scale))
            continue;
        for (const auto& root : roots) {
            for (const auto& ext : exts) {
                const QString candidate = root + QLatin1Char('/') + d.path + QLatin1Char('/') + iconName + ext;
                if (QFile::exists(candidate))
                    return candidate;
            }
        }
    }

    // Pass 2: closest size.
    int bestDist = INT_MAX;
    QString bestPath;
    for (const auto& d : idx.directories) {
        const int dist = directorySizeDistance(d, size, scale);
        if (dist >= bestDist)
            continue;
        for (const auto& root : roots) {
            for (const auto& ext : exts) {
                const QString candidate = root + QLatin1Char('/') + d.path + QLatin1Char('/') + iconName + ext;
                if (QFile::exists(candidate)) {
                    bestDist = dist;
                    bestPath = candidate;
                    break;
                }
            }
            if (bestDist == dist)
                break;
        }
    }
    return bestPath;
}

QString IconThemeResolver::Private::lookupIcon(const QString& iconName, int size, int scale, const QString& themeName)
{
    // Walk this theme first, then each parent. Each parent gets a
    // fresh recursion so the inheritance chain is followed depth-first
    // — matches the spec's algorithm.
    auto path = themeIconPath(iconName, size, scale, themeName, QString());
    if (!path.isEmpty())
        return path;

    const auto& idx = parseThemeIndex(themeName);
    for (const auto& parent : idx.inherits) {
        path = lookupIcon(iconName, size, scale, parent);
        if (!path.isEmpty())
            return path;
    }
    return {};
}

QString IconThemeResolver::Private::lookupFallbackIcon(const QString& iconName) const
{
    // Last resort: scan the search path for `<root>/<iconName>.{png,svg,xpm}`
    // directly. This is the "unthemed icons" path — /usr/share/pixmaps
    // historically dumps a flat tree of app icons there.
    static const QStringList exts = {QStringLiteral(".png"), QStringLiteral(".svg"), QStringLiteral(".xpm")};
    for (const auto& root : searchPath) {
        for (const auto& ext : exts) {
            const QString candidate = root + QLatin1Char('/') + iconName + ext;
            if (QFile::exists(candidate))
                return candidate;
        }
    }
    return {};
}

QString IconThemeResolver::Private::findIconHelper(const QString& iconName, int size, int scale,
                                                   const QString& themeName)
{
    const auto themed = lookupIcon(iconName, size, scale, themeName);
    if (!themed.isEmpty())
        return themed;
    return lookupFallbackIcon(iconName);
}

// ─── Public API ────────────────────────────────────────────────────────────

IconThemeResolver* IconThemeResolver::instance()
{
    static IconThemeResolver* s_instance = nullptr;
    static QMutex s_initMutex;
    QMutexLocker locker(&s_initMutex);
    if (!s_instance) {
        s_instance = new IconThemeResolver(QCoreApplication::instance());
    }
    return s_instance;
}

IconThemeResolver::IconThemeResolver(QObject* parent)
    : QObject(parent)
    , d(new Private(this))
{
    d->searchPath = xdgIconSearchPath();
}

IconThemeResolver::~IconThemeResolver()
{
    delete d;
}

void IconThemeResolver::setThemeName(const QString& themeName)
{
    QMutexLocker locker(&d->mutex);
    if (d->configuredTheme == themeName)
        return;
    d->configuredTheme = themeName;
    d->themeCache.clear();
    d->resolvedCache.clear();
    locker.unlock();
    Q_EMIT themeChanged();
}

QString IconThemeResolver::themeName() const
{
    QMutexLocker locker(&d->mutex);
    return d->configuredTheme.isEmpty() ? d->detectThemeName() : d->configuredTheme;
}

QImage IconThemeResolver::iconForName(const QString& name, int size, int scale, const QString& extraThemeDir) const
{
    if (name.isEmpty() || size <= 0)
        return {};

    QMutexLocker locker(&d->mutex);

    const QString theme = d->configuredTheme.isEmpty() ? d->detectThemeName() : d->configuredTheme;
    const QString cacheKey = theme + QLatin1Char('|') + name + QLatin1Char('|') + extraThemeDir + QLatin1Char('|')
        + QString::number(size) + QLatin1Char(':') + QString::number(scale);
    if (d->resolvedCache.contains(cacheKey)) {
        return d->resolvedCache.value(cacheKey);
    }

    QString path;
    if (!extraThemeDir.isEmpty()) {
        // Try the item's IconThemePath first, as a flat directory
        // (most apps with custom dirs dump icons straight in there,
        // not in a themed subtree). Then fall back to the normal
        // themed lookup below.
        static const QStringList exts = {QStringLiteral(".png"), QStringLiteral(".svg"), QStringLiteral(".xpm")};
        for (const auto& ext : exts) {
            const auto candidate = extraThemeDir + QLatin1Char('/') + name + ext;
            if (QFile::exists(candidate)) {
                path = candidate;
                break;
            }
        }
    }
    if (path.isEmpty()) {
        path = d->findIconHelper(name, size, scale, theme);
    }

    QImage img;
    if (!path.isEmpty()) {
        QImageReader reader(path);
        if (path.endsWith(QLatin1String(".svg"), Qt::CaseInsensitive)) {
            // SVGs render at any size — request the exact preferred
            // size so we don't get a tiny default rasterisation.
            reader.setScaledSize(QSize(size * scale, size * scale));
        }
        img = reader.read();
    }

    // Cap the cache. Eviction policy is "drop a random entry" — the
    // tray rarely has > 32 items × < 4 sizes, so we hit the cap only
    // during pathological churn (icon-theme switches mid-flight). The
    // simple eviction keeps cache pressure deterministic without
    // building an LRU.
    if (d->resolvedCache.size() >= Private::kCacheLimit) {
        d->resolvedCache.erase(d->resolvedCache.begin());
    }
    d->resolvedCache.insert(cacheKey, img);
    return img;
}

QImage IconThemeResolver::decodePixmaps(const QList<QPair<QSize, QByteArray>>& pixmaps, int size)
{
    if (pixmaps.isEmpty())
        return {};

    // Pick the one closest to size from above.
    int bestIdx = 0;
    int bestScore = -1;
    for (int i = 0; i < pixmaps.size(); ++i) {
        const auto dim = qMax(pixmaps[i].first.width(), pixmaps[i].first.height());
        int score;
        if (dim >= size) {
            score = dim - size;
        } else {
            score = (size - dim) + 1000;
        }
        if (bestScore < 0 || score < bestScore) {
            bestScore = score;
            bestIdx = i;
        }
    }

    const auto& [pxSize, bytes] = pixmaps.at(bestIdx);
    if (bytes.size() < pxSize.width() * pxSize.height() * 4)
        return {};

    QImage img(pxSize.width(), pxSize.height(), QImage::Format_ARGB32);
    const auto* src = reinterpret_cast<const quint32*>(bytes.constData());
    auto* dst = reinterpret_cast<quint32*>(img.bits());
    for (int i = 0; i < pxSize.width() * pxSize.height(); ++i) {
        dst[i] = qFromBigEndian(src[i]);
    }
    return img;
}

} // namespace PhosphorServices
