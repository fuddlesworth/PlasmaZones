// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#include <PhosphorShellPicker/AppearanceLibrary.h>
#include <PhosphorTheme/AppearanceStore.h>
#include <QDir>
#include <QSet>
#include <QFileInfo>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUuid>
namespace PhosphorShellPicker {
namespace {
const QString Settings = QStringLiteral("settings"), RecipeName = QStringLiteral("name"), Id = QStringLiteral("id");
bool validName(const QString& name)
{
    return !name.trimmed().isEmpty() && name.size() <= 60 && std::all_of(name.cbegin(), name.cend(), [](QChar c) {
        return c.isPrint();
    });
}
bool saveJson(const QString& path, const QJsonObject& object, qsizetype maximum = 65536)
{
    QSaveFile file(path);
    const auto bytes = QJsonDocument(object).toJson();
    if (bytes.size() > maximum)
        return false;
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
}
QVariantMap AppearanceLibrary::scopedSettings(const QVariantMap& values, bool wallpaper, bool bar)
{
    QStringList keys{QStringLiteral("palette"),
                     QStringLiteral("material"),
                     QStringLiteral("density"),
                     QStringLiteral("radius"),
                     QStringLiteral("gap"),
                     QStringLiteral("glow"),
                     QStringLiteral("uiFont"),
                     QStringLiteral("monoFont"),
                     QStringLiteral("textScale"),
                     QStringLiteral("accentIndex"),
                     QStringLiteral("wallpaperColors"),
                     QStringLiteral("surfacePacks"),
                     QStringLiteral("surfaceEffect"),
                     QStringLiteral("desktopStyle")};
    if (wallpaper)
        keys.append(QStringLiteral("wallpapers"));
    if (bar)
        keys.append(
            {QStringLiteral("barLayout"), QStringLiteral("barInset"), QStringLiteral("edge"), QStringLiteral("media")});
    QVariantMap result;
    for (const auto& key : keys) {
        if (values.contains(key))
            result[key] = values.value(key);
    }
    return result;
}
QVariantList AppearanceLibrary::presets() const
{
    QVariantList result;
    for (const auto& id : {QStringLiteral("phosphor"), QStringLiteral("paper"), QStringLiteral("ember")}) {
        auto settings = PhosphorTheme::AppearanceStore::defaults();
        QString description = tr("The original spectrum. Glass, light and room to breathe.");
        if (id == QStringLiteral("paper")) {
            settings[QStringLiteral("palette")] = QStringLiteral("wallpaper");
            settings[QStringLiteral("material")] = QStringLiteral("light");
            settings[QStringLiteral("radius")] = 24;
            settings[QStringLiteral("gap")] = 22;
            settings[QStringLiteral("glow")] = false;
            description = tr("A light canvas. Soft edges and a quieter glow.");
        } else if (id == QStringLiteral("ember")) {
            settings[QStringLiteral("palette")] = QStringLiteral("ember");
            settings[QStringLiteral("material")] = QStringLiteral("solid");
            settings[QStringLiteral("density")] = QStringLiteral("compact");
            settings[QStringLiteral("radius")] = 8;
            settings[QStringLiteral("gap")] = 10;
            settings[QStringLiteral("glow")] = false;
            settings[QStringLiteral("edge")] = QStringLiteral("bottom");
            settings[QStringLiteral("media")] = false;
            description = tr("Honey, clay and rose. A compact, grounded look.");
        }
        auto scoped = scopedSettings(settings, false, true);
        // Built-in styles never replace user fonts, effects or current wallpaper colors.
        for (const auto& key : {QStringLiteral("uiFont"), QStringLiteral("monoFont"), QStringLiteral("textScale"),
                                QStringLiteral("wallpaperColors"), QStringLiteral("surfacePacks"),
                                QStringLiteral("surfaceEffect"), QStringLiteral("desktopStyle")})
            scoped.remove(key);
        result.append(QVariantMap{{Id, id},
                                  {RecipeName,
                                   id == QStringLiteral("phosphor")    ? tr("Phosphor")
                                       : id == QStringLiteral("paper") ? tr("Paper")
                                                                       : tr("Ember")},
                                  {QStringLiteral("description"), description},
                                  {Settings, scoped},
                                  {QStringLiteral("builtIn"), true}});
    }
    return result + m_saved;
}
QVariantMap AppearanceLibrary::recipe(const QString& id) const
{
    if (id == QStringLiteral("imported"))
        return m_imported;
    for (const auto& value : presets()) {
        if (value.toMap().value(Id).toString() == id)
            return value.toMap();
    }
    return {};
}
QVariantMap AppearanceLibrary::normalizeRecipe(const QVariantMap& value)
{
    if (!validName(value.value(RecipeName).toString())
        || value.value(Settings).metaType().id() != QMetaType::QVariantMap)
        return {};
    QVariantMap validated;
    const auto source = value.value(Settings).toMap();
    if (source.isEmpty() || !PhosphorTheme::AppearanceStore::validate(source, validated))
        return {};
    const auto settings = scopedSettings(source, true, true);
    if (settings.isEmpty())
        return {};
    return {{RecipeName, value.value(RecipeName).toString().trimmed()}, {Settings, settings}};
}
void AppearanceLibrary::loadPresets()
{
    QFile file(m_directory + QStringLiteral("/presets.json"));
    if (!file.exists())
        return;
    if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024) {
        fail(tr("Could not read your saved looks."));
        return;
    }
    const auto root = QJsonDocument::fromJson(file.readAll()).object();
    const auto entries = root.value(QStringLiteral("presets")).toArray();
    if (root.value(QStringLiteral("version")).toInt() != 1 || !root.value(QStringLiteral("presets")).isArray()
        || entries.size() > 64) {
        fail(tr("The saved looks file is invalid."));
        return;
    }
    QSet<QString> ids, names;
    QVariantList loaded;
    for (const auto& entry : entries) {
        auto value = normalizeRecipe(entry.toObject().toVariantMap());
        const auto id = entry.toObject().value(Id).toString();
        const auto name = value.value(RecipeName).toString().toCaseFolded();
        if (value.isEmpty() || QUuid(id).isNull() || ids.contains(id) || names.contains(name)) {
            fail(tr("The saved looks file is invalid."));
            return;
        }
        value[Id] = id;
        ids.insert(id);
        names.insert(name);
        loaded.append(value);
    }
    m_saved = loaded;
}
bool AppearanceLibrary::writePresets(const QVariantList& presets)
{
    if (presets.size() > 64)
        return fail(tr("You can save up to 64 looks. Remove one before adding another."));
    if (!QDir().mkpath(m_directory)
        || !saveJson(
            m_directory + QStringLiteral("/presets.json"),
            {{QStringLiteral("version"), 1}, {QStringLiteral("presets"), QJsonArray::fromVariantList(presets)}},
            1024 * 1024))
        return fail(tr("Could not save your looks. Check the folder permissions and available space."));
    m_saved = presets;
    fail(QString());
    Q_EMIT presetsChanged();
    return true;
}
QString AppearanceLibrary::uniqueName(const QString& name) const
{
    if (!validName(name))
        return {};
    for (const auto& value : presets()) {
        if (value.toMap().value(RecipeName).toString().compare(name.trimmed(), Qt::CaseInsensitive) == 0)
            return {};
    }
    return name.trimmed();
}
bool AppearanceLibrary::savePreset(const QString& name, bool wallpaper, bool bar)
{
    const auto valid = uniqueName(name);
    if (valid.isEmpty())
        return fail(tr("Choose a unique name using 1–60 printable characters."));
    auto next = m_saved;
    next.append(QVariantMap{{Id, QUuid::createUuid().toString()},
                            {RecipeName, valid},
                            {Settings, scopedSettings(m_store->values(), wallpaper, bar)}});
    return writePresets(next);
}
bool AppearanceLibrary::removePreset(const QString& id)
{
    auto next = m_saved;
    for (qsizetype i = 0; i < next.size(); ++i) {
        if (next[i].toMap().value(Id).toString() == id) {
            next.removeAt(i);
            return writePresets(next);
        }
    }
    return fail(tr("This saved look is no longer available."));
}
bool AppearanceLibrary::inspectImport(const QUrl& url)
{
    m_imported.clear();
    Q_EMIT importedChanged();
    if (!url.isLocalFile())
        return fail(tr("Choose a local preset file."));
    QFile file(url.toLocalFile());
    if (!file.open(QIODevice::ReadOnly))
        return fail(file.errorString());
    if (file.size() > 65536)
        return fail(tr("The preset is too large."));
    auto root = QJsonDocument::fromJson(file.readAll()).object();
    const auto kind = root.value(QStringLiteral("kind")).toString();
    // Accept the existing native version-1 settings export as well as named
    // appearance recipes. Browser study documents use a different contract.
    if (root.value(QStringLiteral("version")).toInt() != 1
        || (root.contains(QStringLiteral("kind")) && !root.value(QStringLiteral("kind")).isString())
        || (!kind.isEmpty() && kind != QStringLiteral("phosphor-appearance")))
        return fail(tr("Choose a Phosphor appearance preset (version 1)."));
    if (kind.isEmpty() && !root.contains(RecipeName))
        root[RecipeName] = QFileInfo(url.toLocalFile()).completeBaseName();
    const auto normalized = normalizeRecipe(root.toVariantMap());
    if (normalized.isEmpty())
        return fail(tr("This preset contains invalid appearance settings."));
    m_imported = normalized;
    fail(QString());
    Q_EMIT importedChanged();
    return true;
}
bool AppearanceLibrary::previewPreset(const QString& id, bool wallpaper, bool bar)
{
    const auto selected = recipe(id);
    if (selected.isEmpty())
        return fail(tr("This look is no longer available."));
    const auto settings = scopedSettings(selected.value(Settings).toMap(), wallpaper, bar);
    for (const auto& value : settings.value(QStringLiteral("wallpapers")).toMap()) {
        const QFileInfo image(value.toMap().value(QStringLiteral("path")).toString());
        if (!image.isFile() || !image.isReadable() || inspectImage(image.absoluteFilePath()).isEmpty())
            return fail(tr(
                "A wallpaper in this preset cannot be opened. Preview it without wallpapers or add the image first."));
    }
    auto next = m_store->values();
    for (auto it = settings.cbegin(); it != settings.cend(); ++it)
        next[it.key()] = it.value();
    if (!m_store->setValues(next))
        return fail(m_store->error());
    fail(QString());
    return true;
}
bool AppearanceLibrary::saveImported(const QString& name)
{
    const auto valid = uniqueName(name);
    if (valid.isEmpty() || m_imported.isEmpty())
        return fail(tr("Choose a unique name using 1–60 printable characters."));
    auto value = m_imported;
    value[Id] = QUuid::createUuid().toString();
    value[RecipeName] = valid;
    auto next = m_saved;
    next.append(value);
    return writePresets(next);
}
bool AppearanceLibrary::exportPreset(const QString& id, const QUrl& url)
{
    auto selected = recipe(id);
    if (selected.isEmpty() || !url.isLocalFile())
        return fail(tr("Choose a saved look and a local destination."));
    selected.remove(Id);
    selected.remove(QStringLiteral("builtIn"));
    selected.remove(QStringLiteral("description"));
    selected[QStringLiteral("kind")] = QStringLiteral("phosphor-appearance");
    selected[QStringLiteral("version")] = 1;
    if (!saveJson(url.toLocalFile(), QJsonObject::fromVariantMap(selected)))
        return fail(tr("Could not export this look."));
    fail(QString());
    return true;
}
}
