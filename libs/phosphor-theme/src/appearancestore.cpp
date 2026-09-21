// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#include <PhosphorTheme/AppearanceStore.h>
#include <PhosphorTheme/ShellPalette.h>
#include <QDir>
#include <QCoreApplication>
#include <QColor>
#include <QPointer>
#include <QQmlEngine>
#include <QImageReader>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJSValue>
#include <QSaveFile>
#include <QLockFile>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <cmath>

namespace PhosphorTheme {
namespace {
bool validTrayPreferenceKey(const QString& key)
{
    if (key.isEmpty() || key.size() > 512)
        return false;
    for (const auto c : key.toUcs4()) {
        if (!QChar::isPrint(c))
            return false;
    }
    return true;
}
}
AppearanceStore* AppearanceStore::create(QQmlEngine*, QJSEngine*)
{
    static QPointer<AppearanceStore> instance;
    if (!instance)
        instance = new AppearanceStore(qApp);
    QQmlEngine::setObjectOwnership(instance, QQmlEngine::CppOwnership);
    return instance;
}

QVariantMap AppearanceStore::defaults()
{
    return {{QStringLiteral("presentation"), QStringLiteral("navigator")},
            {QStringLiteral("uiFont"), QString()},
            {QStringLiteral("monoFont"), QString()},
            {QStringLiteral("barLayout"),
             QVariantMap{{QStringLiteral("left"),
                          QVariantList{QVariantList{QStringLiteral("launcher")},
                                       QVariantList{QStringLiteral("focusedapp"), QStringLiteral("media")}}},
                         {QStringLiteral("center"),
                          QVariantList{QVariantList{QStringLiteral("placementmap"), QStringLiteral("workspaces")}}},
                         {QStringLiteral("right"),
                          QVariantList{QVariantList{QStringLiteral("tray"), QStringLiteral("systemmetrics"),
                                                    QStringLiteral("notification"), QStringLiteral("controlcenter"),
                                                    QStringLiteral("clock"), QStringLiteral("power")}}}}},
            {QStringLiteral("wallpapers"), QVariantMap{}},
            {QStringLiteral("wallpaperColors"),
             QVariantList{QStringLiteral("#919dcc"), QStringLiteral("#aca0d7"), QStringLiteral("#c0a5bd"),
                          QStringLiteral("#d4b8b5")}},
            {QStringLiteral("accentIndex"), 1},
            {QStringLiteral("textScale"), 100},
            {QStringLiteral("barInset"), 16},
            {QStringLiteral("surfaceEffect"), QStringLiteral("none")},
            {QStringLiteral("palette"), QStringLiteral("spectrum")},
            {QStringLiteral("material"), QStringLiteral("glass")},
            {QStringLiteral("edge"), QStringLiteral("top")},
            {QStringLiteral("density"), QStringLiteral("comfortable")},
            {QStringLiteral("radius"), 18},
            {QStringLiteral("gap"), 16},
            {QStringLiteral("glow"), true},
            {QStringLiteral("motion"), true},
            {QStringLiteral("surfacePacks"), false},
            {QStringLiteral("desktopStyle"), true},
            {QStringLiteral("media"), true},
            {QStringLiteral("notificationGrouping"), QStringLiteral("app")},
            {QStringLiteral("notificationPreviews"), true},
            {QStringLiteral("lockLayout"), QStringLiteral("split")},
            {QStringLiteral("lockMedia"), false},
            {QStringLiteral("lockNotifications"), true},
            {QStringLiteral("statsStyle"), QStringLiteral("traces")},
            {QStringLiteral("statsMetrics"),
             QVariantList{QStringLiteral("cpu"), QStringLiteral("gpu"), QStringLiteral("memory")}},
            {QStringLiteral("statsMemoryUnit"), QStringLiteral("percent")},
            {QStringLiteral("statsInterval"), 2},
            {QStringLiteral("statsGpuId"), QString()},
            {QStringLiteral("trayIcons"), QStringLiteral("symbolic")},
            {QStringLiteral("trayLimit"), 2},
            {QStringLiteral("trayAttention"), true},
            {QStringLiteral("trayOrder"), QVariantList{}},
            {QStringLiteral("trayVisibility"), QVariantMap{}},
            {QStringLiteral("visualizer"), QStringLiteral("ribbon")}};
}
AppearanceStore::AppearanceStore(QObject* parent)
    : AppearanceStore(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
                          + QStringLiteral("/phosphor-shell/appearance.json"),
                      parent)
{
}
AppearanceStore::AppearanceStore(const QString& path, QObject* parent)
    : QObject(parent)
    , m_path(path)
    , m_values(defaults())
    , m_previewLock(std::make_unique<QLockFile>(path + QStringLiteral(".preview.lock")))
{
    m_previewLock->setStaleLockTime(0);
    QFile file(path);
    if (!file.exists())
        return;
    if (!file.open(QIODevice::ReadOnly)) {
        fail(file.errorString());
        return;
    }
    const auto root = QJsonDocument::fromJson(file.read(65537)).object();
    QVariantMap loaded;
    if (root.value(QStringLiteral("version")).toInt() != 1 || !root.value(QStringLiteral("settings")).isObject()
        || !validate(root.value(QStringLiteral("settings")).toObject().toVariantMap(), loaded)) {
        fail(tr("Invalid appearance settings."));
        return;
    }
    m_values = loaded;
}
AppearanceStore::~AppearanceStore()
{
    if (m_previewLock->isLocked())
        QFile::remove(m_path + QStringLiteral(".preview"));
}
QVariantMap AppearanceStore::effectiveValues(const QString& path, bool* previewActive)
{
    if (previewActive)
        *previewActive = false;
    const QString lockPath = path + QStringLiteral(".preview.lock");
    if (QFileInfo::exists(lockPath)) {
        QLockFile lock(lockPath);
        lock.setStaleLockTime(0);
        if (!lock.tryLock() && lock.error() == QLockFile::LockFailedError) {
            AppearanceStore preview(path + QStringLiteral(".preview"));
            if (preview.error().isEmpty() && QFileInfo::exists(path + QStringLiteral(".preview"))) {
                if (previewActive)
                    *previewActive = true;
                return preview.values();
            }
        }
    }
    return AppearanceStore(path).values();
}
QVariantMap AppearanceStore::paletteFor(const QVariantMap& settings) const
{
    auto merged = m_values;
    for (auto it = settings.cbegin(); it != settings.cend(); ++it)
        merged[it.key()] = it.value();
    QVariantMap validated;
    return ShellPalette::fromSettings(validate(merged, validated) ? validated : m_values).toVariant();
}
bool AppearanceStore::validate(const QVariantMap& values, QVariantMap& result)
{
    result = defaults();
    const QMap<QString, QStringList> enums{
        {QStringLiteral("trayIcons"), {QStringLiteral("symbolic"), QStringLiteral("color")}},
        {QStringLiteral("statsStyle"), {QStringLiteral("traces"), QStringLiteral("meters"), QStringLiteral("numbers")}},
        {QStringLiteral("statsMemoryUnit"), {QStringLiteral("percent"), QStringLiteral("used")}},
        {QStringLiteral("surfaceEffect"), {QStringLiteral("none"), QStringLiteral("glass"), QStringLiteral("motes")}},
        {QStringLiteral("presentation"), {QStringLiteral("navigator"), QStringLiteral("stage")}},
        {QStringLiteral("palette"), {QStringLiteral("spectrum"), QStringLiteral("wallpaper"), QStringLiteral("ember")}},
        {QStringLiteral("material"), {QStringLiteral("glass"), QStringLiteral("solid"), QStringLiteral("light")}},
        {QStringLiteral("edge"), {QStringLiteral("top"), QStringLiteral("bottom")}},
        {QStringLiteral("density"), {QStringLiteral("comfortable"), QStringLiteral("compact")}},
        {QStringLiteral("notificationGrouping"), {QStringLiteral("app"), QStringLiteral("time")}},
        {QStringLiteral("lockLayout"), {QStringLiteral("split"), QStringLiteral("centered")}},
        {QStringLiteral("visualizer"),
         {QStringLiteral("ribbon"), QStringLiteral("bars"), QStringLiteral("halo"), QStringLiteral("off")}}};
    QSet<QString> trayKeys;
    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        if (!result.contains(it.key()))
            return false;
        if (enums.contains(it.key())) {
            if (it.value().metaType().id() != QMetaType::QString
                || !enums.value(it.key()).contains(it.value().toString()))
                return false;
        } else if (it.key() == QLatin1String("trayOrder")) {
            if (it.value().metaType().id() != QMetaType::QVariantList || it.value().toList().size() > 256)
                return false;
            QSet<QString> seen;
            for (const auto& entry : it.value().toList()) {
                const auto key = entry.toString();
                if (entry.metaType().id() != QMetaType::QString || !validTrayPreferenceKey(key) || seen.contains(key))
                    return false;
                seen.insert(key);
                trayKeys.insert(key);
            }
        } else if (it.key() == QLatin1String("trayVisibility")) {
            if (it.value().metaType().id() != QMetaType::QVariantMap || it.value().toMap().size() > 256)
                return false;
            const auto policies = it.value().toMap();
            const QStringList allowed{QStringLiteral("pinned"), QStringLiteral("auto"), QStringLiteral("overflow"),
                                      QStringLiteral("hidden")};
            for (auto policy = policies.cbegin(); policy != policies.cend(); ++policy) {
                if (!validTrayPreferenceKey(policy.key()) || policy.value().metaType().id() != QMetaType::QString
                    || !allowed.contains(policy.value().toString()))
                    return false;
                trayKeys.insert(policy.key());
            }
        } else if (it.key() == QLatin1String("trayLimit")) {
            const auto type = it.value().metaType().id();
            if (type != QMetaType::Int && type != QMetaType::Double && type != QMetaType::LongLong)
                return false;
            const auto limit = it.value().toDouble();
            if (!std::isfinite(limit) || std::floor(limit) != limit || limit < 0 || limit > 4)
                return false;
        } else if (it.key() == QLatin1String("statsMetrics")) {
            if (it.value().metaType().id() != QMetaType::QVariantList)
                return false;
            const auto metrics = it.value().toList();
            if (metrics.isEmpty() || metrics.size() > 3)
                return false;
            const QStringList allowed{QStringLiteral("cpu"), QStringLiteral("gpu"), QStringLiteral("memory"),
                                      QStringLiteral("network"), QStringLiteral("storage")};
            QSet<QString> seen;
            for (const auto& metric : metrics) {
                if (metric.metaType().id() != QMetaType::QString || !allowed.contains(metric.toString())
                    || seen.contains(metric.toString()))
                    return false;
                seen.insert(metric.toString());
            }
        } else if (it.key() == QLatin1String("statsGpuId")) {
            if (it.value().metaType().id() != QMetaType::QString
                || !QRegularExpression(QStringLiteral("^[a-zA-Z0-9_.:-]{0,80}$"))
                        .match(it.value().toString())
                        .hasMatch())
                return false;
        } else if (it.key() == QLatin1String("statsInterval")) {
            const auto type = it.value().metaType().id();
            if (type != QMetaType::Int && type != QMetaType::Double && type != QMetaType::LongLong)
                return false;
            const auto interval = it.value().toDouble();
            if (interval != 1 && interval != 2 && interval != 5)
                return false;
        } else if (it.key() == QLatin1String("wallpapers")) {
            if (it.value().metaType().id() != QMetaType::QVariantMap || it.value().toMap().size() > 32)
                return false;
            const auto wallpapers = it.value().toMap();
            for (auto wall = wallpapers.cbegin(); wall != wallpapers.cend(); ++wall) {
                const auto entry = wall.value().toMap();
                if (wall.key().size() > 128
                    || entry.keys() != QStringList{QStringLiteral("fit"), QStringLiteral("path")}
                    || entry.value(QStringLiteral("path")).metaType().id() != QMetaType::QString
                    || entry.value(QStringLiteral("path")).toString().size() > 4096
                    || !QFileInfo(entry.value(QStringLiteral("path")).toString()).isAbsolute()
                    || !QStringList{QStringLiteral("fill"), QStringLiteral("fit"), QStringLiteral("stretch"),
                                    QStringLiteral("center")}
                            .contains(entry.value(QStringLiteral("fit")).toString()))
                    return false;
            }
        } else if (it.key() == QLatin1String("wallpaperColors")) {
            if (it.value().metaType().id() != QMetaType::QVariantList || it.value().toList().size() != 4)
                return false;
            for (const auto& color : it.value().toList()) {
                if (color.metaType().id() != QMetaType::QString || !QColor(color.toString()).isValid())
                    return false;
            }
        } else if (it.key() == QLatin1String("barLayout")) {
            if (it.value().metaType().id() != QMetaType::QVariantMap)
                return false;
            const auto layout = it.value().toMap();
            if (layout.keys() != QStringList{QStringLiteral("center"), QStringLiteral("left"), QStringLiteral("right")})
                return false;
            QSet<QString> seen;
            static const QRegularExpression idPattern(QStringLiteral("^[a-zA-Z0-9][a-zA-Z0-9._-]{0,79}$"));
            for (const auto& region : layout) {
                if (region.metaType().id() != QMetaType::QVariantList || region.toList().size() > 24)
                    return false;
                for (const auto& group : region.toList()) {
                    if (group.metaType().id() != QMetaType::QVariantList || group.toList().isEmpty())
                        return false;
                    for (const auto& id : group.toList()) {
                        if (id.metaType().id() != QMetaType::QString || !idPattern.match(id.toString()).hasMatch()
                            || seen.contains(id.toString()) || seen.size() >= 24)
                            return false;
                        seen.insert(id.toString());
                    }
                }
            }
        } else if (it.key() == QLatin1String("uiFont") || it.key() == QLatin1String("monoFont")) {
            if (it.value().metaType().id() != QMetaType::QString || it.value().toString().size() > 80)
                return false;
            for (const auto c : it.value().toString()) {
                if (!c.isPrint())
                    return false;
            }
        } else if (result.value(it.key()).metaType().id() == QMetaType::Bool) {
            if (it.value().metaType().id() != QMetaType::Bool)
                return false;
        } else {
            const auto type = it.value().metaType().id();
            if (type != QMetaType::Int && type != QMetaType::Double && type != QMetaType::LongLong)
                return false;
            const double number = it.value().toDouble();
            const int minimum = it.key() == QLatin1String("accentIndex") ? 0
                : it.key() == QLatin1String("textScale")                 ? 90
                : it.key() == QLatin1String("radius")                    ? 4
                                                                         : 6;
            const int maximum = it.key() == QLatin1String("accentIndex") ? 3
                : it.key() == QLatin1String("textScale")                 ? 115
                                                                         : 30;
            if (!std::isfinite(number) || std::floor(number) != number || number < minimum || number > maximum)
                return false;
        }
        result[it.key()] = it.value();
    }
    if (trayKeys.size() > 256)
        return false;
    // Keep every accepted setting document within the reader's size limit.
    return QJsonDocument(QJsonObject{{QStringLiteral("version"), 1},
                                     {QStringLiteral("settings"), QJsonObject::fromVariantMap(result)}})
               .toJson()
               .size()
        <= 65536;
}
bool AppearanceStore::fail(const QString& error)
{
    if (m_error != error) {
        m_error = error;
        Q_EMIT errorChanged();
    }
    return false;
}
bool AppearanceStore::write(const QVariantMap& values)
{
    return writeDocument(m_path, values);
}
bool AppearanceStore::writeDocument(const QString& path, const QVariantMap& values)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return fail(tr("Cannot create the appearance settings directory."));
    QSaveFile file(path);
    const auto bytes = QJsonDocument(QJsonObject{{QStringLiteral("version"), 1},
                                                 {QStringLiteral("settings"), QJsonObject::fromVariantMap(values)}})
                           .toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return fail(file.errorString());
    fail(QString());
    return true;
}
void AppearanceStore::publish(const QVariantMap& values)
{
    if (m_values == values)
        return;
    bool geometry = false;
    for (const auto& key : {QStringLiteral("presentation"), QStringLiteral("edge"), QStringLiteral("density"),
                            QStringLiteral("gap"), QStringLiteral("barInset")})
        geometry |= m_values.value(key) != values.value(key);
    m_values = values;
    Q_EMIT changed();
    if (geometry)
        Q_EMIT geometryChanged();
}
bool AppearanceStore::commit(const QVariantMap& values)
{
    QVariantMap validated;
    if (!validate(values, validated))
        return fail(tr("Invalid appearance settings."));
    if (!m_editing && !write(validated))
        return false;
    if (m_editing && !writeDocument(m_path + QStringLiteral(".preview"), validated))
        return false;
    fail(QString());
    publish(validated);
    return true;
}
bool AppearanceStore::beginPreview()
{
    if (m_editing)
        return true;
    if (!QDir().mkpath(QFileInfo(m_path).absolutePath()) || !m_previewLock->tryLock())
        return fail(tr("Cannot start a preview. Another Appearance window may be using these settings."));
    if (!writeDocument(m_path + QStringLiteral(".preview"), m_values)) {
        m_previewLock->unlock();
        return false;
    }
    m_saved = m_values;
    m_editing = true;
    Q_EMIT changed();
    return true;
}
bool AppearanceStore::applyPreview()
{
    if (!write(m_values))
        return false;
    m_saved = m_values;
    Q_EMIT changed();
    return true;
}
void AppearanceStore::revertPreview()
{
    if (!m_editing)
        return;
    commit(m_saved);
}
void AppearanceStore::endPreview()
{
    if (!m_editing)
        return;
    QFile::remove(m_path + QStringLiteral(".preview"));
    m_previewLock->unlock();
    publish(m_saved);
    m_editing = false;
    m_saved.clear();
    Q_EMIT changed();
}
bool AppearanceStore::setValues(const QVariantMap& values)
{
    return commit(values);
}
bool AppearanceStore::setWallpaper(const QString& path, const QString& screen, const QString& fit)
{
    const QFileInfo info(path);
    QImageReader reader(path);
    if (!info.isFile() || !info.isReadable() || !reader.canRead())
        return fail(tr("Choose a readable image."));
    auto wallpapers = m_values.value(QStringLiteral("wallpapers")).toMap();
    if (screen.isEmpty())
        wallpapers.clear();
    wallpapers[screen] = QVariantMap{{QStringLiteral("path"), info.absoluteFilePath()}, {QStringLiteral("fit"), fit}};
    return setValue(QStringLiteral("wallpapers"), wallpapers);
}
bool AppearanceStore::setValue(const QString& key, const QVariant& value)
{
    auto next = m_values;
    // QML passes arrays/objects through QVariant as QJSValue. Normalize at
    // this boundary so the same strict validation serves native and QML callers.
    next[key] = value.metaType().id() == qMetaTypeId<QJSValue>() ? value.value<QJSValue>().toVariant() : value;
    return commit(next);
}
bool AppearanceStore::moveWidget(const QString& id, const QString& region, int index)
{
    if (!region.isEmpty() && region != QLatin1String("left") && region != QLatin1String("center")
        && region != QLatin1String("right"))
        return fail(tr("Unknown bar region."));
    auto layout = m_values.value(QStringLiteral("barLayout")).toMap();
    for (auto it = layout.begin(); it != layout.end(); ++it) {
        QVariantList groups;
        for (const auto& entry : it.value().toList()) {
            auto group = entry.toList();
            group.removeAll(id);
            if (!group.isEmpty())
                groups.append(QVariant(group));
        }
        it.value() = groups;
    }
    if (!region.isEmpty()) {
        auto groups = layout.value(region).toList();
        // The index counts widgets, independent of the optional visual groups.
        int offset = 0;
        bool inserted = false;
        for (auto& entry : groups) {
            auto group = entry.toList();
            if (index >= 0 && index <= offset + group.size()) {
                group.insert(qBound(0, index - offset, int(group.size())), id);
                entry = group;
                inserted = true;
                break;
            }
            offset += group.size();
        }
        if (!inserted)
            groups.append(QVariant(QVariantList{id}));
        layout[region] = groups;
    }
    return setValue(QStringLiteral("barLayout"), layout);
}
bool AppearanceStore::resetBarLayout()
{
    return setValue(QStringLiteral("barLayout"), defaults().value(QStringLiteral("barLayout")));
}
QVariantMap AppearanceStore::presetValues(const QString& preset)
{
    auto next = defaults();
    if (preset == QLatin1String("paper")) {
        next[QStringLiteral("palette")] = QStringLiteral("wallpaper");
        next[QStringLiteral("material")] = QStringLiteral("light");
        next[QStringLiteral("radius")] = 24;
        next[QStringLiteral("gap")] = 22;
        next[QStringLiteral("glow")] = false;
    } else if (preset == QLatin1String("ember")) {
        next[QStringLiteral("palette")] = QStringLiteral("ember");
        next[QStringLiteral("material")] = QStringLiteral("solid");
        next[QStringLiteral("media")] = false;
        next[QStringLiteral("density")] = QStringLiteral("compact");
        next[QStringLiteral("radius")] = 8;
        next[QStringLiteral("gap")] = 10;
        next[QStringLiteral("edge")] = QStringLiteral("bottom");
        next[QStringLiteral("glow")] = false;
    } else if (preset != QLatin1String("phosphor"))
        return {};
    return next;
}
bool AppearanceStore::applyPreset(const QString& preset)
{
    auto next = presetValues(preset);
    if (next.isEmpty()) {
        return fail(tr("Unknown appearance preset."));
    }
    for (const auto& key : {QStringLiteral("wallpapers"),
                            QStringLiteral("wallpaperColors"),
                            QStringLiteral("presentation"),
                            QStringLiteral("barLayout"),
                            QStringLiteral("uiFont"),
                            QStringLiteral("monoFont"),
                            QStringLiteral("motion"),
                            QStringLiteral("visualizer"),
                            QStringLiteral("textScale"),
                            QStringLiteral("surfaceEffect"),
                            QStringLiteral("surfacePacks"),
                            QStringLiteral("desktopStyle"),
                            QStringLiteral("lockLayout"),
                            QStringLiteral("lockMedia"),
                            QStringLiteral("lockNotifications"),
                            QStringLiteral("notificationGrouping"),
                            QStringLiteral("notificationPreviews"),
                            QStringLiteral("statsStyle"),
                            QStringLiteral("statsMetrics"),
                            QStringLiteral("statsMemoryUnit"),
                            QStringLiteral("statsInterval"),
                            QStringLiteral("statsGpuId"),
                            QStringLiteral("trayIcons"),
                            QStringLiteral("trayLimit"),
                            QStringLiteral("trayAttention"),
                            QStringLiteral("trayOrder"),
                            QStringLiteral("trayVisibility")})
        next[key] = m_values.value(key);
    return commit(next);
}

QVariantMap AppearanceStore::palette() const
{
    return ShellPalette::fromSettings(m_values).toVariant();
}

QString AppearanceStore::currentPreset() const
{
    for (const auto& preset : {QStringLiteral("phosphor"), QStringLiteral("paper"), QStringLiteral("ember")}) {
        const auto expected = presetValues(preset);
        bool matches = true;
        for (const auto& key : {QStringLiteral("palette"), QStringLiteral("material"), QStringLiteral("density"),
                                QStringLiteral("radius"), QStringLiteral("gap"), QStringLiteral("glow"),
                                QStringLiteral("media"), QStringLiteral("edge")}) {
            if (m_values.value(key) != expected.value(key)) {
                matches = false;
                break;
            }
        }
        if (matches) {
            return preset;
        }
    }
    return QStringLiteral("custom");
}

bool AppearanceStore::importPreset(const QUrl& url)
{
    if (!url.isLocalFile())
        return fail(tr("Choose a local preset file."));
    QFile file(url.toLocalFile());
    if (!file.open(QIODevice::ReadOnly))
        return fail(file.errorString());
    if (file.size() > 65536)
        return fail(tr("The preset is too large."));
    const auto root = QJsonDocument::fromJson(file.readAll()).object();
    if (root.value(QStringLiteral("version")).toInt() != 1 || !root.value(QStringLiteral("settings")).isObject())
        return fail(tr("Invalid appearance preset."));
    return commit(root.value(QStringLiteral("settings")).toObject().toVariantMap());
}
bool AppearanceStore::exportPreset(const QUrl& url)
{
    if (!url.isLocalFile())
        return fail(tr("Choose a local preset file."));
    QSaveFile file(url.toLocalFile());
    const auto bytes = QJsonDocument(QJsonObject{{QStringLiteral("version"), 1},
                                                 {QStringLiteral("settings"), QJsonObject::fromVariantMap(m_values)}})
                           .toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return fail(file.errorString());
    fail(QString());
    return true;
}
}
