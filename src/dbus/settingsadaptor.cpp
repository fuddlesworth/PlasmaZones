// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "settingsadaptor.h"
#include "../core/interfaces.h"
#include "../config/settings.h" // For concrete Settings type
#include "../core/logging.h"
#include "../core/shaderregistry.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QColor>
#include <QDBusVariant>
#include <QDBusAbstractAdaptor> // Explicit include to ensure proper namespace
#include <QDBusConnection>
#include <QDBusMessage>

namespace PlasmaZones {

SettingsAdaptor::SettingsAdaptor(ISettings* settings, QObject* parent)
    : QDBusAbstractAdaptor(parent)
    , m_settings(settings)
    , m_saveTimer(new QTimer(this))
{
    Q_ASSERT(settings);
    initializeRegistry();

    // Configure debounced save timer (performance optimization)
    m_saveTimer->setSingleShot(true);
    m_saveTimer->setInterval(SaveDebounceMs);
    connect(m_saveTimer, &QTimer::timeout, this, [this]() {
        m_settings->save();
        qCDebug(lcDbusSettings) << "Debounced settings save completed";
    });

    // Connect to interface signals (DIP)
    connect(m_settings, &ISettings::settingsChanged, this, &SettingsAdaptor::settingsChanged);
}

SettingsAdaptor::~SettingsAdaptor()
{
    // Flush any pending debounced saves before destruction
    // This ensures settings are not lost on shutdown
    if (m_saveTimer->isActive()) {
        m_saveTimer->stop();
        m_settings->save();
        qCDebug(lcDbusSettings) << "Flushed pending settings save on destruction";
    }
}

void SettingsAdaptor::scheduleSave()
{
    // Restart the timer on each setting change (debouncing)
    // This batches multiple rapid changes into a single save
    m_saveTimer->start();
}

// Note: Macros are defined in initializeRegistry() to avoid namespace pollution

void SettingsAdaptor::initializeRegistry()
{
// Register getters and setters for all settings
// This registry pattern allows adding new settings without modifying setSetting()
// Using macros to reduce repetition

// Helper macros
#define REGISTER_STRING_SETTING(name, getter, setter)                                                                  \
    m_getters[QStringLiteral(name)] = [this]() {                                                                       \
        return m_settings->getter();                                                                                   \
    };                                                                                                                 \
    m_setters[QStringLiteral(name)] = [this](const QVariant& v) {                                                      \
        m_settings->setter(v.toString());                                                                              \
        return true;                                                                                                   \
    };

#define REGISTER_BOOL_SETTING(name, getter, setter)                                                                    \
    m_getters[QStringLiteral(name)] = [this]() {                                                                       \
        return m_settings->getter();                                                                                   \
    };                                                                                                                 \
    m_setters[QStringLiteral(name)] = [this](const QVariant& v) {                                                      \
        m_settings->setter(v.toBool());                                                                                \
        return true;                                                                                                   \
    };

#define REGISTER_INT_SETTING(name, getter, setter)                                                                     \
    m_getters[QStringLiteral(name)] = [this]() {                                                                       \
        return m_settings->getter();                                                                                   \
    };                                                                                                                 \
    m_setters[QStringLiteral(name)] = [this](const QVariant& v) {                                                      \
        m_settings->setter(v.toInt());                                                                                 \
        return true;                                                                                                   \
    };

#define REGISTER_DOUBLE_SETTING(name, getter, setter)                                                                  \
    m_getters[QStringLiteral(name)] = [this]() {                                                                       \
        return m_settings->getter();                                                                                   \
    };                                                                                                                 \
    m_setters[QStringLiteral(name)] = [this](const QVariant& v) {                                                      \
        m_settings->setter(v.toDouble());                                                                              \
        return true;                                                                                                   \
    };

#define REGISTER_COLOR_SETTING(keyName, getter, setter)                                                                \
    m_getters[QStringLiteral(keyName)] = [this]() {                                                                    \
        QColor color = m_settings->getter();                                                                           \
        return color.name(QColor::HexArgb);                                                                            \
    };                                                                                                                 \
    m_setters[QStringLiteral(keyName)] = [this](const QVariant& v) {                                                   \
        m_settings->setter(QColor(v.toString()));                                                                      \
        return true;                                                                                                   \
    };

#define REGISTER_STRINGLIST_SETTING(name, getter, setter)                                                              \
    m_getters[QStringLiteral(name)] = [this]() {                                                                       \
        return m_settings->getter();                                                                                   \
    };                                                                                                                 \
    m_setters[QStringLiteral(name)] = [this](const QVariant& v) {                                                      \
        m_settings->setter(v.toStringList());                                                                          \
        return true;                                                                                                   \
    };

    // Activation settings
    REGISTER_BOOL_SETTING("shiftDragToActivate", shiftDragToActivate, setShiftDragToActivate) // Deprecated

    // New modifier settings (enum as int)
    m_getters[QStringLiteral("dragActivationModifier")] = [this]() {
        return static_cast<int>(m_settings->dragActivationModifier());
    };
    m_setters[QStringLiteral("dragActivationModifier")] = [this](const QVariant& v) {
        int mod = v.toInt();
        if (mod >= 0 && mod <= static_cast<int>(DragModifier::CtrlAltMeta)) {
            m_settings->setDragActivationModifier(static_cast<DragModifier>(mod));
            return true;
        }
        return false;
    };

    // Skip-snap modifier: hold this key to move window without snapping
    m_getters[QStringLiteral("skipSnapModifier")] = [this]() {
        return static_cast<int>(m_settings->skipSnapModifier());
    };
    m_setters[QStringLiteral("skipSnapModifier")] = [this](const QVariant& v) {
        int mod = v.toInt();
        if (mod >= 0 && mod <= static_cast<int>(DragModifier::AlwaysActive)) {
            m_settings->setSkipSnapModifier(static_cast<DragModifier>(mod));
            return true;
        }
        return false;
    };

    // Multi-zone modifier: hold this key to span windows across multiple zones
    m_getters[QStringLiteral("multiZoneModifier")] = [this]() {
        return static_cast<int>(m_settings->multiZoneModifier());
    };
    m_setters[QStringLiteral("multiZoneModifier")] = [this](const QVariant& v) {
        int mod = v.toInt();
        if (mod >= 0 && mod <= static_cast<int>(DragModifier::CtrlAltMeta)) {
            m_settings->setMultiZoneModifier(static_cast<DragModifier>(mod));
            return true;
        }
        return false;
    };

    REGISTER_BOOL_SETTING("middleClickMultiZone", middleClickMultiZone, setMiddleClickMultiZone)

    // Display settings
    REGISTER_BOOL_SETTING("showZonesOnAllMonitors", showZonesOnAllMonitors, setShowZonesOnAllMonitors)
    REGISTER_BOOL_SETTING("showZoneNumbers", showZoneNumbers, setShowZoneNumbers)
    REGISTER_BOOL_SETTING("flashZonesOnSwitch", flashZonesOnSwitch, setFlashZonesOnSwitch)
    REGISTER_BOOL_SETTING("showOsdOnLayoutSwitch", showOsdOnLayoutSwitch, setShowOsdOnLayoutSwitch)

    // Appearance settings
    REGISTER_BOOL_SETTING("useSystemColors", useSystemColors, setUseSystemColors)
    REGISTER_COLOR_SETTING("highlightColor", highlightColor, setHighlightColor)
    REGISTER_COLOR_SETTING("inactiveColor", inactiveColor, setInactiveColor)
    REGISTER_COLOR_SETTING("borderColor", borderColor, setBorderColor)
    REGISTER_COLOR_SETTING("numberColor", numberColor, setNumberColor)
    REGISTER_DOUBLE_SETTING("activeOpacity", activeOpacity, setActiveOpacity)
    REGISTER_DOUBLE_SETTING("inactiveOpacity", inactiveOpacity, setInactiveOpacity)
    REGISTER_INT_SETTING("borderWidth", borderWidth, setBorderWidth)
    REGISTER_INT_SETTING("borderRadius", borderRadius, setBorderRadius)
    REGISTER_BOOL_SETTING("enableBlur", enableBlur, setEnableBlur)
    REGISTER_BOOL_SETTING("enableShaderEffects", enableShaderEffects, setEnableShaderEffects)
    REGISTER_INT_SETTING("shaderFrameRate", shaderFrameRate, setShaderFrameRate)

    // Zone settings
    REGISTER_INT_SETTING("zonePadding", zonePadding, setZonePadding)
    REGISTER_INT_SETTING("outerGap", outerGap, setOuterGap)
    REGISTER_INT_SETTING("adjacentThreshold", adjacentThreshold, setAdjacentThreshold)
    REGISTER_INT_SETTING("pollIntervalMs", pollIntervalMs, setPollIntervalMs)
    REGISTER_INT_SETTING("minimumZoneSizePx", minimumZoneSizePx, setMinimumZoneSizePx)
    REGISTER_INT_SETTING("minimumZoneDisplaySizePx", minimumZoneDisplaySizePx, setMinimumZoneDisplaySizePx)

    // Behavior settings
    REGISTER_BOOL_SETTING("keepWindowsInZonesOnResolutionChange", keepWindowsInZonesOnResolutionChange,
                          setKeepWindowsInZonesOnResolutionChange)
    REGISTER_BOOL_SETTING("moveNewWindowsToLastZone", moveNewWindowsToLastZone, setMoveNewWindowsToLastZone)
    REGISTER_BOOL_SETTING("restoreOriginalSizeOnUnsnap", restoreOriginalSizeOnUnsnap, setRestoreOriginalSizeOnUnsnap)

    // Exclusions
    REGISTER_STRINGLIST_SETTING("excludedApplications", excludedApplications, setExcludedApplications)
    REGISTER_STRINGLIST_SETTING("excludedWindowClasses", excludedWindowClasses, setExcludedWindowClasses)
    REGISTER_BOOL_SETTING("excludeTransientWindows", excludeTransientWindows, setExcludeTransientWindows)
    REGISTER_INT_SETTING("minimumWindowWidth", minimumWindowWidth, setMinimumWindowWidth)
    REGISTER_INT_SETTING("minimumWindowHeight", minimumWindowHeight, setMinimumWindowHeight)

    // Autotile Animation Settings (KWin effect visual transitions)
    REGISTER_BOOL_SETTING("autotileAnimationsEnabled", autotileAnimationsEnabled, setAutotileAnimationsEnabled)
    REGISTER_INT_SETTING("autotileAnimationDuration", autotileAnimationDuration, setAutotileAnimationDuration)

    // Core Autotiling Settings
    REGISTER_BOOL_SETTING("autotileEnabled", autotileEnabled, setAutotileEnabled)
    REGISTER_STRING_SETTING("autotileAlgorithm", autotileAlgorithm, setAutotileAlgorithm)
    REGISTER_DOUBLE_SETTING("autotileSplitRatio", autotileSplitRatio, setAutotileSplitRatio)
    REGISTER_INT_SETTING("autotileMasterCount", autotileMasterCount, setAutotileMasterCount)
    REGISTER_INT_SETTING("autotileInnerGap", autotileInnerGap, setAutotileInnerGap)
    REGISTER_INT_SETTING("autotileOuterGap", autotileOuterGap, setAutotileOuterGap)
    REGISTER_BOOL_SETTING("autotileFocusNewWindows", autotileFocusNewWindows, setAutotileFocusNewWindows)
    REGISTER_BOOL_SETTING("autotileSmartGaps", autotileSmartGaps, setAutotileSmartGaps)

    // Additional Autotiling Settings
    REGISTER_BOOL_SETTING("autotileFocusFollowsMouse", autotileFocusFollowsMouse, setAutotileFocusFollowsMouse)
    REGISTER_BOOL_SETTING("autotileRespectMinimumSize", autotileRespectMinimumSize, setAutotileRespectMinimumSize)
    REGISTER_BOOL_SETTING("autotileShowActiveBorder", autotileShowActiveBorder, setAutotileShowActiveBorder)
    REGISTER_INT_SETTING("autotileActiveBorderWidth", autotileActiveBorderWidth, setAutotileActiveBorderWidth)
    REGISTER_BOOL_SETTING("autotileUseSystemBorderColor", autotileUseSystemBorderColor, setAutotileUseSystemBorderColor)
    REGISTER_COLOR_SETTING("autotileActiveBorderColor", autotileActiveBorderColor, setAutotileActiveBorderColor)
    REGISTER_BOOL_SETTING("autotileMonocleHideOthers", autotileMonocleHideOthers, setAutotileMonocleHideOthers)
    REGISTER_BOOL_SETTING("autotileMonocleShowTabs", autotileMonocleShowTabs, setAutotileMonocleShowTabs)

// Clean up macros (local scope)
#undef REGISTER_STRING_SETTING
#undef REGISTER_BOOL_SETTING
#undef REGISTER_INT_SETTING
#undef REGISTER_DOUBLE_SETTING
#undef REGISTER_COLOR_SETTING
#undef REGISTER_STRINGLIST_SETTING
}

void SettingsAdaptor::reloadSettings()
{
    m_settings->load();
}

void SettingsAdaptor::saveSettings()
{
    m_settings->save();
}

void SettingsAdaptor::resetToDefaults()
{
    m_settings->reset();
}

QString SettingsAdaptor::getAllSettings()
{
    QJsonObject settings;
    for (auto it = m_getters.constBegin(); it != m_getters.constEnd(); ++it) {
        settings[it.key()] = QJsonValue::fromVariant(it.value()());
    }
    return QString::fromUtf8(QJsonDocument(settings).toJson());
}

QDBusVariant SettingsAdaptor::getSetting(const QString& key)
{
    if (key.isEmpty()) {
        qCWarning(lcDbusSettings) << "Cannot get setting - empty key";
        // Return a valid but empty QDBusVariant to avoid marshalling errors
        // (QDBusVariant() with no argument creates an invalid variant that can't be sent)
        return QDBusVariant(QVariant(QString()));
    }

    auto it = m_getters.find(key);
    if (it != m_getters.end()) {
        QVariant value = it.value()();
        // Ensure we never return an invalid variant - use empty string as fallback
        if (!value.isValid()) {
            qCWarning(lcDbusSettings) << "Setting" << key << "returned invalid variant, using empty string";
            return QDBusVariant(QVariant(QString()));
        }
        return QDBusVariant(value);
    }

    qCWarning(lcDbusSettings) << "Setting key not found:" << key;
    // Return a valid but empty QDBusVariant with error indicator
    // Callers should check for empty string as "not found" indicator
    return QDBusVariant(QVariant(QString()));
}

bool SettingsAdaptor::setSetting(const QString& key, const QDBusVariant& value)
{
    if (key.isEmpty()) {
        qCWarning(lcDbusSettings) << "Cannot set setting - empty key";
        return false;
    }

    auto it = m_setters.find(key);
    if (it != m_setters.end()) {
        bool result = it.value()(value.variant());
        if (result) {
            // Use debounced save instead of immediate save (performance optimization)
            // This batches multiple rapid setting changes into a single disk write
            scheduleSave();
            qCDebug(lcDbusSettings) << "Setting" << key << "updated, save scheduled";
        } else {
            qCWarning(lcDbusSettings) << "Failed to set setting:" << key;
        }
        return result;
    }

    qCWarning(lcDbusSettings) << "Setting key not found:" << key;
    return false;
}

QStringList SettingsAdaptor::getSettingKeys()
{
    return m_getters.keys();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Shader Registry D-Bus Methods
// ═══════════════════════════════════════════════════════════════════════════════

QVariantList SettingsAdaptor::availableShaders()
{
    auto* registry = ShaderRegistry::instance();
    return registry ? registry->availableShadersVariant() : QVariantList();
}

QVariantMap SettingsAdaptor::shaderInfo(const QString& shaderId)
{
    auto* registry = ShaderRegistry::instance();
    return registry ? registry->shaderInfo(shaderId) : QVariantMap();
}

QVariantMap SettingsAdaptor::defaultShaderParams(const QString& shaderId)
{
    auto* registry = ShaderRegistry::instance();
    return registry ? registry->defaultParams(shaderId) : QVariantMap();
}

bool SettingsAdaptor::shadersEnabled()
{
    auto* registry = ShaderRegistry::instance();
    return registry ? registry->shadersEnabled() : false;
}

bool SettingsAdaptor::userShadersEnabled()
{
    auto* registry = ShaderRegistry::instance();
    return registry ? registry->userShadersEnabled() : false;
}

QString SettingsAdaptor::userShaderDirectory()
{
    auto* registry = ShaderRegistry::instance();
    return registry ? registry->userShaderDirectory() : QString();
}

void SettingsAdaptor::openUserShaderDirectory()
{
    auto* registry = ShaderRegistry::instance();
    if (registry) {
        registry->openUserShaderDirectory();
    }
}

void SettingsAdaptor::refreshShaders()
{
    auto* registry = ShaderRegistry::instance();
    if (registry) {
        registry->refresh();
    }
}

} // namespace PlasmaZones
