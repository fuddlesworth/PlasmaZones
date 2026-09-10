// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShellPicker/ThemePresets.h>

#include <PhosphorTheme/IThemeService.h>
#include <PhosphorTheme/PaletteStore.h>

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QStandardPaths>

Q_LOGGING_CATEGORY(lcPresets, "phosphorshellpicker.presets")

namespace PhosphorShellPicker {

namespace {

QVariantMap buildPalette(std::initializer_list<std::pair<const char*, const char*>> entries)
{
    QVariantMap map;
    for (const auto& [key, hex] : entries) {
        map.insert(QString::fromLatin1(key), QColor(QString::fromLatin1(hex)));
    }
    return map;
}

QVariantMap presetEntry(const QString& name, const QVariantMap& tokens)
{
    return {
        {QStringLiteral("name"), name},
        {QStringLiteral("tokens"), tokens},
        {QStringLiteral("swatches"), ThemePresets::swatchesFor(tokens)},
    };
}

} // namespace

ThemePresets::ThemePresets(QObject* parent)
    : QObject(parent)
    , m_directory(defaultDirectory())
{
}

ThemePresets::~ThemePresets() = default;

QString ThemePresets::directory() const
{
    return m_directory;
}

void ThemePresets::setDirectory(const QString& directory)
{
    if (m_directory == directory) {
        return;
    }
    m_directory = directory;
    Q_EMIT directoryChanged();
}

QVariantList ThemePresets::presets() const
{
    return m_presets;
}

int ThemePresets::count() const
{
    return static_cast<int>(m_presets.size());
}

QString ThemePresets::defaultDirectory()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return QDir(base).filePath(QStringLiteral("palettes"));
}

QVariantMap ThemePresets::darkPalette()
{
    return PhosphorTheme::PaletteStore::defaultPalette();
}

QVariantMap ThemePresets::lightPalette()
{
    using PhosphorTheme::TokenNames;
    // The light variant of the picker mock: the spectrum's light stops
    // (Spectrum.qml's own) on a pale blue ground, navy text.
    return buildPalette({
        {TokenNames::Background, "#F6F9FF"},
        {TokenNames::Surface, "#EEF3FF"},
        {TokenNames::SurfaceContainer, "#E8EEFF"},
        {TokenNames::SurfaceContainerHigh, "#DCE5FB"},
        {TokenNames::SurfaceVariant, "#CBD5F0"},
        {TokenNames::OnSurface, "#0B1730"},
        {TokenNames::OnSurfaceVariant, "#475569"},
        {TokenNames::Primary, "#3B82F6"},
        {TokenNames::OnPrimary, "#FFFFFF"},
        {TokenNames::PrimaryContainer, "#DBEAFE"},
        {TokenNames::OnPrimaryContainer, "#1E3A8A"},
        {TokenNames::Secondary, "#7C3AED"},
        {TokenNames::OnSecondary, "#FFFFFF"},
        {TokenNames::SecondaryContainer, "#EDE9FE"},
        {TokenNames::Tertiary, "#0EA5E9"},
        {TokenNames::OnTertiary, "#FFFFFF"},
        {TokenNames::TertiaryContainer, "#E0F2FE"},
        {TokenNames::Error, "#E11D48"},
        {TokenNames::OnError, "#FFFFFF"},
        {TokenNames::ErrorContainer, "#FFE4E6"},
        {TokenNames::Outline, "#94A3B8"},
        {TokenNames::OutlineVariant, "#CBD5E1"},
        {TokenNames::Success, "#059669"},
        {TokenNames::SuccessBright, "#10B981"},
        {TokenNames::Warning, "#D97706"},
        {TokenNames::WarningBright, "#F59E0B"},
        {TokenNames::ErrorBright, "#F43F5E"},
        {TokenNames::Info, "#0284C7"},
        {TokenNames::InfoBright, "#0EA5E9"},
    });
}

QVariantList ThemePresets::swatchesFor(const QVariantMap& tokens)
{
    using PhosphorTheme::TokenNames;
    static const char* const keys[] = {
        TokenNames::Surface,  TokenNames::SurfaceContainerHigh, TokenNames::Primary, TokenNames::Secondary,
        TokenNames::Tertiary,
    };
    QVariantList swatches;
    for (const char* key : keys) {
        swatches.append(tokens.value(QString::fromLatin1(key), QColor(Qt::transparent)));
    }
    return swatches;
}

QVariantMap ThemePresets::readPaletteFile(const QString& path)
{
    // A palette is a small flat map of token names to colours; a megabyte is
    // already orders of magnitude past any real one. The scan directory is
    // user-writable, so an oversized or non-regular file reached through a
    // symlink with a .json name would otherwise be pulled into memory whole
    // on the GUI thread. isFile() is checked on the RESOLVED target, so a
    // symlink to a fifo or a device node is refused rather than blocking the
    // shell on open.
    static constexpr qint64 MaxPaletteBytes = 1024 * 1024;
    const QFileInfo info(path);
    if (!info.isFile() || info.size() > MaxPaletteBytes) {
        qCWarning(lcPresets) << "ignoring palette" << path << "— not a regular file, or larger than" << MaxPaletteBytes
                             << "bytes";
        return {};
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) {
        return {};
    }
    QJsonObject object = doc.object();
    if (object.contains(QLatin1String("tokens"))) {
        if (!object.value(QLatin1String("tokens")).isObject()) {
            return {};
        }
        object = object.value(QLatin1String("tokens")).toObject();
    }
    QVariantMap tokens;
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        if (!it.value().isString()) {
            continue;
        }
        const QColor color(it.value().toString());
        if (color.isValid()) {
            tokens.insert(it.key(), color);
        }
    }
    return tokens;
}

void ThemePresets::rescan()
{
    QVariantList scanned;
    scanned.append(presetEntry(QStringLiteral("Dark"), darkPalette()));
    scanned.append(presetEntry(QStringLiteral("Light"), lightPalette()));

    const QDir dir(m_directory);
    if (dir.exists()) {
        const QFileInfoList files =
            dir.entryInfoList({QStringLiteral("*.json")}, QDir::Files | QDir::Readable, QDir::Name | QDir::IgnoreCase);
        for (const QFileInfo& file : files) {
            if (file.fileName() == QLatin1String("current.json")) {
                continue;
            }
            const QVariantMap tokens = readPaletteFile(file.absoluteFilePath());
            if (tokens.isEmpty()) {
                continue;
            }
            scanned.append(presetEntry(file.completeBaseName(), tokens));
        }
    }

    if (scanned == m_presets) {
        return;
    }
    m_presets = scanned;
    Q_EMIT presetsChanged();
}

QVariantMap ThemePresets::tokensFor(const QString& name) const
{
    for (const QVariant& entry : m_presets) {
        const QVariantMap map = entry.toMap();
        if (map.value(QStringLiteral("name")).toString() == name) {
            return map.value(QStringLiteral("tokens")).toMap();
        }
    }
    return {};
}

} // namespace PhosphorShellPicker
