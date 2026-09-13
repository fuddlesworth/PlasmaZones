// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#include <PhosphorTheme/AppearanceStore.h>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <cmath>

namespace PhosphorTheme {
QVariantMap AppearanceStore::defaults()
{
    return {{QStringLiteral("palette"), QStringLiteral("spectrum")},
            {QStringLiteral("material"), QStringLiteral("glass")},
            {QStringLiteral("edge"), QStringLiteral("top")},
            {QStringLiteral("density"), QStringLiteral("comfortable")},
            {QStringLiteral("radius"), 18},
            {QStringLiteral("gap"), 12},
            {QStringLiteral("glow"), true},
            {QStringLiteral("motion"), true},
            {QStringLiteral("surfacePacks"), false},
            {QStringLiteral("media"), true},
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
        {QStringLiteral("palette"), {QStringLiteral("spectrum"), QStringLiteral("wallpaper"), QStringLiteral("ember")}},
        {QStringLiteral("material"), {QStringLiteral("glass"), QStringLiteral("solid"), QStringLiteral("light")}},
        {QStringLiteral("edge"), {QStringLiteral("top"), QStringLiteral("bottom")}},
        {QStringLiteral("density"), {QStringLiteral("comfortable"), QStringLiteral("compact")}},
        {QStringLiteral("visualizer"),
         {QStringLiteral("ribbon"), QStringLiteral("bars"), QStringLiteral("halo"), QStringLiteral("off")}}};
    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        if (!result.contains(it.key()))
            return false;
        if (enums.contains(it.key())) {
            if (it.value().metaType().id() != QMetaType::QString
                || !enums.value(it.key()).contains(it.value().toString()))
                return false;
        } else if (result.value(it.key()).metaType().id() == QMetaType::Bool) {
            if (it.value().metaType().id() != QMetaType::Bool)
                return false;
        } else {
            const auto type = it.value().metaType().id();
            if (type != QMetaType::Int && type != QMetaType::Double && type != QMetaType::LongLong)
                return false;
            const double number = it.value().toDouble();
            const int minimum = it.key() == QLatin1String("radius") ? 4 : 6;
            if (!std::isfinite(number) || std::floor(number) != number || number < minimum || number > 30)
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
bool AppearanceStore::commit(const QVariantMap& values)
{
    QVariantMap validated;
    if (!validate(values, validated))
        return fail(tr("Invalid appearance settings."));
    if (!QDir().mkpath(QFileInfo(m_path).absolutePath()))
        return fail(tr("Cannot create the appearance settings directory."));
    QSaveFile file(m_path);
    const auto bytes = QJsonDocument(QJsonObject{{QStringLiteral("version"), 1},
                                                 {QStringLiteral("settings"), QJsonObject::fromVariantMap(validated)}})
                           .toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return fail(file.errorString());
    fail(QString());
    if (m_values != validated) {
        const bool geometry = m_values.value(QStringLiteral("edge")) != validated.value(QStringLiteral("edge"))
            || m_values.value(QStringLiteral("density")) != validated.value(QStringLiteral("density"))
            || m_values.value(QStringLiteral("gap")) != validated.value(QStringLiteral("gap"));
        m_values = validated;
        Q_EMIT changed();
        if (geometry)
            Q_EMIT geometryChanged();
    }
    return true;
}
bool AppearanceStore::setValue(const QString& key, const QVariant& value)
{
    auto next = m_values;
    next[key] = value;
    return commit(next);
}
bool AppearanceStore::applyPreset(const QString& preset)
{
    auto next = defaults();
    if (preset == QLatin1String("paper")) {
        next[QStringLiteral("palette")] = QStringLiteral("wallpaper");
        next[QStringLiteral("material")] = QStringLiteral("light");
        next[QStringLiteral("radius")] = 24;
        next[QStringLiteral("glow")] = false;
    } else if (preset == QLatin1String("ember")) {
        next[QStringLiteral("palette")] = QStringLiteral("ember");
        next[QStringLiteral("material")] = QStringLiteral("solid");
        next[QStringLiteral("density")] = QStringLiteral("compact");
        next[QStringLiteral("radius")] = 8;
        next[QStringLiteral("edge")] = QStringLiteral("bottom");
        next[QStringLiteral("glow")] = false;
    } else if (preset != QLatin1String("phosphor"))
        return fail(tr("Unknown appearance preset."));
    return commit(next);
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
