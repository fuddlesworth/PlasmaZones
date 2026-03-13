// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "settings.h"
#include "colorimporter.h"
#include "configdefaults.h"
#include "../core/constants.h"
#include "../core/logging.h"
#include "../core/utils.h"
#include <KConfig>
#include <KConfigGroup>
#include <KSharedConfig>
#include <KColorScheme>
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

int Settings::readValidatedInt(const KConfigGroup& group, const char* key, int defaultValue, int min, int max,
                               const char* settingName)
{
    int value = group.readEntry(QLatin1String(key), defaultValue);
    if (value < min || value > max) {
        qCWarning(lcConfig) << settingName << ":" << value << "invalid, using default (must be" << min << "-" << max
                            << ")";
        value = defaultValue;
    }
    return value;
}

QColor Settings::readValidatedColor(const KConfigGroup& group, const char* key, const QColor& defaultValue,
                                    const char* settingName)
{
    QColor color = group.readEntry(QLatin1String(key), defaultValue);
    if (!color.isValid()) {
        qCWarning(lcConfig) << settingName << "color: invalid, using default";
        color = defaultValue;
    }
    return color;
}

void Settings::loadIndexedShortcuts(const KConfigGroup& group, const QString& keyPattern, QString (&shortcuts)[9],
                                    const QString (&defaults)[9])
{
    for (int i = 0; i < 9; ++i) {
        QString key = keyPattern.arg(i + 1);
        shortcuts[i] = group.readEntry(key, defaults[i]);
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

QVariantList Settings::loadTriggerList(const KConfigGroup& group, const QString& key, int legacyModifier,
                                       int legacyMouseButton)
{
    QString json = group.readEntry(key, QString());
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

void Settings::saveTriggerList(KConfigGroup& group, const QString& key, const QVariantList& triggers)
{
    QJsonArray arr;
    for (const QVariant& t : triggers) {
        auto map = t.toMap();
        QJsonObject obj;
        obj[QLatin1String("modifier")] = map.value(QStringLiteral("modifier"), 0).toInt();
        obj[QLatin1String("mouseButton")] = map.value(QStringLiteral("mouseButton"), 0).toInt();
        arr.append(obj);
    }
    group.writeEntry(key, QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
}

bool Settings::isWindowExcluded(const QString& appName, const QString& windowClass) const
{
    for (const auto& excluded : m_excludedApplications) {
        if (appName.contains(excluded, Qt::CaseInsensitive)) {
            return true;
        }
    }
    for (const auto& excluded : m_excludedWindowClasses) {
        if (windowClass.contains(excluded, Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}

// ── load() dispatcher ────────────────────────────────────────────────────────

void Settings::load()
{
    auto config = KSharedConfig::openConfig(QStringLiteral("plasmazonesrc"));
    config->reparseConfiguration();

    // Capture old values for post-load signal emission
    const QString oldDefaultLayoutId = m_defaultLayoutId;
    const bool oldEnableShaders = m_enableShaderEffects;
    const int oldShaderFrameRate = m_shaderFrameRate;
    const bool oldEnableAudioViz = m_enableAudioVisualizer;
    const int oldBarCount = m_audioSpectrumBarCount;

    KConfigGroup activation = config->group(QStringLiteral("Activation"));
    loadActivationConfig(activation);
    loadDisplayConfig(config->group(QStringLiteral("Display")));
    loadAppearanceConfig(config->group(QStringLiteral("Appearance")));
    loadZoneGeometryConfig(config->group(QStringLiteral("Zones")));
    loadBehaviorConfig(config->group(QStringLiteral("Behavior")), config->group(QStringLiteral("Exclusions")),
                       activation);
    loadZoneSelectorConfig(config->group(QStringLiteral("ZoneSelector")));
    loadPerScreenOverrides(config);

    // Shaders (small enough to stay inline)
    KConfigGroup shaders = config->group(QStringLiteral("Shaders"));
    m_enableShaderEffects =
        shaders.readEntry(QLatin1String("EnableShaderEffects"), ConfigDefaults::enableShaderEffects());
    m_shaderFrameRate =
        qBound(30, shaders.readEntry(QLatin1String("ShaderFrameRate"), ConfigDefaults::shaderFrameRate()), 144);
    m_enableAudioVisualizer =
        shaders.readEntry(QLatin1String("EnableAudioVisualizer"), ConfigDefaults::enableAudioVisualizer());
    m_audioSpectrumBarCount = qBound(
        16, shaders.readEntry(QLatin1String("AudioSpectrumBarCount"), ConfigDefaults::audioSpectrumBarCount()), 256);

    loadShortcutConfig(config->group(QStringLiteral("GlobalShortcuts")));
    loadAutotilingConfig(config->group(QStringLiteral("Autotiling")), config->group(QStringLiteral("Animations")),
                         config->group(QStringLiteral("AutotileShortcuts")));

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
    auto config = KSharedConfig::openConfig(QStringLiteral("plasmazonesrc"));
    KConfigGroup activation = config->group(QStringLiteral("Activation"));
    KConfigGroup display = config->group(QStringLiteral("Display"));
    KConfigGroup appearance = config->group(QStringLiteral("Appearance"));
    KConfigGroup zones = config->group(QStringLiteral("Zones"));
    KConfigGroup behavior = config->group(QStringLiteral("Behavior"));
    KConfigGroup exclusions = config->group(QStringLiteral("Exclusions"));
    KConfigGroup zoneSelector = config->group(QStringLiteral("ZoneSelector"));
    KConfigGroup globalShortcuts = config->group(QStringLiteral("GlobalShortcuts"));
    KConfigGroup autotiling = config->group(QStringLiteral("Autotiling"));
    KConfigGroup animations = config->group(QStringLiteral("Animations"));
    KConfigGroup autotileShortcuts = config->group(QStringLiteral("AutotileShortcuts"));

    saveActivationConfig(activation);
    saveDisplayConfig(display);
    saveAppearanceConfig(appearance);
    saveZoneGeometryConfig(zones);
    saveBehaviorConfig(behavior, exclusions, activation);
    saveZoneSelectorConfig(zoneSelector);
    saveAllPerScreenOverrides(config);
    saveShortcutConfig(globalShortcuts);
    saveAutotilingConfig(autotiling, animations, autotileShortcuts);

    // Shader Effects (4 entries, not worth a separate helper)
    KConfigGroup shaders = config->group(QStringLiteral("Shaders"));
    shaders.writeEntry(QLatin1String("EnableShaderEffects"), m_enableShaderEffects);
    shaders.writeEntry(QLatin1String("ShaderFrameRate"), m_shaderFrameRate);
    shaders.writeEntry(QLatin1String("EnableAudioVisualizer"), m_enableAudioVisualizer);
    shaders.writeEntry(QLatin1String("AudioSpectrumBarCount"), m_audioSpectrumBarCount);

    config->sync();
}

// ── reset / color helpers ────────────────────────────────────────────────────

void Settings::reset()
{
    auto config = KSharedConfig::openConfig(QStringLiteral("plasmazonesrc"));
    const QStringList groups = {
        QStringLiteral("Activation"),   QStringLiteral("Display"),           QStringLiteral("Appearance"),
        QStringLiteral("Zones"),        QStringLiteral("Behavior"),          QStringLiteral("Exclusions"),
        QStringLiteral("ZoneSelector"), QStringLiteral("Shaders"),           QStringLiteral("GlobalShortcuts"),
        QStringLiteral("Autotiling"),   QStringLiteral("AutotileShortcuts"), QStringLiteral("Animations"),
        QStringLiteral("Updates")};
    for (const QString& groupName : groups) {
        config->deleteGroup(groupName);
    }
    // Also delete per-screen override groups
    const QStringList allGroups = config->groupList();
    for (const QString& groupName : allGroups) {
        if (groupName.startsWith(QLatin1String("ZoneSelector:"))
            || groupName.startsWith(QLatin1String("AutotileScreen:"))
            || groupName.startsWith(QLatin1String("SnappingScreen:"))) {
            config->deleteGroup(groupName);
        }
    }
    config->sync();
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
    // Selection set for highlight color (accent/selection background)
    KColorScheme selectionScheme(QPalette::Active, KColorScheme::Selection);
    QColor highlight = selectionScheme.background(KColorScheme::NormalBackground).color();
    highlight.setAlpha(Defaults::HighlightAlpha);
    m_highlightColor = highlight;

    // View set for inactive/border/text (neutral non-accent colors)
    // This matches QML defaults: highlightColor → Theme.highlightColor, inactiveColor → Theme.textColor
    KColorScheme viewScheme(QPalette::Active, KColorScheme::View);
    QColor inactive = viewScheme.foreground(KColorScheme::NormalText).color();
    inactive.setAlpha(Defaults::InactiveAlpha);
    m_inactiveColor = inactive;

    QColor border = viewScheme.foreground(KColorScheme::NormalText).color();
    border.setAlpha(Defaults::BorderAlpha);
    m_borderColor = border;

    m_labelFontColor = viewScheme.foreground(KColorScheme::NormalText).color();

    Q_EMIT highlightColorChanged();
    Q_EMIT inactiveColorChanged();
    Q_EMIT borderColorChanged();
    Q_EMIT labelFontColorChanged();
}

void Settings::applyAutotileBorderSystemColor()
{
    KColorScheme scheme(QPalette::Active, KColorScheme::Selection);
    QColor border = scheme.foreground(KColorScheme::NormalText).color();
    border.setAlpha(Defaults::BorderAlpha);
    m_autotileBorderColor = border;
    Q_EMIT autotileBorderColorChanged();
}

} // namespace PlasmaZones
