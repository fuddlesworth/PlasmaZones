// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorServiceIconTheme/IconThemeResolver.h>

#include <climits>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QImageReader>
#include <QLoggingCategory>
#include <QMap>
#include <QMutex>
#include <QMutexLocker>
#include <QQueue>
#include <QSet>
#include <QStandardPaths>
#include <QStringList>
#include <QtEndian>

Q_LOGGING_CATEGORY(lcIconTheme, "phosphor.service.icontheme")

namespace PhosphorServiceIconTheme {

// DirectoryEntry / ThemeIndex live at namespace scope (not in an
// anonymous namespace) because Private caches a QHash<QString,
// ThemeIndex> as a member, anonymous-namespace types pulled into
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

// File extensions we accept for icon files, in priority order. PNG
// wins over SVG when both exist at the same theme directory (themed
// rasters are usually hand-tuned), SVG over XPM. Keep this single
// list; the themed walk, the fallback scan, and the IconThemePath
// flat probe all share it.
const QStringList& iconFileExtensions()
{
    static const QStringList kExts = {
        QStringLiteral(".png"),
        QStringLiteral(".svg"),
        QStringLiteral(".xpm"),
    };
    return kExts;
}

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
    // XDG Icon Theme Spec 0.13, "Icon Lookup", `DirectorySizeDistance`.
    // Returned distance is in scaled-pixel units; smaller is better.
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
    // Threshold, spec formula uses `(Size ± Threshold) * Scale`, NOT
    // `minSize` / `maxSize`. Earlier rev mixed the two and produced
    // nonsense distances for Threshold directories whose minSize /
    // maxSize defaulted to Size (giving the right magnitude by
    // accident on most themes, but the wrong picks when the theme
    // author tuned Threshold differently).
    if (size * scale < (d.size - d.threshold) * d.scale) {
        return (d.size - d.threshold) * d.scale - size * scale;
    }
    if (size * scale > (d.size + d.threshold) * d.scale) {
        return size * scale - (d.size + d.threshold) * d.scale;
    }
    return 0;
}

QStringList xdgIconSearchPath()
{
    // Per the spec: $HOME/.icons, $XDG_DATA_DIRS/icons, /usr/share/pixmaps.
    QStringList paths;
    const auto home = QDir::homePath();
    paths << home + QStringLiteral("/.icons");

    // Use Qt's GenericDataLocation list, that's $XDG_DATA_HOME +
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

// Per XDG Icon Theme Spec, an icon name is a simple identifier
// (typically `[A-Za-z0-9_-]`). The resolver appends `name + ext` to
// filesystem paths derived from theme roots, so a name containing
// path separators, parent-directory tokens, or NUL bytes would let
// an attacker who controls the icon name (a hostile SNI item or
// menu provider) probe arbitrary files via QFile::exists and
// potentially decode any image-shaped file inside the calling
// process. The same applies to `extraThemeDir` when present.
// Path-traversal rejection shared by `isUnsafeIconName` (icon name
// concatenated into `<root>/<theme>/<dir>/<name><ext>`) and
// `isUnsafeThemeName` (theme name concatenated into
// `<root>/<theme>/index.theme`). Reject path separators, parent-dir
// tokens, and NUL so any future hardening lands in one place.
bool containsPathTraversalChars(const QString& s)
{
    if (s.contains(QLatin1Char('/')))
        return true;
    if (s.contains(QLatin1Char('\\')))
        return true;
    if (s.contains(QLatin1String("..")))
        return true;
    if (s.contains(QChar(QChar::Null)))
        return true;
    return false;
}

bool isUnsafeIconName(const QString& name)
{
    return containsPathTraversalChars(name);
}

bool isUnsafeIconDir(const QString& dir)
{
    // IconThemePath values are full filesystem paths so they legitimately
    // contain `/`; only reject parent-dir traversal patterns and NUL.
    // The bare `..` value would let a hostile SNI peer probe the
    // calling process's cwd-parent via QFile::exists; reject explicitly
    // since the substring checks below miss the no-separator form.
    if (dir == QLatin1String(".."))
        return true;
    if (dir.contains(QLatin1String("/../")) || dir.startsWith(QLatin1String("../"))
        || dir.endsWith(QLatin1String("/..")))
        return true;
    if (dir.contains(QChar(QChar::Null)))
        return true;
    return false;
}

bool isUnsafeThemeName(const QString& name)
{
    return containsPathTraversalChars(name);
}

} // namespace

class IconThemeResolver::Private
{
public:
    Private() = default;

    mutable QMutex mutex;
    QString configuredTheme; ///< empty => autodetect
    QStringList searchPath; ///< from xdgIconSearchPath()
    // `themeCache` and `resolvedCache` are both memoisation state.
    // The public API surface in IconThemeResolver.h declares
    // `iconForName(...) const` (theme resolution does not mutate
    // user-visible state). To honour that contract without lying
    // about the cache being internal-only, both maps are `mutable`.
    // Access is always serialised through `mutex` above.
    mutable QHash<QString, ThemeIndex> themeCache; ///< parsed index.theme per theme name
    mutable QHash<QString, QImage> resolvedCache;
    // FIFO eviction order for `resolvedCache`. We push on insert,
    // pop oldest on overflow. Pure FIFO (not LRU on hit) is enough
    // for the tray workload, tray icons either stay hot or churn
    // entirely on theme switch, and we already clear both caches
    // wholesale in `setThemeName`. Anti-LRU (`erase(begin())`)
    // would re-evict whatever bucket QHash::begin happens to
    // point at, which is unrelated to access order and produces
    // pathological thrash under bucket-collision.
    mutable QQueue<QString> resolvedOrder;
    static constexpr int kCacheLimit = 256;

    [[nodiscard]] QString detectThemeName() const;
    [[nodiscard]] const ThemeIndex& parseThemeIndex(const QString& themeName) const;
    [[nodiscard]] QString findIconHelper(const QString& iconName, int size, int scale, const QString& themeName) const;
    [[nodiscard]] QString lookupIcon(const QString& iconName, int size, int scale, const QString& themeName,
                                     QSet<QString>* visited = nullptr) const;
    [[nodiscard]] QString lookupFallbackIcon(const QString& iconName) const;
    [[nodiscard]] QString themeIconPath(const QString& iconName, int size, int scale, const QString& themeName) const;
};

QString IconThemeResolver::Private::detectThemeName() const
{
    // Qt sets QIcon::themeName() from the platform integration,
    // works on Wayland too, sourced from xdg-portal / xsettings /
    // GSettings as the platform supports. Falls back to "hicolor"
    // which is always present.
    auto name = QIcon::themeName();
    if (name.isEmpty())
        name = QStringLiteral("hicolor");
    return name;
}

namespace {

/// Parse an index.theme file into (groupName → {key → value}) pairs.
/// Hand-rolled because QSettings::IniFormat treats `/` in group names
/// as nested-subgroup separators, which silently mangles XDG's spec-
/// mandated `[NN/context]` group names ("16x16/apps" becomes group
/// "16x16" subgroup "apps", and the directory descriptor's keys
/// disappear under a path the rest of the code doesn't expect). The
/// .desktop / .theme INI dialect is trivial: `# comment`, `[group]`,
/// `key=value`, `key[locale]=value` (we ignore locale variants).
QMap<QString, QMap<QString, QString>> parseIniFile(const QString& path)
{
    // Real-world index.theme files are typically under 10 KB; Adwaita's
    // is ~50 KB at the heaviest. A 1 MiB total cap and a 64 KiB per-line
    // cap rule out the pathological "hostile theme on a shared XDG_DATA
    // dir publishes a multi-GB index.theme" case without rejecting any
    // legitimate file.
    constexpr qint64 kMaxFileBytes = 1 << 20;
    constexpr qint64 kMaxLineBytes = 1 << 16;
    QMap<QString, QMap<QString, QString>> result;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return result;
    }
    if (f.size() > kMaxFileBytes) {
        qCWarning(lcIconTheme) << "index.theme exceeds size cap, skipping:" << path << "size=" << f.size();
        return result;
    }
    QString currentGroup;
    while (!f.atEnd()) {
        auto line = QString::fromUtf8(f.readLine(kMaxLineBytes)).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')) || line.startsWith(QLatin1Char(';'))) {
            continue;
        }
        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
            currentGroup = line.mid(1, line.size() - 2);
            continue;
        }
        if (currentGroup.isEmpty()) {
            continue;
        }
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0) {
            continue;
        }
        auto key = line.left(eq).trimmed();
        const auto value = line.mid(eq + 1).trimmed();
        // Drop `key[lang]` localisation suffixes, we only care about
        // the C locale for spec-defined keys (Size, Type, etc.).
        if (key.contains(QLatin1Char('['))) {
            continue;
        }
        result[currentGroup].insert(key, value);
    }
    return result;
}

} // namespace

const ThemeIndex& IconThemeResolver::Private::parseThemeIndex(const QString& themeName) const
{
    if (auto it = themeCache.constFind(themeName); it != themeCache.constEnd()) {
        return *it;
    }

    ThemeIndex idx;
    idx.name = themeName;

    // Walk every search path looking for <path>/<theme>/index.theme.
    // The first one wins; the rest are checked only for their
    // Directories sections in case the theme is split across roots
    // (KDE does this for "breeze", half is in /usr/share, half in
    // /usr/share/kf5 etc.).
    bool foundHeader = false;
    for (const auto& root : searchPath) {
        const QString indexPath = root + QLatin1Char('/') + themeName + QStringLiteral("/index.theme");
        if (!QFile::exists(indexPath))
            continue;

        const auto sections = parseIniFile(indexPath);
        const auto& iconThemeSection = sections.value(QStringLiteral("Icon Theme"));

        if (!foundHeader) {
            foundHeader = true;
            const auto inheritsRaw = iconThemeSection.value(QStringLiteral("Inherits"));
            for (const auto& parent : inheritsRaw.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
                idx.inherits.append(parent.trimmed());
            }
        }

        // XDG Icon Theme Spec mandates that only sections listed in
        // Directories= (or ScaledDirectories= for HiDPI variants) of
        // the [Icon Theme] header are candidate icon directories. Vendor
        // extensions like [KDE Icon Theme] and arbitrary metadata groups
        // are accepted as comments by the spec, not as lookup roots.
        // Restricting the section walk avoids wasted QFile::exists
        // syscalls on every name lookup against non-spec groups.
        QSet<QString> directoriesAllowed;
        const auto collectDirs = [&](const QString& key) {
            const auto raw = iconThemeSection.value(key);
            for (const auto& d : raw.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
                directoriesAllowed.insert(d.trimmed());
            }
        };
        collectDirs(QStringLiteral("Directories"));
        collectDirs(QStringLiteral("ScaledDirectories"));

        for (auto sectionIt = sections.constBegin(); sectionIt != sections.constEnd(); ++sectionIt) {
            const auto& group = sectionIt.key();
            if (group == QLatin1String("Icon Theme")) {
                continue;
            }
            if (!directoriesAllowed.contains(group)) {
                continue;
            }
            // index.theme contents are technically attacker-controlled
            // when any directory on the XDG_DATA_DIRS path is writable
            // by an untrusted user (rare but possible on shared boxes
            // or sandbox escapes). A `[../../../etc]` directory group
            // would let themeIconPath construct
            // `<root>/<themeName>/../../../etc/<iconName>.png` and
            // probe arbitrary files. Reject any DirectoryEntry whose
            // path contains traversal vectors. Empty group names are
            // also rejected: they would let themeIconPath probe the
            // bare theme root, bypassing the spec's `NN/context`
            // directory layout.
            if (group.isEmpty() || group.contains(QLatin1String("..")) || group.contains(QLatin1Char('\\'))
                || group.contains(QChar(QChar::Null)) || group.startsWith(QLatin1Char('/'))) {
                qCWarning(lcIconTheme) << "rejected suspicious directory in" << indexPath << ":" << group;
                continue;
            }
            const auto& kv = sectionIt.value();
            DirectoryEntry e;
            e.path = group;
            e.size = kv.value(QStringLiteral("Size")).toInt();
            e.scale = kv.value(QStringLiteral("Scale"), QStringLiteral("1")).toInt();
            e.context = kv.value(QStringLiteral("Context"));
            e.type = kv.value(QStringLiteral("Type"), QStringLiteral("Threshold"));
            e.minSize = kv.value(QStringLiteral("MinSize"), QString::number(e.size)).toInt();
            e.maxSize = kv.value(QStringLiteral("MaxSize"), QString::number(e.size)).toInt();
            e.threshold = kv.value(QStringLiteral("Threshold"), QStringLiteral("2")).toInt();
            idx.directories.append(e);
        }
    }

    // Every theme inherits hicolor at the end of its parent chain.
    if (!idx.inherits.contains(QStringLiteral("hicolor")) && themeName != QLatin1String("hicolor")) {
        idx.inherits.append(QStringLiteral("hicolor"));
    }

    return *themeCache.insert(themeName, idx);
}

QString IconThemeResolver::Private::themeIconPath(const QString& iconName, int size, int scale,
                                                  const QString& themeName) const
{
    const auto& idx = parseThemeIndex(themeName);

    // Try every directory in the theme. First pass: directories whose
    // size matches. Second pass: closest by size distance.
    const QStringList& exts = iconFileExtensions();

    // Per-item overrides (`extraThemeDir`) are handled as a flat probe
    // at the top of `iconForName`, not threaded through the themed
    // walk: SNI's IconThemePath typically points at a dir containing
    // raw `<iconName>.<ext>` files rather than a themed `NN/apps`
    // subtree.
    QStringList roots;
    roots.reserve(searchPath.size());
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

QString IconThemeResolver::Private::lookupIcon(const QString& iconName, int size, int scale, const QString& themeName,
                                               QSet<QString>* visited) const
{
    // Walk this theme first, then each parent. Each parent gets a
    // fresh recursion so the inheritance chain is followed depth-first
    // per the XDG spec algorithm. Cycle detection via `visited`:
    // a malformed theme (A Inherits=B; B Inherits=A) used to recurse
    // until stack overflow.
    QSet<QString> visitedLocal;
    QSet<QString>* v = visited ? visited : &visitedLocal;
    if (v->contains(themeName)) {
        return {};
    }
    v->insert(themeName);

    auto path = themeIconPath(iconName, size, scale, themeName);
    if (!path.isEmpty())
        return path;

    // Copy the inherits list before recursing. `parseThemeIndex` may
    // insert into `themeCache` during the recursive `lookupIcon` call
    // for each parent (via the inner `themeIconPath` -> `parseThemeIndex`
    // chain). A QHash insertion can rehash and invalidate every
    // reference and iterator into the table, including any reference
    // we hold to the value associated with `themeName`. Copying the
    // small QStringList up front decouples our iteration from any
    // subsequent rehashes.
    const QStringList inherits = parseThemeIndex(themeName).inherits;
    for (const auto& parent : inherits) {
        path = lookupIcon(iconName, size, scale, parent, v);
        if (!path.isEmpty())
            return path;
    }
    return {};
}

QString IconThemeResolver::Private::lookupFallbackIcon(const QString& iconName) const
{
    // Last resort: scan the search path for `<root>/<iconName>.{png,svg,xpm}`
    // directly. This is the "unthemed icons" path, /usr/share/pixmaps
    // historically dumps a flat tree of app icons there.
    const QStringList& exts = iconFileExtensions();
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
                                                   const QString& themeName) const
{
    const auto themed = lookupIcon(iconName, size, scale, themeName);
    if (!themed.isEmpty())
        return themed;
    return lookupFallbackIcon(iconName);
}

// ─── Public API ────────────────────────────────────────────────────────────

IconThemeResolver* IconThemeResolver::instance()
{
    // C++11 magic-statics guarantee thread-safe one-time initialization
    // without a hand-rolled mutex. The instance parents to
    // QCoreApplication on first call so it shares the application
    // lifetime; the raw `new` is intentional (no leak, see below).
    //
    // The object is NOT a function-local static of type
    // IconThemeResolver itself because Qt forbids destroying QObjects
    // after QCoreApplication has been torn down (the meta-object
    // system asserts), and a function-local static would destroy at
    // process exit, potentially after QCoreApplication's destructor.
    // Allocating with `new` + parenting to QCoreApplication delegates
    // teardown to Qt's QObject tree, which runs before atexit().
    static IconThemeResolver* s_instance = new IconThemeResolver(QCoreApplication::instance());
    return s_instance;
}

IconThemeResolver::IconThemeResolver(QObject* parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    d->searchPath = xdgIconSearchPath();
}

IconThemeResolver::~IconThemeResolver() = default;

void IconThemeResolver::setThemeName(const QString& themeName)
{
    if (!themeName.isEmpty() && isUnsafeThemeName(themeName)) {
        qCWarning(lcIconTheme) << "rejected unsafe theme name:" << themeName;
        return;
    }
    QMutexLocker locker(&d->mutex);
    if (d->configuredTheme == themeName)
        return;
    d->configuredTheme = themeName;
    d->themeCache.clear();
    d->resolvedCache.clear();
    d->resolvedOrder.clear();
    // Refresh the XDG search path on every theme switch. The
    // singleton's constructor reads env vars once and caches; if a
    // test or runtime caller mutates `XDG_DATA_HOME` / `XDG_DATA_DIRS`
    // and then switches themes, the new theme would resolve against
    // the stale paths without this refresh. Cheap (single Qt API
    // call) and the test suite explicitly depends on it.
    d->searchPath = xdgIconSearchPath();
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
    if (name.isEmpty() || size <= 0 || scale <= 0)
        return {};

    // Validate untrusted inputs at the boundary. `name` originates
    // from D-Bus peers (SNI items, dbusmenu providers); `extraThemeDir`
    // from the same peers via SNI's IconThemePath. Both are appended
    // verbatim to filesystem paths below.
    if (isUnsafeIconName(name) || isUnsafeIconDir(extraThemeDir)) {
        qCWarning(lcIconTheme) << "rejected unsafe icon lookup name=" << name << " extraThemeDir=" << extraThemeDir;
        return {};
    }

    // Phase 1: lookup the cache and resolve the on-disk path under
    // the lock. Path resolution mutates `themeCache` (via
    // `parseThemeIndex`) so it has to be serialised. We deliberately
    // drop the lock for phase 2 (`QImageReader::read`) below: image
    // decode can take milliseconds for a complex SVG and we don't
    // want `themeName()` / `setThemeName()` / concurrent
    // `iconForName()` calls to block on it. The cost is that two
    // threads racing on the same uncached key may both decode; only
    // the first to reach phase 3 inserts into the cache, and the
    // later thread returns the winner's image rather than its own
    // freshly-decoded one. Wasteful but correct; the returned value
    // is what subsequent readers will get on the next cache hit.
    QString cacheKey;
    QString path;
    int targetScale = 0;
    {
        QMutexLocker locker(&d->mutex);

        const QString theme = d->configuredTheme.isEmpty() ? d->detectThemeName() : d->configuredTheme;
        // Field separator: NUL is rejected by every validator
        // (isUnsafeIconName / isUnsafeIconDir / isUnsafeThemeName), so
        // it cannot appear inside any field. Using `|` allowed a
        // collision when extraThemeDir contained the delimiter; NUL
        // makes the key unambiguous regardless of input shape.
        const QChar sep = QChar(QChar::Null);
        cacheKey = theme + sep + name + sep + extraThemeDir + sep + QString::number(size) + QLatin1Char(':')
            + QString::number(scale);
        if (auto it = d->resolvedCache.constFind(cacheKey); it != d->resolvedCache.constEnd()) {
            return *it;
        }

        if (!extraThemeDir.isEmpty()) {
            // Try the item's IconThemePath first, as a flat directory
            // (most apps with custom dirs dump icons straight in there,
            // not in a themed subtree). Then fall back to the normal
            // themed lookup below.
            const QStringList& exts = iconFileExtensions();
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
        targetScale = size * scale;
    }

    // Phase 2: decode without holding the lock.
    QImage img;
    if (!path.isEmpty()) {
        QImageReader reader(path);
        if (path.endsWith(QLatin1String(".svg"), Qt::CaseInsensitive)) {
            // SVGs render at any size: request the exact preferred
            // size so we don't get a tiny default rasterisation.
            reader.setScaledSize(QSize(targetScale, targetScale));
        }
        img = reader.read();
        if (img.isNull()) {
            // A rotten install (corrupt PNG, unsupported SVG feature,
            // SVG renderer missing) silently produced "no image" in
            // prior revs. Surface the underlying error so a future
            // regression is debuggable without a gdb session. Stays
            // at qCDebug because per-icon decode failures are not
            // user-actionable on their own; aggregated they signal a
            // theme breakage.
            qCDebug(lcIconTheme) << "QImageReader failed for" << path << ":" << reader.errorString();
        }
    }

    // Phase 3: insert into the resolved cache. Re-check for a racing
    // insert from another thread before our own put so we don't
    // double-count toward the FIFO order. Skip caching null QImages
    // (lookup miss or decode failure) so a later filesystem update
    // (theme install, icon copied in, package upgrade) is visible on
    // the next lookup instead of waiting for setThemeName() to flush
    // the whole cache.
    if (img.isNull()) {
        return img;
    }
    {
        QMutexLocker locker(&d->mutex);
        if (auto it = d->resolvedCache.constFind(cacheKey); it != d->resolvedCache.constEnd()) {
            return *it;
        }
        while (d->resolvedCache.size() >= Private::kCacheLimit && !d->resolvedOrder.isEmpty()) {
            d->resolvedCache.remove(d->resolvedOrder.dequeue());
        }
        d->resolvedCache.insert(cacheKey, img);
        d->resolvedOrder.enqueue(cacheKey);
    }
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
    // Validate against adversarial input from another process:
    //   1. Width / height must be sane (4096 hard cap bounds allocation
    //      for adversarial pixmap blobs; a real icon is at most 512).
    //   2. Size check uses 64-bit arithmetic; signed `int * int * 4`
    //      with adversarial dims (e.g. 65536 × 65536) wraps to a small
    //      positive value, bypasses the bounds check, and the copy
    //      loop overruns.
    static constexpr int kMaxIconDim = 4096;
    if (pxSize.width() <= 0 || pxSize.height() <= 0 || pxSize.width() > kMaxIconDim || pxSize.height() > kMaxIconDim) {
        return {};
    }
    const qint64 expected = qint64(pxSize.width()) * qint64(pxSize.height()) * 4;
    if (bytes.size() < expected) {
        return {};
    }

    QImage img(pxSize.width(), pxSize.height(), QImage::Format_ARGB32);
    // Source bytes from QByteArray are NOT guaranteed 4-byte aligned;
    // use the unaligned-safe `qFromBigEndian<quint32>(const void*)`
    // overload to avoid SIGBUS on ARM / RISC-V. Iterate per row via
    // scanLine() so non-default row alignments (which QImage may
    // insert for some widths) don't corrupt the output.
    const char* src = bytes.constData();
    for (int y = 0; y < pxSize.height(); ++y) {
        auto* dstRow = reinterpret_cast<quint32*>(img.scanLine(y));
        for (int x = 0; x < pxSize.width(); ++x) {
            dstRow[x] = qFromBigEndian<quint32>(src + (qsizetype(y) * pxSize.width() + x) * 4);
        }
    }
    return img;
}

} // namespace PhosphorServiceIconTheme
