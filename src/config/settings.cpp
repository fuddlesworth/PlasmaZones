// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "settings.h"
#include "colorimporter.h"
#include "configdefaults.h"
#include "configbackends.h"
#include "perscreenresolver.h"
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
{
    load();
}

Settings::Settings(PhosphorConfig::IBackend* backend, QObject* parent)
    : ISettings(parent)
    , m_configBackend(backend)
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
    loadAppearanceConfig(m_configBackend);
    loadZoneGeometryConfig(m_configBackend);
    loadBehaviorConfig(m_configBackend);
    loadZoneSelectorConfig(m_configBackend);
    loadPerScreenOverrides(m_configBackend);
    loadVirtualScreenConfigs(m_configBackend);

    // Rendering backend
    {
        auto rendering = m_configBackend->group(ConfigDefaults::renderingGroup());
        m_renderingBackend = ConfigDefaults::normalizeRenderingBackend(
            rendering->readString(ConfigDefaults::backendKey(), ConfigDefaults::renderingBackend()));
    }

    // Shaders (small enough to stay inline)
    {
        auto shaders = m_configBackend->group(ConfigDefaults::shadersGroup());
        m_enableShaderEffects = shaders->readBool(ConfigDefaults::enabledKey(), ConfigDefaults::enableShaderEffects());
        m_shaderFrameRate = qBound(ConfigDefaults::shaderFrameRateMin(),
                                   shaders->readInt(ConfigDefaults::frameRateKey(), ConfigDefaults::shaderFrameRate()),
                                   ConfigDefaults::shaderFrameRateMax());
        m_enableAudioVisualizer =
            shaders->readBool(ConfigDefaults::audioVisualizerKey(), ConfigDefaults::enableAudioVisualizer());
        m_audioSpectrumBarCount = qBound(
            ConfigDefaults::audioSpectrumBarCountMin(),
            shaders->readInt(ConfigDefaults::audioSpectrumBarCountKey(), ConfigDefaults::audioSpectrumBarCount()),
            ConfigDefaults::audioSpectrumBarCountMax());
    }

    loadShortcutConfig(m_configBackend);
    loadAutotilingConfig(m_configBackend);
    loadEditorConfig(m_configBackend);

    // Ordering (small enough to stay inline)
    {
        auto ordering = m_configBackend->group(ConfigDefaults::orderingGroup());
        auto parseOrderList = [](const QString& raw) -> QStringList {
            if (raw.isEmpty())
                return {};
            QStringList result = raw.split(QLatin1Char(','));
            for (auto& s : result)
                s = s.trimmed();
            result.removeAll(QString());
            result.removeDuplicates();
            return result;
        };
        QStringList newSnappingOrder = parseOrderList(ordering->readString(ConfigDefaults::snappingLayoutOrderKey()));
        if (m_snappingLayoutOrder != newSnappingOrder) {
            m_snappingLayoutOrder = newSnappingOrder;
            Q_EMIT snappingLayoutOrderChanged();
        }
        QStringList newTilingOrder = parseOrderList(ordering->readString(ConfigDefaults::tilingAlgorithmOrderKey()));
        if (m_tilingAlgorithmOrder != newTilingOrder) {
            m_tilingAlgorithmOrder = newTilingOrder;
            Q_EMIT tilingAlgorithmOrderChanged();
        }
    }

    if (m_useSystemColors) {
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
    // Root-level groups that must survive a save() cycle.
    // Everything else at the JSON root is considered stale and is deleted.
    // Assignment:* and QuickLayouts now live in assignments.json, so they
    // no longer need to be preserved here.
    const QStringList preservedGroups = {
        ConfigDefaults::tilingQuickLayoutSlotsGroup(), // written independently
        ConfigDefaults::updatesGroup(), // written independently
    };

    // Collect unique top-level group names from the backend, then delete any
    // that are not in the preserved set and are not per-screen groups.
    // Per-screen groups are handled by saveAllPerScreenOverrides().
    // Managed groups (Snapping, Tiling, …) are intentionally NOT preserved here
    // — save() rewrites them from scratch with only currently-valid keys.
    QSet<QString> deleted;
    for (const QString& groupName : m_configBackend->groupList()) {
        if (PerScreenPathResolver::isPerScreenPrefix(groupName)) {
            continue;
        }
        // VirtualScreen: groups are saved separately by saveVirtualScreenConfigs()
        // and must not be deleted here as a stale root-level group.
        if (groupName.startsWith(ConfigDefaults::virtualScreenGroupPrefix())) {
            continue;
        }
        const int dotIdx = groupName.indexOf(QLatin1Char('.'));
        const QString topLevel = (dotIdx >= 0) ? groupName.left(dotIdx) : groupName;
        if (deleted.contains(topLevel) || preservedGroups.contains(topLevel)) {
            continue;
        }
        m_configBackend->deleteGroup(topLevel);
        deleted.insert(topLevel);
    }
}

void Settings::save()
{
    purgeStaleKeys();

    saveActivationConfig(m_configBackend);
    saveDisplayConfig(m_configBackend);
    saveAppearanceConfig(m_configBackend);
    saveZoneGeometryConfig(m_configBackend);
    saveBehaviorConfig(m_configBackend);
    saveZoneSelectorConfig(m_configBackend);
    saveAllPerScreenOverrides(m_configBackend);
    saveVirtualScreenConfigs(m_configBackend);
    saveShortcutConfig(m_configBackend);
    saveAutotilingConfig(m_configBackend);
    {
        auto ordering = m_configBackend->group(ConfigDefaults::orderingGroup());
        ordering->writeString(ConfigDefaults::snappingLayoutOrderKey(), m_snappingLayoutOrder.join(QLatin1Char(',')));
        ordering->writeString(ConfigDefaults::tilingAlgorithmOrderKey(), m_tilingAlgorithmOrder.join(QLatin1Char(',')));
    }
    saveEditorConfig(m_configBackend);

    // Rendering backend
    {
        auto rendering = m_configBackend->group(ConfigDefaults::renderingGroup());
        rendering->writeString(ConfigDefaults::backendKey(), m_renderingBackend);
    }

    // Shader Effects
    {
        auto shaders = m_configBackend->group(ConfigDefaults::shadersGroup());
        shaders->writeBool(ConfigDefaults::enabledKey(), m_enableShaderEffects);
        shaders->writeInt(ConfigDefaults::frameRateKey(), m_shaderFrameRate);
        shaders->writeBool(ConfigDefaults::audioVisualizerKey(), m_enableAudioVisualizer);
        shaders->writeInt(ConfigDefaults::audioSpectrumBarCountKey(), m_audioSpectrumBarCount);
    }

    m_configBackend->sync();
}

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
    if (m_useSystemColors) {
        m_useSystemColors = false;
        Q_EMIT useSystemColorsChanged();
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
    if (m_highlightColor != highlight) {
        m_highlightColor = highlight;
        Q_EMIT highlightColorChanged();
    }

    QColor inactive = pal.color(QPalette::Active, QPalette::Text);
    inactive.setAlpha(Defaults::InactiveAlpha);
    if (m_inactiveColor != inactive) {
        m_inactiveColor = inactive;
        Q_EMIT inactiveColorChanged();
    }

    QColor border = pal.color(QPalette::Active, QPalette::Text);
    border.setAlpha(Defaults::BorderAlpha);
    if (m_borderColor != border) {
        m_borderColor = border;
        Q_EMIT borderColorChanged();
    }

    const QColor fontColor = pal.color(QPalette::Active, QPalette::Text);
    if (m_labelFontColor != fontColor) {
        m_labelFontColor = fontColor;
        Q_EMIT labelFontColorChanged();
    }
}

void Settings::applyAutotileBorderSystemColor()
{
    // Use the exact snapping zone highlight/inactive colors including their alpha.
    if (m_autotileBorderColor != m_highlightColor) {
        m_autotileBorderColor = m_highlightColor;
        Q_EMIT autotileBorderColorChanged();
    }

    if (m_autotileInactiveBorderColor != m_inactiveColor) {
        m_autotileInactiveBorderColor = m_inactiveColor;
        Q_EMIT autotileInactiveBorderColorChanged();
    }
}

} // namespace PlasmaZones
