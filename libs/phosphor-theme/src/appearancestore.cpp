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
#include <QSaveFile>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <cmath>

namespace PhosphorTheme {
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
                          QVariantList{QVariantList{QStringLiteral("clock"), QStringLiteral("notification"),
                                                    QStringLiteral("tray"), QStringLiteral("controlcenter"),
                                                    QStringLiteral("appearance"), QStringLiteral("power")}}}}},
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
{
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
bool AppearanceStore::validate(const QVariantMap& values, QVariantMap& result)
{
    result = defaults();
    const QMap<QString, QStringList> enums{
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
    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        if (!result.contains(it.key()))
            return false;
        if (enums.contains(it.key())) {
            if (it.value().metaType().id() != QMetaType::QString
                || !enums.value(it.key()).contains(it.value().toString()))
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
    return true;
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
    if (!QDir().mkpath(QFileInfo(m_path).absolutePath()))
        return fail(tr("Cannot create the appearance settings directory."));
    QSaveFile file(m_path);
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
    fail(QString());
    publish(validated);
    return true;
}
void AppearanceStore::beginPreview()
{
    if (m_editing)
        return;
    m_saved = m_values;
    m_editing = true;
    Q_EMIT changed();
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
    fail(QString());
    publish(m_saved);
}
void AppearanceStore::endPreview()
{
    revertPreview();
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
    next[key] = value;
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
    for (const auto& key :
         {QStringLiteral("wallpapers"), QStringLiteral("wallpaperColors"), QStringLiteral("presentation"),
          QStringLiteral("barLayout"), QStringLiteral("uiFont"), QStringLiteral("monoFont"), QStringLiteral("motion"),
          QStringLiteral("visualizer"), QStringLiteral("textScale"), QStringLiteral("surfaceEffect"),
          QStringLiteral("surfacePacks"), QStringLiteral("desktopStyle"), QStringLiteral("lockLayout"),
          QStringLiteral("lockMedia"), QStringLiteral("lockNotifications"), QStringLiteral("notificationGrouping"),
          QStringLiteral("notificationPreviews")})
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
