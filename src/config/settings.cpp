// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "settings.h"
#include "colorimporter.h"
#include "configdefaults.h"
#include "configbackend_qsettings.h"
#include "../core/constants.h"
#include "../core/logging.h"
#include "../core/utils.h"
#include <QGuiApplication>
#include <QPalette>
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>
#include "../autotile/AlgorithmRegistry.h"
#include <climits> // For INT_MAX in readValidatedInt

namespace PlasmaZones {

// ── Constructor ──────────────────────────────────────────────────────────────

Settings::Settings(QObject* parent)
    : ISettings(parent)
    , m_configBackend(createDefaultConfigBackend())
{
    load();
}

// ── Helper Methods ───────────────────────────────────────────────────────────

QString Settings::normalizeUuidString(const QString& uuidStr)
{
    if (uuidStr.isEmpty()) {
        return QString();
    }
    QUuid uuid = QUuid::fromString(uuidStr);
    if (uuid.isNull()) {
        qCWarning(lcConfig) << "Invalid UUID string in config, ignoring:" << uuidStr;
        return QString();
    }
    return uuid.toString();
}

int Settings::readValidatedInt(ConfigGroup& group, const char* key, int defaultValue, int min, int max,
                               const char* settingName)
{
    int value = group.readInt(QString::fromLatin1(key), defaultValue);
    if (value < min || value > max) {
        qCWarning(lcConfig) << settingName << ":" << value << "invalid, using default (must be" << min << "-" << max
                            << ")";
        value = defaultValue;
    }
    return value;
}

QColor Settings::readValidatedColor(ConfigGroup& group, const char* key, const QColor& defaultValue,
                                    const char* settingName)
{
    QColor color = group.readColor(QString::fromLatin1(key), defaultValue);
    if (!color.isValid()) {
        qCWarning(lcConfig) << settingName << "color: invalid, using default";
        color = defaultValue;
    }
    return color;
}

void Settings::loadIndexedShortcuts(ConfigGroup& group, const QString& keyPattern, QString (&shortcuts)[9],
                                    const QString (&defaults)[9])
{
    for (int i = 0; i < 9; ++i) {
        QString key = keyPattern.arg(i + 1);
        shortcuts[i] = group.readString(key, defaults[i]);
    }
}

std::optional<QVariantList> Settings::parseTriggerListJson(const QString& json)
{
    if (json.isEmpty()) {
        return std::nullopt;
    }
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isArray()) {
        qCWarning(lcConfig) << "Invalid trigger list JSON:" << parseError.errorString() << "- using fallback";
        return std::nullopt;
    }
    QVariantList result;
    const QJsonArray arr = doc.array();
    for (const QJsonValue& val : arr) {
        if (val.isObject()) {
            QJsonObject obj = val.toObject();
            QVariantMap trigger;
            trigger[QStringLiteral("modifier")] = obj.value(QLatin1String("modifier")).toInt(0);
            trigger[QStringLiteral("mouseButton")] = obj.value(QLatin1String("mouseButton")).toInt(0);
            result.append(trigger);
        } else {
            qCWarning(lcConfig) << "Trigger array: non-object element at index" << result.size() << ", skipping";
        }
    }
    if (result.size() > MaxTriggersPerAction) {
        result = result.mid(0, MaxTriggersPerAction);
    }
    return result; // May be empty (valid [] means no triggers)
}

QVariantList Settings::loadTriggerList(ConfigGroup& group, const QString& key, int legacyModifier,
                                       int legacyMouseButton)
{
    QString json = group.readString(key);
    std::optional<QVariantList> parsed = parseTriggerListJson(json);
    if (parsed.has_value()) {
        qCDebug(lcConfig) << "Loaded" << key << ":" << parsed->size() << "triggers";
        return *parsed;
    }
    // No valid JSON: construct single-element trigger list from legacy values
    QVariantMap trigger;
    trigger[QStringLiteral("modifier")] = legacyModifier;
    trigger[QStringLiteral("mouseButton")] = legacyMouseButton;
    return {trigger};
}

void Settings::saveTriggerList(ConfigGroup& group, const QString& key, const QVariantList& triggers)
{
    QJsonArray arr;
    for (const QVariant& t : triggers) {
        auto map = t.toMap();
        QJsonObject obj;
        obj[QLatin1String("modifier")] = map.value(QStringLiteral("modifier"), 0).toInt();
        obj[QLatin1String("mouseButton")] = map.value(QStringLiteral("mouseButton"), 0).toInt();
        arr.append(obj);
    }
    group.writeString(key, QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
}

// ── load() dispatcher ────────────────────────────────────────────────────────

void Settings::load()
{
    m_configBackend->reparseConfiguration();

    // Capture old values for post-load signal emission
    const QString oldDefaultLayoutId = m_defaultLayoutId;
    const bool oldEnableShaders = m_enableShaderEffects;
    const int oldShaderFrameRate = m_shaderFrameRate;
    const bool oldEnableAudioViz = m_enableAudioVisualizer;
    const int oldBarCount = m_audioSpectrumBarCount;

    {
        auto activation = m_configBackend->group(QStringLiteral("Activation"));
        loadActivationConfig(*activation);
    }
    {
        auto display = m_configBackend->group(QStringLiteral("Display"));
        loadDisplayConfig(*display);
    }
    {
        auto appearance = m_configBackend->group(QStringLiteral("Appearance"));
        loadAppearanceConfig(*appearance);
    }
    {
        auto zones = m_configBackend->group(QStringLiteral("Zones"));
        loadZoneGeometryConfig(*zones);
    }
    loadBehaviorConfig(m_configBackend.get());
    {
        auto zoneSelector = m_configBackend->group(QStringLiteral("ZoneSelector"));
        loadZoneSelectorConfig(*zoneSelector);
    }
    loadPerScreenOverrides(m_configBackend.get());

    // Shaders (small enough to stay inline)
    {
        auto shaders = m_configBackend->group(QStringLiteral("Shaders"));
        m_enableShaderEffects =
            shaders->readBool(QStringLiteral("EnableShaderEffects"), ConfigDefaults::enableShaderEffects());
        m_shaderFrameRate =
            qBound(30, shaders->readInt(QStringLiteral("ShaderFrameRate"), ConfigDefaults::shaderFrameRate()), 144);
        m_enableAudioVisualizer =
            shaders->readBool(QStringLiteral("EnableAudioVisualizer"), ConfigDefaults::enableAudioVisualizer());
        m_audioSpectrumBarCount = qBound(
            16, shaders->readInt(QStringLiteral("AudioSpectrumBarCount"), ConfigDefaults::audioSpectrumBarCount()),
            256);
    }

    {
        auto globalShortcuts = m_configBackend->group(QStringLiteral("GlobalShortcuts"));
        loadShortcutConfig(*globalShortcuts);
    }
    loadAutotilingConfig(m_configBackend.get());

    if (m_useSystemColors) {
        applySystemColorScheme();
    }
    if (m_autotileUseSystemBorderColors) {
        applyAutotileBorderSystemColor();
    }

    qCInfo(lcConfig) << "Settings loaded";
    Q_EMIT settingsChanged();

    // Emit specific signals for settings with runtime side-effects
    if (m_enableShaderEffects != oldEnableShaders)
        Q_EMIT enableShaderEffectsChanged();
    if (m_shaderFrameRate != oldShaderFrameRate)
        Q_EMIT shaderFrameRateChanged();
    if (m_enableAudioVisualizer != oldEnableAudioViz)
        Q_EMIT enableAudioVisualizerChanged();
    if (m_audioSpectrumBarCount != oldBarCount)
        Q_EMIT audioSpectrumBarCountChanged();
    if (m_defaultLayoutId != oldDefaultLayoutId)
        Q_EMIT defaultLayoutIdChanged();
}

// ── save() dispatcher ────────────────────────────────────────────────────────

void Settings::save()
{
    {
        auto activation = m_configBackend->group(QStringLiteral("Activation"));
        saveActivationConfig(*activation);
    }
    {
        auto display = m_configBackend->group(QStringLiteral("Display"));
        saveDisplayConfig(*display);
    }
    {
        auto appearance = m_configBackend->group(QStringLiteral("Appearance"));
        saveAppearanceConfig(*appearance);
    }
    {
        auto zones = m_configBackend->group(QStringLiteral("Zones"));
        saveZoneGeometryConfig(*zones);
    }
    saveBehaviorConfig(m_configBackend.get());
    {
        auto zoneSelector = m_configBackend->group(QStringLiteral("ZoneSelector"));
        saveZoneSelectorConfig(*zoneSelector);
    }
    saveAllPerScreenOverrides(m_configBackend.get());
    {
        auto globalShortcuts = m_configBackend->group(QStringLiteral("GlobalShortcuts"));
        saveShortcutConfig(*globalShortcuts);
    }
    saveAutotilingConfig(m_configBackend.get());

    // Shader Effects (4 entries, not worth a separate helper)
    {
        auto shaders = m_configBackend->group(QStringLiteral("Shaders"));
        shaders->writeBool(QStringLiteral("EnableShaderEffects"), m_enableShaderEffects);
        shaders->writeInt(QStringLiteral("ShaderFrameRate"), m_shaderFrameRate);
        shaders->writeBool(QStringLiteral("EnableAudioVisualizer"), m_enableAudioVisualizer);
        shaders->writeInt(QStringLiteral("AudioSpectrumBarCount"), m_audioSpectrumBarCount);
    }

    m_configBackend->sync();
}

// ── reset / color helpers ────────────────────────────────────────────────────

void Settings::reset()
{
    const QStringList groups = {
        QStringLiteral("Activation"),   QStringLiteral("Display"),           QStringLiteral("Appearance"),
        QStringLiteral("Zones"),        QStringLiteral("Behavior"),          QStringLiteral("Exclusions"),
        QStringLiteral("ZoneSelector"), QStringLiteral("Shaders"),           QStringLiteral("GlobalShortcuts"),
        QStringLiteral("Autotiling"),   QStringLiteral("AutotileShortcuts"), QStringLiteral("Animations"),
        QStringLiteral("Updates")};
    for (const QString& groupName : groups) {
        m_configBackend->deleteGroup(groupName);
    }
    // Also delete per-screen override groups
    const QStringList allGroups = m_configBackend->groupList();
    for (const QString& groupName : allGroups) {
        if (groupName.startsWith(QLatin1String("ZoneSelector:"))
            || groupName.startsWith(QLatin1String("AutotileScreen:"))
            || groupName.startsWith(QLatin1String("SnappingScreen:"))) {
            m_configBackend->deleteGroup(groupName);
        }
    }
    m_configBackend->sync();
    load();
    qCInfo(lcConfig) << "Settings reset to defaults";
}

QString Settings::loadColorsFromFile(const QString& filePath)
{
    ColorImportResult result = ColorImporter::importFromFile(filePath);
    if (!result.success) {
        return result.errorMessage;
    }
    setHighlightColor(result.highlightColor);
    setInactiveColor(result.inactiveColor);
    setBorderColor(result.borderColor);
    setLabelFontColor(result.labelFontColor);
    m_useSystemColors = false;
    Q_EMIT useSystemColorsChanged();
    return QString(); // Success - no error
}

void Settings::applySystemColorScheme()
{
    // QPalette respects QT_QPA_PLATFORMTHEME — on non-KDE desktops, Qt reads
    // the platform theme (qt6ct, gnome, lxqt) to populate the palette.
    const QPalette pal = QGuiApplication::palette();

    QColor highlight = pal.color(QPalette::Active, QPalette::Highlight);
    highlight.setAlpha(Defaults::HighlightAlpha);
    m_highlightColor = highlight;

    QColor inactive = pal.color(QPalette::Active, QPalette::Text);
    inactive.setAlpha(Defaults::InactiveAlpha);
    m_inactiveColor = inactive;

    QColor border = pal.color(QPalette::Active, QPalette::Text);
    border.setAlpha(Defaults::BorderAlpha);
    m_borderColor = border;

    m_labelFontColor = pal.color(QPalette::Active, QPalette::Text);

    Q_EMIT highlightColorChanged();
    Q_EMIT inactiveColorChanged();
    Q_EMIT borderColorChanged();
    Q_EMIT labelFontColorChanged();
}

void Settings::applyAutotileBorderSystemColor()
{
    // Use the exact snapping zone highlight/inactive colors including their alpha.
    m_autotileBorderColor = m_highlightColor;
    Q_EMIT autotileBorderColorChanged();

    m_autotileInactiveBorderColor = m_inactiveColor;
    Q_EMIT autotileInactiveBorderColorChanged();
}

} // namespace PlasmaZones
