// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "settings.h"
#include "colorimporter.h"
#include "configdefaults.h"
#include "configbackends.h"
#include "perscreenresolver.h"
#include "settingsschema.h"

#include <PhosphorConfig/JsonBackend.h>
#include "../core/constants.h"
#include "../core/logging.h"
#include "../core/utils.h"
#include <QGuiApplication>
#include <QMetaMethod>
#include <QMetaProperty>
#include <QPalette>
#include <QFile>
#include <QTextStream>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QUuid>
#include "../autotile/AlgorithmRegistry.h"
#include <climits> // For INT_MAX in readValidatedInt

namespace PlasmaZones {

// ── Constructor ──────────────────────────────────────────────────────────────

Settings::Settings(QObject* parent)
    : ISettings(parent)
    , m_ownedBackend(createDefaultConfigBackend())
    , m_configBackend(m_ownedBackend.get())
    , m_store(std::make_unique<PhosphorConfig::Store>(m_configBackend, buildSettingsSchema(), this))
{
    load();
}

Settings::Settings(PhosphorConfig::IBackend* backend, QObject* parent)
    : ISettings(parent)
    , m_configBackend(backend)
    , m_store(std::make_unique<PhosphorConfig::Store>(m_configBackend, buildSettingsSchema(), this))
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

int Settings::readValidatedInt(PhosphorConfig::IGroup& group, const QString& key, int defaultValue, int min, int max,
                               const char* settingName)
{
    int value = group.readInt(key, defaultValue);
    if (value < min || value > max) {
        qCWarning(lcConfig) << settingName << ":" << value << "invalid, using default (must be" << min << "-" << max
                            << ")";
        value = defaultValue;
    }
    return value;
}

QColor Settings::readValidatedColor(PhosphorConfig::IGroup& group, const QString& key, const QColor& defaultValue,
                                    const char* settingName)
{
    QColor color = group.readColor(key, defaultValue);
    if (!color.isValid()) {
        qCWarning(lcConfig) << settingName << "color: invalid, using default";
        color = defaultValue;
    }
    return color;
}

void Settings::loadIndexedShortcuts(PhosphorConfig::IGroup& group, const QString& keyPattern, QString (&shortcuts)[9],
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
            trigger[ConfigDefaults::triggerModifierField()] =
                obj.value(ConfigDefaults::triggerModifierField()).toInt(0);
            trigger[ConfigDefaults::triggerMouseButtonField()] =
                obj.value(ConfigDefaults::triggerMouseButtonField()).toInt(0);
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

void Settings::saveTriggerList(PhosphorConfig::IGroup& group, const QString& key, const QVariantList& triggers)
{
    QJsonArray arr;
    for (const QVariant& t : triggers) {
        auto map = t.toMap();
        QJsonObject obj;
        obj[ConfigDefaults::triggerModifierField()] = map.value(ConfigDefaults::triggerModifierField(), 0).toInt();
        obj[ConfigDefaults::triggerMouseButtonField()] =
            map.value(ConfigDefaults::triggerMouseButtonField(), 0).toInt();
        arr.append(obj);
    }
    group.writeString(key, QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
}

// ── load() dispatcher ────────────────────────────────────────────────────────

void Settings::load()
{
    m_configBackend->reparseConfiguration();

    // Snapshot every Q_PROPERTY declared on Settings (skipping inherited
    // QObject properties like objectName) that has a NOTIFY signal, so we
    // can re-emit the matching NOTIFY signal after load() sets members
    // directly. Without these emits QML bindings bound to, say,
    // `settings.borderWidth` would not update when the discard path
    // reloads the on-disk values.
    //
    // INVARIANT: every Q_PROPERTY on Settings must hold a type whose
    // QVariant comparison is meaningful (Qt builtins, POD enums,
    // QVariantMap, QStringList). Custom Q_GADGETs without a registered
    // operator== will silently miscompare here.
    const QMetaObject* mo = metaObject();
    const int propCount = mo->propertyCount();
    const int firstOwnProp = QObject::staticMetaObject.propertyCount();
    QVector<QVariant> snapshot;
    snapshot.resize(propCount);
    for (int i = firstOwnProp; i < propCount; ++i) {
        const QMetaProperty prop = mo->property(i);
        if (prop.hasNotifySignal() && prop.isReadable())
            snapshot[i] = prop.read(this);
    }

    loadActivationConfig(m_configBackend);
    loadDisplayConfig(m_configBackend);
    // Appearance: backed by m_store — nothing to load here.
    loadZoneGeometryConfig(m_configBackend);
    loadBehaviorConfig(m_configBackend);
    loadZoneSelectorConfig(m_configBackend);
    loadPerScreenOverrides(m_configBackend);
    loadVirtualScreenConfigs(m_configBackend);

    // Rendering backend is backed by m_store — nothing to load here.

    // Shaders is backed by m_store — nothing to load here. Getters read
    // through the store on demand, with the schema's validator clamping
    // FrameRate and BarCount to their configured ranges.

    loadShortcutConfig(m_configBackend);
    loadAutotilingConfig(m_configBackend);
    loadEditorConfig(m_configBackend);

    // Ordering is backed by m_store — getters parse the comma-joined wire
    // format on demand.

    if (useSystemColors()) {
        applySystemColorScheme();
    }
    if (m_autotileUseSystemBorderColors) {
        applyAutotileBorderSystemColor();
    }

    qCInfo(lcConfig) << "Settings loaded";
    Q_EMIT settingsChanged();

    // Emit NOTIFY signals for every Q_PROPERTY whose value changed. load()
    // sets members directly (not via setters), so without this loop QML
    // bindings would never see reloaded values after discard / reset.
    for (int i = firstOwnProp; i < propCount; ++i) {
        const QMetaProperty prop = mo->property(i);
        if (!prop.hasNotifySignal() || !prop.isReadable())
            continue;
        const QVariant newValue = prop.read(this);
        if (newValue != snapshot[i]) {
            const QMetaMethod notify = prop.notifySignal();
            notify.invoke(this, Qt::DirectConnection);
        }
    }
}

// ── save() dispatcher ────────────────────────────────────────────────────────

// Groups that save() writes exhaustively — shared by reset().
// Does NOT include unmanaged groups (TilingQuickLayoutSlots, Updates) which are
// written independently and must survive a normal save.
QStringList Settings::managedGroupNames()
{
    return {
        ConfigDefaults::generalGroup(), // "General"
        ConfigDefaults::snappingGroup(), // "Snapping"
        ConfigDefaults::tilingGroup(), // "Tiling"
        ConfigDefaults::exclusionsGroup(), // "Exclusions"
        ConfigDefaults::performanceGroup(), // "Performance"
        ConfigDefaults::renderingGroup(), // "Rendering"
        ConfigDefaults::shadersGroup(), // "Shaders"
        ConfigDefaults::shortcutsGroup(), // "Shortcuts" — covers Shortcuts.Global + Shortcuts.Tiling
        ConfigDefaults::animationsGroup(), // "Animations"
        ConfigDefaults::editorGroup(), // "Editor" — covers Editor.Shortcuts + Editor.Snapping + Editor.FillOnDrop
        ConfigDefaults::orderingGroup(), // "Ordering"
    };
}

void Settings::deletePerScreenGroups(PhosphorConfig::IBackend* backend)
{
    const QStringList allGroups = backend->groupList();
    for (const QString& groupName : allGroups) {
        if (PerScreenPathResolver::isPerScreenPrefix(groupName)
            || groupName.startsWith(ConfigDefaults::virtualScreenGroupPrefix())) {
            backend->deleteGroup(groupName);
        }
    }
}

void Settings::purgeStaleKeys()
{
    // Root-level groups that must survive a save() cycle — written
    // independently of Settings::save(). Assignment:* and QuickLayouts
    // live in assignments.json and aren't seen here.
    const QStringList preservedGroups = {
        ConfigDefaults::tilingQuickLayoutSlotsGroup(),
        ConfigDefaults::updatesGroup(),
    };

    // Compute the set of paths the Store claims. These must not be
    // blanket-deleted because Store::write has already persisted authoritative
    // values and no subsequent save*Config call will rewrite them. Their
    // ancestor paths ("Snapping" for "Snapping.Appearance.Colors") must also
    // survive as intermediate JSON nodes.
    const auto& schema = m_store->schema();
    QSet<QString> storeGroups;
    QSet<QString> storeAncestors;
    for (auto it = schema.groups.constBegin(); it != schema.groups.constEnd(); ++it) {
        const QString& groupName = it.key();
        storeGroups.insert(groupName);
        const QStringList segments = groupName.split(QLatin1Char('.'), Qt::SkipEmptyParts);
        QString ancestor;
        for (int i = 0; i + 1 < segments.size(); ++i) {
            ancestor = ancestor.isEmpty() ? segments[i] : (ancestor + QLatin1Char('.') + segments[i]);
            storeAncestors.insert(ancestor);
        }
    }

    auto isStoreClaimed = [&](const QString& group) {
        return storeGroups.contains(group) || storeAncestors.contains(group);
    };

    // Pass 1: Store-declared groups get per-key purging — keep declared keys,
    // delete everything else. This preserves the authoritative values while
    // still evicting anything left over from a renamed or removed key.
    for (auto it = schema.groups.constBegin(); it != schema.groups.constEnd(); ++it) {
        const QString& groupName = it.key();
        QSet<QString> declared;
        for (const auto& def : it.value()) {
            declared.insert(def.key);
        }
        auto g = m_configBackend->group(groupName);
        // hasKey() doesn't expose an iterator, so probe the keys we found
        // when the group is enumerated as a JSON leaf (flat children).
        const QJsonObject snapshot = dynamic_cast<PhosphorConfig::JsonBackend*>(m_configBackend)->jsonRootSnapshot();
        QJsonObject cursor = snapshot;
        for (const QString& seg : groupName.split(QLatin1Char('.'), Qt::SkipEmptyParts)) {
            cursor = cursor.value(seg).toObject();
        }
        for (auto keyIt = cursor.constBegin(); keyIt != cursor.constEnd(); ++keyIt) {
            if (keyIt.value().isObject()) {
                continue; // sub-group, not a leaf key
            }
            if (!declared.contains(keyIt.key())) {
                g->deleteKey(keyIt.key());
            }
        }
    }

    // Pass 2: anything else in the backend that isn't preserved, isn't
    // claimed by the Store, and isn't a per-screen / virtual-screen group
    // gets blanket-deleted. save*Config runs next and rewrites unmigrated
    // managed groups from their cached members.
    QSet<QString> deleted;
    for (const QString& groupName : m_configBackend->groupList()) {
        if (PerScreenPathResolver::isPerScreenPrefix(groupName)) {
            continue;
        }
        if (groupName.startsWith(ConfigDefaults::virtualScreenGroupPrefix())) {
            continue;
        }
        if (isStoreClaimed(groupName)) {
            continue;
        }
        const int dotIdx = groupName.indexOf(QLatin1Char('.'));
        const QString topLevel = (dotIdx >= 0) ? groupName.left(dotIdx) : groupName;
        if (deleted.contains(topLevel) || preservedGroups.contains(topLevel)) {
            continue;
        }
        // When the top-level itself is a Store ancestor, we can't delete
        // the whole thing — descend and delete only the non-claimed child.
        if (isStoreClaimed(topLevel)) {
            m_configBackend->deleteGroup(groupName);
        } else {
            m_configBackend->deleteGroup(topLevel);
            deleted.insert(topLevel);
        }
    }
}

void Settings::save()
{
    purgeStaleKeys();

    saveActivationConfig(m_configBackend);
    saveDisplayConfig(m_configBackend);
    // Appearance: backed by m_store — writes persisted via setters.
    saveZoneGeometryConfig(m_configBackend);
    saveBehaviorConfig(m_configBackend);
    saveZoneSelectorConfig(m_configBackend);
    saveAllPerScreenOverrides(m_configBackend);
    saveVirtualScreenConfigs(m_configBackend);
    saveShortcutConfig(m_configBackend);
    saveAutotilingConfig(m_configBackend);
    // Ordering is backed by m_store — setters persist immediately.
    saveEditorConfig(m_configBackend);

    // Rendering backend is backed by m_store — setter persists directly.
    // Shaders is backed by m_store — setters write to the backend
    // immediately, so save() only needs to flush alongside everything else.

    m_configBackend->sync();
}

// ── Shaders (PhosphorConfig::Store-backed) ──────────────────────────────────

bool Settings::enableShaderEffects() const
{
    return m_store->read<bool>(ConfigDefaults::shadersGroup(), ConfigDefaults::enabledKey());
}

void Settings::setEnableShaderEffects(bool enable)
{
    if (enableShaderEffects() == enable) {
        return;
    }
    m_store->write(ConfigDefaults::shadersGroup(), ConfigDefaults::enabledKey(), enable);
    Q_EMIT enableShaderEffectsChanged();
    Q_EMIT settingsChanged();
}

int Settings::shaderFrameRate() const
{
    return m_store->read<int>(ConfigDefaults::shadersGroup(), ConfigDefaults::frameRateKey());
}

void Settings::setShaderFrameRate(int fps)
{
    // Schema validator clamps on write, so a read-back after the write gives
    // the canonical value even if the caller passed something out of range.
    const int before = shaderFrameRate();
    m_store->write(ConfigDefaults::shadersGroup(), ConfigDefaults::frameRateKey(), fps);
    if (shaderFrameRate() == before) {
        return;
    }
    Q_EMIT shaderFrameRateChanged();
    Q_EMIT settingsChanged();
}

bool Settings::enableAudioVisualizer() const
{
    return m_store->read<bool>(ConfigDefaults::shadersGroup(), ConfigDefaults::audioVisualizerKey());
}

void Settings::setEnableAudioVisualizer(bool enable)
{
    if (enableAudioVisualizer() == enable) {
        return;
    }
    m_store->write(ConfigDefaults::shadersGroup(), ConfigDefaults::audioVisualizerKey(), enable);
    Q_EMIT enableAudioVisualizerChanged();
    Q_EMIT settingsChanged();
}

int Settings::audioSpectrumBarCount() const
{
    return m_store->read<int>(ConfigDefaults::shadersGroup(), ConfigDefaults::audioSpectrumBarCountKey());
}

void Settings::setAudioSpectrumBarCount(int count)
{
    const int before = audioSpectrumBarCount();
    m_store->write(ConfigDefaults::shadersGroup(), ConfigDefaults::audioSpectrumBarCountKey(), count);
    if (audioSpectrumBarCount() == before) {
        return;
    }
    Q_EMIT audioSpectrumBarCountChanged();
    Q_EMIT settingsChanged();
}

// ── Store-backed getter/setter macros ───────────────────────────────────────
// Shared by every group migrated to PhosphorConfig::Store. Each macro
// expands to a mechanical "read through m_store / write through m_store,
// then check change before NOTIFY" implementation. Clamp / normalize
// behaviour lives in the schema validator, so the read-back after write
// gives the canonical value.
//
// These are local to settings.cpp and #undef'd at the bottom of the file.

#define PZ_STORE_GET(retType, fn, group, key, readType)                                                                \
    retType Settings::fn() const                                                                                       \
    {                                                                                                                  \
        return m_store->read<readType>(ConfigDefaults::group(), ConfigDefaults::key());                                \
    }

#define PZ_STORE_SET_BOOL(fn, group, key, signal)                                                                      \
    void Settings::fn(bool value)                                                                                      \
    {                                                                                                                  \
        if (m_store->read<bool>(ConfigDefaults::group(), ConfigDefaults::key()) == value) {                            \
            return;                                                                                                    \
        }                                                                                                              \
        m_store->write(ConfigDefaults::group(), ConfigDefaults::key(), value);                                         \
        Q_EMIT signal();                                                                                               \
        Q_EMIT settingsChanged();                                                                                      \
    }

#define PZ_STORE_SET_INT(fn, group, key, signal)                                                                       \
    void Settings::fn(int value)                                                                                       \
    {                                                                                                                  \
        const int before = m_store->read<int>(ConfigDefaults::group(), ConfigDefaults::key());                         \
        m_store->write(ConfigDefaults::group(), ConfigDefaults::key(), value);                                         \
        const int after = m_store->read<int>(ConfigDefaults::group(), ConfigDefaults::key());                          \
        if (after == before) {                                                                                         \
            return;                                                                                                    \
        }                                                                                                              \
        Q_EMIT signal();                                                                                               \
        Q_EMIT settingsChanged();                                                                                      \
    }

#define PZ_STORE_SET_DOUBLE(fn, group, key, signal)                                                                    \
    void Settings::fn(qreal value)                                                                                     \
    {                                                                                                                  \
        const qreal before = m_store->read<double>(ConfigDefaults::group(), ConfigDefaults::key());                    \
        m_store->write(ConfigDefaults::group(), ConfigDefaults::key(), value);                                         \
        const qreal after = m_store->read<double>(ConfigDefaults::group(), ConfigDefaults::key());                     \
        if (qFuzzyCompare(1.0 + before, 1.0 + after)) {                                                                \
            return;                                                                                                    \
        }                                                                                                              \
        Q_EMIT signal();                                                                                               \
        Q_EMIT settingsChanged();                                                                                      \
    }

#define PZ_STORE_SET_COLOR(fn, group, key, signal)                                                                     \
    void Settings::fn(const QColor& value)                                                                             \
    {                                                                                                                  \
        if (m_store->read<QColor>(ConfigDefaults::group(), ConfigDefaults::key()) == value) {                          \
            return;                                                                                                    \
        }                                                                                                              \
        m_store->write(ConfigDefaults::group(), ConfigDefaults::key(), value);                                         \
        Q_EMIT signal();                                                                                               \
        Q_EMIT settingsChanged();                                                                                      \
    }

#define PZ_STORE_SET_STRING(fn, group, key, signal)                                                                    \
    void Settings::fn(const QString& value)                                                                            \
    {                                                                                                                  \
        if (m_store->read<QString>(ConfigDefaults::group(), ConfigDefaults::key()) == value) {                         \
            return;                                                                                                    \
        }                                                                                                              \
        m_store->write(ConfigDefaults::group(), ConfigDefaults::key(), value);                                         \
        Q_EMIT signal();                                                                                               \
        Q_EMIT settingsChanged();                                                                                      \
    }

// ── Appearance (PhosphorConfig::Store-backed) ───────────────────────────────
// Colors group
PZ_STORE_GET(bool, useSystemColors, snappingAppearanceColorsGroup, useSystemKey, bool)
void Settings::setUseSystemColors(bool use)
{
    if (useSystemColors() == use) {
        return;
    }
    m_store->write(ConfigDefaults::snappingAppearanceColorsGroup(), ConfigDefaults::useSystemKey(), use);
    if (use) {
        applySystemColorScheme();
    }
    Q_EMIT useSystemColorsChanged();
    Q_EMIT settingsChanged();
}
PZ_STORE_GET(QColor, highlightColor, snappingAppearanceColorsGroup, highlightKey, QColor)
PZ_STORE_SET_COLOR(setHighlightColor, snappingAppearanceColorsGroup, highlightKey, highlightColorChanged)
PZ_STORE_GET(QColor, inactiveColor, snappingAppearanceColorsGroup, inactiveKey, QColor)
PZ_STORE_SET_COLOR(setInactiveColor, snappingAppearanceColorsGroup, inactiveKey, inactiveColorChanged)
PZ_STORE_GET(QColor, borderColor, snappingAppearanceColorsGroup, borderKey, QColor)
PZ_STORE_SET_COLOR(setBorderColor, snappingAppearanceColorsGroup, borderKey, borderColorChanged)

// Labels group
PZ_STORE_GET(QColor, labelFontColor, snappingAppearanceLabelsGroup, fontColorKey, QColor)
PZ_STORE_SET_COLOR(setLabelFontColor, snappingAppearanceLabelsGroup, fontColorKey, labelFontColorChanged)
PZ_STORE_GET(QString, labelFontFamily, snappingAppearanceLabelsGroup, fontFamilyKey, QString)
PZ_STORE_SET_STRING(setLabelFontFamily, snappingAppearanceLabelsGroup, fontFamilyKey, labelFontFamilyChanged)
PZ_STORE_GET(qreal, labelFontSizeScale, snappingAppearanceLabelsGroup, fontSizeScaleKey, double)
PZ_STORE_SET_DOUBLE(setLabelFontSizeScale, snappingAppearanceLabelsGroup, fontSizeScaleKey, labelFontSizeScaleChanged)
PZ_STORE_GET(int, labelFontWeight, snappingAppearanceLabelsGroup, fontWeightKey, int)
PZ_STORE_SET_INT(setLabelFontWeight, snappingAppearanceLabelsGroup, fontWeightKey, labelFontWeightChanged)
PZ_STORE_GET(bool, labelFontItalic, snappingAppearanceLabelsGroup, fontItalicKey, bool)
PZ_STORE_SET_BOOL(setLabelFontItalic, snappingAppearanceLabelsGroup, fontItalicKey, labelFontItalicChanged)
PZ_STORE_GET(bool, labelFontUnderline, snappingAppearanceLabelsGroup, fontUnderlineKey, bool)
PZ_STORE_SET_BOOL(setLabelFontUnderline, snappingAppearanceLabelsGroup, fontUnderlineKey, labelFontUnderlineChanged)
PZ_STORE_GET(bool, labelFontStrikeout, snappingAppearanceLabelsGroup, fontStrikeoutKey, bool)
PZ_STORE_SET_BOOL(setLabelFontStrikeout, snappingAppearanceLabelsGroup, fontStrikeoutKey, labelFontStrikeoutChanged)

// Opacity group
PZ_STORE_GET(qreal, activeOpacity, snappingAppearanceOpacityGroup, activeKey, double)
PZ_STORE_SET_DOUBLE(setActiveOpacity, snappingAppearanceOpacityGroup, activeKey, activeOpacityChanged)
PZ_STORE_GET(qreal, inactiveOpacity, snappingAppearanceOpacityGroup, inactiveKey, double)
PZ_STORE_SET_DOUBLE(setInactiveOpacity, snappingAppearanceOpacityGroup, inactiveKey, inactiveOpacityChanged)

// Border group
PZ_STORE_GET(int, borderWidth, snappingAppearanceBorderGroup, widthKey, int)
PZ_STORE_SET_INT(setBorderWidth, snappingAppearanceBorderGroup, widthKey, borderWidthChanged)
PZ_STORE_GET(int, borderRadius, snappingAppearanceBorderGroup, radiusKey, int)
PZ_STORE_SET_INT(setBorderRadius, snappingAppearanceBorderGroup, radiusKey, borderRadiusChanged)

// Effects group (blur lives here for historical reasons)
PZ_STORE_GET(bool, enableBlur, snappingEffectsGroup, blurKey, bool)
PZ_STORE_SET_BOOL(setEnableBlur, snappingEffectsGroup, blurKey, enableBlurChanged)

// ── Ordering (PhosphorConfig::Store-backed) ─────────────────────────────────
// On disk: comma-joined QString. In API: QStringList. The schema validator
// normalizes the canonical format (trim/dedup), so the round-trip through
// the store always produces the same string for any equivalent input.

namespace {
QStringList parseCommaList(const QString& raw)
{
    return raw.isEmpty() ? QStringList{} : raw.split(QLatin1Char(','));
}
} // namespace

QStringList Settings::snappingLayoutOrder() const
{
    return parseCommaList(
        m_store->read<QString>(ConfigDefaults::orderingGroup(), ConfigDefaults::snappingLayoutOrderKey()));
}

void Settings::setSnappingLayoutOrder(const QStringList& order)
{
    const QString before =
        m_store->read<QString>(ConfigDefaults::orderingGroup(), ConfigDefaults::snappingLayoutOrderKey());
    m_store->write(ConfigDefaults::orderingGroup(), ConfigDefaults::snappingLayoutOrderKey(),
                   order.join(QLatin1Char(',')));
    const QString after =
        m_store->read<QString>(ConfigDefaults::orderingGroup(), ConfigDefaults::snappingLayoutOrderKey());
    if (before == after) {
        return;
    }
    Q_EMIT snappingLayoutOrderChanged();
    Q_EMIT settingsChanged();
}

QStringList Settings::tilingAlgorithmOrder() const
{
    return parseCommaList(
        m_store->read<QString>(ConfigDefaults::orderingGroup(), ConfigDefaults::tilingAlgorithmOrderKey()));
}

void Settings::setTilingAlgorithmOrder(const QStringList& order)
{
    const QString before =
        m_store->read<QString>(ConfigDefaults::orderingGroup(), ConfigDefaults::tilingAlgorithmOrderKey());
    m_store->write(ConfigDefaults::orderingGroup(), ConfigDefaults::tilingAlgorithmOrderKey(),
                   order.join(QLatin1Char(',')));
    const QString after =
        m_store->read<QString>(ConfigDefaults::orderingGroup(), ConfigDefaults::tilingAlgorithmOrderKey());
    if (before == after) {
        return;
    }
    Q_EMIT tilingAlgorithmOrderChanged();
    Q_EMIT settingsChanged();
}

// ── Animations (PhosphorConfig::Store-backed) ───────────────────────────────
// Snapping + autotile geometry-change transitions. Clamp validators enforce
// duration/min-distance/sequence-mode/stagger-interval ranges uniformly.

PZ_STORE_GET(bool, animationsEnabled, animationsGroup, enabledKey, bool)
PZ_STORE_SET_BOOL(setAnimationsEnabled, animationsGroup, enabledKey, animationsEnabledChanged)
PZ_STORE_GET(int, animationDuration, animationsGroup, durationKey, int)
PZ_STORE_SET_INT(setAnimationDuration, animationsGroup, durationKey, animationDurationChanged)
PZ_STORE_GET(QString, animationEasingCurve, animationsGroup, easingCurveKey, QString)
PZ_STORE_SET_STRING(setAnimationEasingCurve, animationsGroup, easingCurveKey, animationEasingCurveChanged)
PZ_STORE_GET(int, animationMinDistance, animationsGroup, minDistanceKey, int)
PZ_STORE_SET_INT(setAnimationMinDistance, animationsGroup, minDistanceKey, animationMinDistanceChanged)
PZ_STORE_GET(int, animationSequenceMode, animationsGroup, sequenceModeKey, int)
PZ_STORE_SET_INT(setAnimationSequenceMode, animationsGroup, sequenceModeKey, animationSequenceModeChanged)
PZ_STORE_GET(int, animationStaggerInterval, animationsGroup, staggerIntervalKey, int)
PZ_STORE_SET_INT(setAnimationStaggerInterval, animationsGroup, staggerIntervalKey, animationStaggerIntervalChanged)

// ── Rendering (PhosphorConfig::Store-backed) ────────────────────────────────
// Validator (normalizeRenderingBackend in the schema) coerces unknown values
// to a known backend, so a hand-edited "Rendering.Backend = foobar" reads
// back as the default on next load.

PZ_STORE_GET(QString, renderingBackend, renderingGroup, backendKey, QString)
PZ_STORE_SET_STRING(setRenderingBackend, renderingGroup, backendKey, renderingBackendChanged)

// ── Performance (PhosphorConfig::Store-backed) ──────────────────────────────

PZ_STORE_GET(int, pollIntervalMs, performanceGroup, pollIntervalMsKey, int)
PZ_STORE_SET_INT(setPollIntervalMs, performanceGroup, pollIntervalMsKey, pollIntervalMsChanged)
PZ_STORE_GET(int, minimumZoneSizePx, performanceGroup, minimumZoneSizePxKey, int)
PZ_STORE_SET_INT(setMinimumZoneSizePx, performanceGroup, minimumZoneSizePxKey, minimumZoneSizePxChanged)
PZ_STORE_GET(int, minimumZoneDisplaySizePx, performanceGroup, minimumZoneDisplaySizePxKey, int)
PZ_STORE_SET_INT(setMinimumZoneDisplaySizePx, performanceGroup, minimumZoneDisplaySizePxKey,
                 minimumZoneDisplaySizePxChanged)

// ── Zone geometry (PhosphorConfig::Store-backed) ────────────────────────────
// Inner/outer gaps (uniform + per-side) plus adjacency threshold. Schema
// clampInt validators enforce the same ranges readValidatedInt used to.

PZ_STORE_GET(int, zonePadding, snappingGapsGroup, innerKey, int)
PZ_STORE_SET_INT(setZonePadding, snappingGapsGroup, innerKey, zonePaddingChanged)
PZ_STORE_GET(int, outerGap, snappingGapsGroup, outerKey, int)
PZ_STORE_SET_INT(setOuterGap, snappingGapsGroup, outerKey, outerGapChanged)
PZ_STORE_GET(bool, usePerSideOuterGap, snappingGapsGroup, usePerSideKey, bool)
PZ_STORE_SET_BOOL(setUsePerSideOuterGap, snappingGapsGroup, usePerSideKey, usePerSideOuterGapChanged)
PZ_STORE_GET(int, outerGapTop, snappingGapsGroup, topKey, int)
PZ_STORE_SET_INT(setOuterGapTop, snappingGapsGroup, topKey, outerGapTopChanged)
PZ_STORE_GET(int, outerGapBottom, snappingGapsGroup, bottomKey, int)
PZ_STORE_SET_INT(setOuterGapBottom, snappingGapsGroup, bottomKey, outerGapBottomChanged)
PZ_STORE_GET(int, outerGapLeft, snappingGapsGroup, leftKey, int)
PZ_STORE_SET_INT(setOuterGapLeft, snappingGapsGroup, leftKey, outerGapLeftChanged)
PZ_STORE_GET(int, outerGapRight, snappingGapsGroup, rightKey, int)
PZ_STORE_SET_INT(setOuterGapRight, snappingGapsGroup, rightKey, outerGapRightChanged)
PZ_STORE_GET(int, adjacentThreshold, snappingGapsGroup, adjacentThresholdKey, int)
PZ_STORE_SET_INT(setAdjacentThreshold, snappingGapsGroup, adjacentThresholdKey, adjacentThresholdChanged)

// ── reset / color helpers ────────────────────────────────────────────────────

void Settings::reset()
{
    // Delete all managed groups plus unmanaged groups (reset nukes everything)
    for (const QString& groupName : managedGroupNames()) {
        m_configBackend->deleteGroup(groupName);
    }
    m_configBackend->deleteGroup(ConfigDefaults::updatesGroup());
    m_configBackend->deleteGroup(ConfigDefaults::tilingQuickLayoutSlotsGroup());
    if (!QFile::remove(ConfigDefaults::sessionFilePath()) && QFile::exists(ConfigDefaults::sessionFilePath())) {
        qCWarning(lcConfig) << "Failed to remove session file:" << ConfigDefaults::sessionFilePath();
    }
    deletePerScreenGroups(m_configBackend);
    m_configBackend->sync();
    load();
    qCInfo(lcConfig) << "Settings reset to defaults";
}

// ── Editor setters ────────────────────────────────────────────────────────────

void Settings::setEditorDuplicateShortcut(const QString& shortcut)
{
    if (m_editorDuplicateShortcut != shortcut) {
        m_editorDuplicateShortcut = shortcut;
        Q_EMIT editorDuplicateShortcutChanged();
    }
}

void Settings::setEditorSplitHorizontalShortcut(const QString& shortcut)
{
    if (m_editorSplitHorizontalShortcut != shortcut) {
        m_editorSplitHorizontalShortcut = shortcut;
        Q_EMIT editorSplitHorizontalShortcutChanged();
    }
}

void Settings::setEditorSplitVerticalShortcut(const QString& shortcut)
{
    if (m_editorSplitVerticalShortcut != shortcut) {
        m_editorSplitVerticalShortcut = shortcut;
        Q_EMIT editorSplitVerticalShortcutChanged();
    }
}

void Settings::setEditorFillShortcut(const QString& shortcut)
{
    if (m_editorFillShortcut != shortcut) {
        m_editorFillShortcut = shortcut;
        Q_EMIT editorFillShortcutChanged();
    }
}

void Settings::setEditorGridSnappingEnabled(bool enabled)
{
    if (m_editorGridSnappingEnabled != enabled) {
        m_editorGridSnappingEnabled = enabled;
        Q_EMIT editorGridSnappingEnabledChanged();
    }
}

void Settings::setEditorEdgeSnappingEnabled(bool enabled)
{
    if (m_editorEdgeSnappingEnabled != enabled) {
        m_editorEdgeSnappingEnabled = enabled;
        Q_EMIT editorEdgeSnappingEnabledChanged();
    }
}

void Settings::setEditorSnapIntervalX(qreal interval)
{
    interval = qBound(0.01, interval, 1.0);
    if (!qFuzzyCompare(m_editorSnapIntervalX, interval)) {
        m_editorSnapIntervalX = interval;
        Q_EMIT editorSnapIntervalXChanged();
    }
}

void Settings::setEditorSnapIntervalY(qreal interval)
{
    interval = qBound(0.01, interval, 1.0);
    if (!qFuzzyCompare(m_editorSnapIntervalY, interval)) {
        m_editorSnapIntervalY = interval;
        Q_EMIT editorSnapIntervalYChanged();
    }
}

void Settings::setEditorSnapOverrideModifier(int mod)
{
    if (m_editorSnapOverrideModifier != mod) {
        m_editorSnapOverrideModifier = mod;
        Q_EMIT editorSnapOverrideModifierChanged();
    }
}

void Settings::setFillOnDropEnabled(bool enabled)
{
    if (m_fillOnDropEnabled != enabled) {
        m_fillOnDropEnabled = enabled;
        Q_EMIT fillOnDropEnabledChanged();
    }
}

void Settings::setFillOnDropModifier(int mod)
{
    if (m_fillOnDropModifier != mod) {
        m_fillOnDropModifier = mod;
        Q_EMIT fillOnDropModifierChanged();
    }
}

// ── TilingQuickLayoutSlots helpers ───────────────────────────────────────────

QString Settings::readTilingQuickLayoutSlot(int slotNumber) const
{
    if (slotNumber < 1 || slotNumber > 9)
        return {};
    // Config is already current from load() — no reparse needed per read.
    // The staged value check in getTilingQuickLayoutSlot() handles unsaved changes.
    auto group = m_configBackend->group(ConfigDefaults::tilingQuickLayoutSlotsGroup());
    return group->readString(QString::number(slotNumber));
}

void Settings::writeTilingQuickLayoutSlot(int slotNumber, const QString& layoutId)
{
    if (slotNumber < 1 || slotNumber > 9)
        return;
    auto group = m_configBackend->group(ConfigDefaults::tilingQuickLayoutSlotsGroup());
    group->writeString(QString::number(slotNumber), layoutId);
}

void Settings::syncConfig()
{
    m_configBackend->sync();
}

// ── Color helpers ────────────────────────────────────────────────────────────

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
    if (useSystemColors()) {
        setUseSystemColors(false);
    }
    return QString(); // Success - no error
}

void Settings::applySystemColorScheme()
{
    // QPalette respects QT_QPA_PLATFORMTHEME — on non-KDE desktops, Qt reads
    // the platform theme (qt6ct, gnome, lxqt) to populate the palette.
    const QPalette pal = QGuiApplication::palette();

    QColor highlight = pal.color(QPalette::Active, QPalette::Highlight);
    highlight.setAlpha(Defaults::HighlightAlpha);
    setHighlightColor(highlight);

    QColor inactive = pal.color(QPalette::Active, QPalette::Text);
    inactive.setAlpha(Defaults::InactiveAlpha);
    setInactiveColor(inactive);

    QColor border = pal.color(QPalette::Active, QPalette::Text);
    border.setAlpha(Defaults::BorderAlpha);
    setBorderColor(border);

    const QColor fontColor = pal.color(QPalette::Active, QPalette::Text);
    setLabelFontColor(fontColor);
}

void Settings::applyAutotileBorderSystemColor()
{
    // Use the exact snapping zone highlight/inactive colors including their alpha.
    const QColor hl = highlightColor();
    if (m_autotileBorderColor != hl) {
        m_autotileBorderColor = hl;
        Q_EMIT autotileBorderColorChanged();
    }

    const QColor ia = inactiveColor();
    if (m_autotileInactiveBorderColor != ia) {
        m_autotileInactiveBorderColor = ia;
        Q_EMIT autotileInactiveBorderColorChanged();
    }
}

#undef PZ_STORE_GET
#undef PZ_STORE_SET_BOOL
#undef PZ_STORE_SET_INT
#undef PZ_STORE_SET_DOUBLE
#undef PZ_STORE_SET_COLOR
#undef PZ_STORE_SET_STRING

} // namespace PlasmaZones
