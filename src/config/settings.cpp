// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "settings.h"
#include "colorimporter.h"
#include "configdefaults.h"
#include "configbackends.h"
#include "configmigration.h"
#include "perscreenresolver.h"
#include "settingsschema.h"

#include "../core/constants.h"
#include "../core/logging.h"
#include "../core/utils.h"
#include <QGuiApplication>
#include <QMetaMethod>
#include <QMetaProperty>
#include <QPalette>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QUuid>
#include <PhosphorTiles/AlgorithmRegistry.h>
#include "../autotile/AutotileConfig.h"

namespace PlasmaZones {

// ── Constructor ──────────────────────────────────────────────────────────────

namespace {
std::unique_ptr<PhosphorConfig::IBackend> migrateAndCreateOwnedBackend()
{
    // Defensive: ensure INI→JSON + schema-version migration has run before
    // we open the backend. Production entry points (daemon/main,
    // settings/main, editor controller) already call this; routing it
    // through the owning ctor makes tests and one-off tools safe too.
    // ensureJsonConfig is idempotent via a process-level atomic.
    ConfigMigration::ensureJsonConfig();
    return createDefaultConfigBackend();
}
} // namespace

Settings::Settings(PhosphorConfig::IBackend* backend, QObject* parent)
    : ISettings(parent)
    , m_configBackend(backend)
    , m_store(std::make_unique<PhosphorConfig::Store>(backend, buildSettingsSchema(), this))
{
    // Contract: @p backend MUST already be pointing at a migrated config
    // file. Production entry points (daemon/main, settings/main, editor
    // controller) call ConfigMigration::ensureJsonConfig() exactly once at
    // startup, before instantiating the backend. The non-owning ctor is
    // used when callers share one backend across Settings + LayoutManager
    // + other components, so the migration responsibility lives with them.
    //
    // (The owning ctor below routes through migrateAndCreateOwnedBackend
    // which performs the migration itself for standalone tools and tests.)
    load();
}

Settings::Settings(QObject* parent)
    : ISettings(parent)
    , m_ownedBackend(migrateAndCreateOwnedBackend())
    , m_configBackend(m_ownedBackend.get())
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

// ── load() dispatcher ────────────────────────────────────────────────────────

void Settings::load()
{
    // Snapshot every Q_PROPERTY declared on Settings (skipping inherited
    // QObject properties like objectName) that has a NOTIFY signal, so we
    // can re-emit the matching NOTIFY signal after load() refreshes
    // backing state. Without these emits QML bindings bound to, say,
    // `settings.borderWidth` would not update when the discard path
    // reloads the on-disk values.
    //
    // CRITICAL ORDERING: snapshot MUST be taken BEFORE
    // reparseConfiguration() and before per-screen / virtual-screen
    // loaders mutate members. Store-backed properties read on demand
    // through m_store, so once reparseConfiguration() overwrites the
    // backend's in-memory state, a post-reparse snapshot would see the
    // already-reloaded value and the comparison loop below would never
    // detect a change — silently breaking the discard-changes UX flow.
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

    m_configBackend->reparseConfiguration();

    // Store-backed groups (Shaders, Appearance, Ordering, Animations,
    // Rendering, Performance, ZoneGeometry, Shortcuts, Editor, Exclusions,
    // Display, ZoneSelector, Activation, Behavior, Autotiling) don't need
    // explicit load calls — their getters read through m_store on demand.
    loadPerScreenOverrides(m_configBackend);
    loadVirtualScreenConfigs(m_configBackend);

    if (useSystemColors()) {
        applySystemColorScheme();
    }
    if (autotileUseSystemBorderColors()) {
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
    // Explicitly remove the resolver's reserved container (e.g. the "PerScreen"
    // top-level object). groupList() hides this key entirely (it's filtered as
    // a reserved root), so the loop above never sees it — but when every
    // colon-form descendant has been individually deleted, an empty {} husk
    // can survive at the container path. Delete it directly via the raw JSON
    // path so it doesn't linger on disk after reset().
    backend->deleteGroup(PerScreenPathResolver::perScreenKey());
}

void Settings::purgeStaleKeys()
{
    // Root-level groups that must survive a save() cycle — written
    // independently of Settings::save(). Assignment:* and QuickLayouts
    // live in assignments.json and aren't seen here.
    //
    // "General" is preserved because JsonBackend uses it as the default
    // rootGroupName for writeRootString/readRootString — any caller
    // that persists a root-level key (current or future: KCM metadata,
    // first-run markers, etc.) lands there. Wiping it on every save
    // would silently destroy those values.
    const QStringList preservedGroups = {
        ConfigDefaults::generalGroup(),
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

    // Pass 1: per-key scalar purging inside Store-claimed paths.
    //
    // Store-declared groups: keep the declared key set, delete everything else
    // (evicts stale leftovers from renamed / removed keys).
    //
    // Store ancestor groups (e.g. "Snapping" when the schema declares
    // "Snapping.Appearance.Colors"): declared set is empty, so every scalar
    // leaf key gets deleted. save*Config will rewrite valid scalars a moment
    // later; sub-objects (the actual Store-claimed descendants) are preserved
    // because we only touch non-object children.
    auto purgeScalarsIn = [&](const QString& groupName, const QSet<QString>& declared) {
        auto g = m_configBackend->group(groupName);
        // IGroup::keyList returns scalar-leaf keys only; nested sub-groups are
        // filtered out by the backend so deleting undeclared keys here never
        // touches a Store-claimed descendant.
        const QStringList existingKeys = g->keyList();
        for (const QString& key : existingKeys) {
            if (!declared.contains(key)) {
                g->deleteKey(key);
            }
        }
    };

    for (auto it = schema.groups.constBegin(); it != schema.groups.constEnd(); ++it) {
        QSet<QString> declared;
        for (const auto& def : it.value()) {
            declared.insert(def.key);
        }
        purgeScalarsIn(it.key(), declared);
    }
    for (const QString& ancestor : std::as_const(storeAncestors)) {
        // Skip ancestors that are themselves Store-declared groups (e.g.
        // "Snapping.Behavior" carries its own declared keys AND hosts
        // declared descendants). The schema-loop pass above already
        // purged them with the right declared set; wiping with an empty
        // set here would delete those declared scalars.
        if (storeGroups.contains(ancestor)) {
            continue;
        }
        purgeScalarsIn(ancestor, {});
    }

    // Pass 2: anything else in the backend that isn't preserved, isn't
    // claimed by the Store, and isn't a per-screen / virtual-screen group
    // gets blanket-deleted. save*Config runs next and rewrites unmigrated
    // managed groups from their cached members.
    QSet<QString> deletedTopLevels;
    QSet<QString> deletedSubPaths;
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
        if (deletedTopLevels.contains(topLevel) || preservedGroups.contains(topLevel)) {
            continue;
        }
        // When the top-level itself is a Store ancestor, we can't delete
        // the whole thing — descend and delete only the non-claimed child.
        if (isStoreClaimed(topLevel)) {
            if (deletedSubPaths.contains(groupName)) {
                continue;
            }
            m_configBackend->deleteGroup(groupName);
            deletedSubPaths.insert(groupName);
        } else {
            m_configBackend->deleteGroup(topLevel);
            deletedTopLevels.insert(topLevel);
        }
    }
}

void Settings::save()
{
    purgeStaleKeys();

    // Flush every Store-declared key so the on-disk file always carries
    // the complete declared set after save, not just the keys a user
    // happened to mutate. Keys that haven't been written yet fall back to
    // the schema default via Store::readVariant, and the write path runs
    // the validator so clamped/normalized values land as the canonical form.
    //
    // INVARIANT: this loop iterates schema-declared keys only. Any key
    // present in the backing store but NOT in the schema is ignored here
    // and purged separately by @c purgeStaleKeys. @c Store::write rejects
    // writes to undeclared keys with a warning, so looping over
    // @c groupList() keys instead would fail closed but noisily.
    const auto& schema = m_store->schema();
    for (auto it = schema.groups.constBegin(); it != schema.groups.constEnd(); ++it) {
        for (const auto& def : it.value()) {
            m_store->write(it.key(), def.key, m_store->readVariant(it.key(), def.key));
        }
    }

    // Store-backed groups persist through setters on every write; the flush
    // loop above rewrites every declared key so clamp/canonicalization takes
    // effect during save(). Only the non-Store groups need explicit save calls.
    saveAllPerScreenOverrides(m_configBackend);
    saveVirtualScreenConfigs(m_configBackend);

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
// the store always produces the same string for any equivalent input. The
// parser below is still defensive (trim + skip-empty) in case a caller
// reads a string written before the validator was installed.

namespace {
QStringList parseCommaList(const QString& raw)
{
    if (raw.isEmpty()) {
        return {};
    }
    QStringList parts = raw.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (auto& s : parts) {
        s = s.trimmed();
    }
    return parts;
}
} // namespace

QStringList Settings::snappingLayoutOrder() const
{
    return parseCommaList(
        m_store->read<QString>(ConfigDefaults::orderingGroup(), ConfigDefaults::snappingLayoutOrderKey()));
}

void Settings::setSnappingLayoutOrder(const QStringList& order)
{
    // Read the canonical stored form before AND after writing so the
    // canonicalCommaList validator gets to pick the comparison points.
    // Comparing the user's (possibly non-canonical) input to the stored
    // canonical value would emit a spurious `changed` signal every time a
    // caller passed e.g. " a , b " while disk already holds "a,b".
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
    // See setSnappingLayoutOrder — post-write compare against the canonical
    // form avoids spurious change signals for equivalent non-canonical input.
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

// ── PhosphorZones::Zone geometry (PhosphorConfig::Store-backed) ────────────────────────────
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

// ── Display (PhosphorConfig::Store-backed) ──────────────────────────────────
// Display.* keys live in snappingBehaviorDisplayGroup; OSD + Effects keys
// share snappingEffectsGroup with the already-migrated Appearance.Blur.
// QStringList keys go over the wire as comma-joined strings; the getters
// parse back via parseCommaList (defined above in the Ordering section).

PZ_STORE_GET(bool, showZonesOnAllMonitors, snappingBehaviorDisplayGroup, showOnAllMonitorsKey, bool)
PZ_STORE_SET_BOOL(setShowZonesOnAllMonitors, snappingBehaviorDisplayGroup, showOnAllMonitorsKey,
                  showZonesOnAllMonitorsChanged)

QStringList Settings::disabledMonitors() const
{
    // Resolve connector names → stable screen ids on every read. Stored
    // connector names stay human-readable; consumers see canonical ids.
    QStringList entries = parseCommaList(
        m_store->read<QString>(ConfigDefaults::snappingBehaviorDisplayGroup(), ConfigDefaults::disabledMonitorsKey()));
    for (auto& name : entries) {
        if (Utils::isConnectorName(name)) {
            const QString resolved = Utils::screenIdForName(name);
            if (resolved != name) {
                name = resolved;
            }
        }
    }
    return entries;
}

void Settings::setDisabledMonitors(const QStringList& screenIdOrNames)
{
    // Post-write compare — the canonicalCommaList validator normalises
    // whitespace/duplicates on write, so a caller passing e.g. "DP-1, HDMI-1 "
    // against a stored "DP-1,HDMI-1" would fail the pre-write equality check
    // and fire a spurious changed signal even though the canonical form is
    // identical.
    const QString before =
        m_store->read<QString>(ConfigDefaults::snappingBehaviorDisplayGroup(), ConfigDefaults::disabledMonitorsKey());
    m_store->write(ConfigDefaults::snappingBehaviorDisplayGroup(), ConfigDefaults::disabledMonitorsKey(),
                   screenIdOrNames.join(QLatin1Char(',')));
    const QString after =
        m_store->read<QString>(ConfigDefaults::snappingBehaviorDisplayGroup(), ConfigDefaults::disabledMonitorsKey());
    if (before == after) {
        return;
    }
    Q_EMIT disabledMonitorsChanged();
    Q_EMIT settingsChanged();
}

bool Settings::isMonitorDisabled(const QString& screenIdOrName) const
{
    const QStringList entries = disabledMonitors();
    if (entries.contains(screenIdOrName)) {
        return true;
    }
    // Backward compat: resolve between connector name and screen id so
    // stored entries still match across the two representations.
    if (Utils::isConnectorName(screenIdOrName)) {
        const QString resolved = Utils::screenIdForName(screenIdOrName);
        if (resolved != screenIdOrName && entries.contains(resolved)) {
            return true;
        }
    } else {
        const QString connector = Utils::screenNameForId(screenIdOrName);
        if (!connector.isEmpty() && entries.contains(connector)) {
            return true;
        }
    }
    return false;
}

QStringList Settings::disabledDesktops() const
{
    return parseCommaList(
        m_store->read<QString>(ConfigDefaults::snappingBehaviorDisplayGroup(), ConfigDefaults::disabledDesktopsKey()));
}

void Settings::setDisabledDesktops(const QStringList& entries)
{
    // Post-write compare — see setDisabledMonitors for the canonicalisation
    // rationale.
    const QString before =
        m_store->read<QString>(ConfigDefaults::snappingBehaviorDisplayGroup(), ConfigDefaults::disabledDesktopsKey());
    m_store->write(ConfigDefaults::snappingBehaviorDisplayGroup(), ConfigDefaults::disabledDesktopsKey(),
                   entries.join(QLatin1Char(',')));
    const QString after =
        m_store->read<QString>(ConfigDefaults::snappingBehaviorDisplayGroup(), ConfigDefaults::disabledDesktopsKey());
    if (before == after) {
        return;
    }
    Q_EMIT disabledDesktopsChanged();
    Q_EMIT settingsChanged();
}

bool Settings::isDesktopDisabled(const QString& screenIdOrName, int desktop) const
{
    if (desktop <= 0) {
        return false;
    }
    const QStringList entries = disabledDesktops();
    QStringList namesToCheck = {screenIdOrName};
    if (Utils::isConnectorName(screenIdOrName)) {
        const QString resolved = Utils::screenIdForName(screenIdOrName);
        if (resolved != screenIdOrName) {
            namesToCheck.append(resolved);
        }
    } else {
        const QString connector = Utils::screenNameForId(screenIdOrName);
        if (!connector.isEmpty() && connector != screenIdOrName) {
            namesToCheck.append(connector);
        }
    }
    const QString desktopStr = QString::number(desktop);
    for (const QString& name : std::as_const(namesToCheck)) {
        if (entries.contains(name + QLatin1Char('/') + desktopStr)) {
            return true;
        }
    }
    return false;
}

QStringList Settings::disabledActivities() const
{
    return parseCommaList(m_store->read<QString>(ConfigDefaults::snappingBehaviorDisplayGroup(),
                                                 ConfigDefaults::disabledActivitiesKey()));
}

void Settings::setDisabledActivities(const QStringList& entries)
{
    // Post-write compare — see setDisabledMonitors for the canonicalisation
    // rationale.
    const QString before =
        m_store->read<QString>(ConfigDefaults::snappingBehaviorDisplayGroup(), ConfigDefaults::disabledActivitiesKey());
    m_store->write(ConfigDefaults::snappingBehaviorDisplayGroup(), ConfigDefaults::disabledActivitiesKey(),
                   entries.join(QLatin1Char(',')));
    const QString after =
        m_store->read<QString>(ConfigDefaults::snappingBehaviorDisplayGroup(), ConfigDefaults::disabledActivitiesKey());
    if (before == after) {
        return;
    }
    Q_EMIT disabledActivitiesChanged();
    Q_EMIT settingsChanged();
}

bool Settings::isActivityDisabled(const QString& screenIdOrName, const QString& activityId) const
{
    if (activityId.isEmpty()) {
        return false;
    }
    const QStringList entries = disabledActivities();
    QStringList namesToCheck = {screenIdOrName};
    if (Utils::isConnectorName(screenIdOrName)) {
        const QString resolved = Utils::screenIdForName(screenIdOrName);
        if (resolved != screenIdOrName) {
            namesToCheck.append(resolved);
        }
    } else {
        const QString connector = Utils::screenNameForId(screenIdOrName);
        if (!connector.isEmpty() && connector != screenIdOrName) {
            namesToCheck.append(connector);
        }
    }
    for (const QString& name : std::as_const(namesToCheck)) {
        if (entries.contains(name + QLatin1Char('/') + activityId)) {
            return true;
        }
    }
    return false;
}

PZ_STORE_GET(bool, showZoneNumbers, snappingEffectsGroup, showNumbersKey, bool)
PZ_STORE_SET_BOOL(setShowZoneNumbers, snappingEffectsGroup, showNumbersKey, showZoneNumbersChanged)
PZ_STORE_GET(bool, flashZonesOnSwitch, snappingEffectsGroup, flashOnSwitchKey, bool)
PZ_STORE_SET_BOOL(setFlashZonesOnSwitch, snappingEffectsGroup, flashOnSwitchKey, flashZonesOnSwitchChanged)
PZ_STORE_GET(bool, showOsdOnLayoutSwitch, snappingEffectsGroup, osdOnLayoutSwitchKey, bool)
PZ_STORE_SET_BOOL(setShowOsdOnLayoutSwitch, snappingEffectsGroup, osdOnLayoutSwitchKey, showOsdOnLayoutSwitchChanged)
PZ_STORE_GET(bool, showNavigationOsd, snappingEffectsGroup, navigationOsdKey, bool)
PZ_STORE_SET_BOOL(setShowNavigationOsd, snappingEffectsGroup, navigationOsdKey, showNavigationOsdChanged)

// Enum setters: stored as int, exposed via the enum-typed getter/setter and
// also via the int adapters QML uses (osdStyleInt / overlayDisplayModeInt).

OsdStyle Settings::osdStyle() const
{
    return static_cast<OsdStyle>(
        m_store->read<int>(ConfigDefaults::snappingEffectsGroup(), ConfigDefaults::osdStyleKey()));
}
int Settings::osdStyleInt() const
{
    return static_cast<int>(osdStyle());
}
void Settings::setOsdStyle(OsdStyle style)
{
    const int before = m_store->read<int>(ConfigDefaults::snappingEffectsGroup(), ConfigDefaults::osdStyleKey());
    m_store->write(ConfigDefaults::snappingEffectsGroup(), ConfigDefaults::osdStyleKey(), static_cast<int>(style));
    const int after = m_store->read<int>(ConfigDefaults::snappingEffectsGroup(), ConfigDefaults::osdStyleKey());
    if (after == before) {
        return;
    }
    Q_EMIT osdStyleChanged();
    Q_EMIT settingsChanged();
}
void Settings::setOsdStyleInt(int style)
{
    if (style >= 0 && style <= static_cast<int>(OsdStyle::Preview)) {
        setOsdStyle(static_cast<OsdStyle>(style));
    }
}

OverlayDisplayMode Settings::overlayDisplayMode() const
{
    return static_cast<OverlayDisplayMode>(
        m_store->read<int>(ConfigDefaults::snappingEffectsGroup(), ConfigDefaults::overlayDisplayModeKey()));
}
int Settings::overlayDisplayModeInt() const
{
    return static_cast<int>(overlayDisplayMode());
}
void Settings::setOverlayDisplayMode(OverlayDisplayMode mode)
{
    const int before =
        m_store->read<int>(ConfigDefaults::snappingEffectsGroup(), ConfigDefaults::overlayDisplayModeKey());
    m_store->write(ConfigDefaults::snappingEffectsGroup(), ConfigDefaults::overlayDisplayModeKey(),
                   static_cast<int>(mode));
    const int after =
        m_store->read<int>(ConfigDefaults::snappingEffectsGroup(), ConfigDefaults::overlayDisplayModeKey());
    if (after == before) {
        return;
    }
    Q_EMIT overlayDisplayModeChanged();
    Q_EMIT settingsChanged();
}
void Settings::setOverlayDisplayModeInt(int mode)
{
    if (mode >= 0 && mode <= static_cast<int>(OverlayDisplayMode::LayoutPreview)) {
        setOverlayDisplayMode(static_cast<OverlayDisplayMode>(mode));
    }
}

// filterLayoutsByAspectRatio sits in snappingBehaviorDisplayGroup with the
// other display settings — NOTIFY signal is filterLayoutsByAspectRatioChanged.
PZ_STORE_GET(bool, filterLayoutsByAspectRatio, snappingBehaviorDisplayGroup, filterByAspectRatioKey, bool)
PZ_STORE_SET_BOOL(setFilterLayoutsByAspectRatio, snappingBehaviorDisplayGroup, filterByAspectRatioKey,
                  filterLayoutsByAspectRatioChanged)

// ── Exclusions (PhosphorConfig::Store-backed) ───────────────────────────────

QStringList Settings::excludedApplications() const
{
    return parseCommaList(m_store->read<QString>(ConfigDefaults::exclusionsGroup(), ConfigDefaults::applicationsKey()));
}

void Settings::setExcludedApplications(const QStringList& apps)
{
    // Post-write compare — see setDisabledMonitors for the canonicalisation
    // rationale. Callers like addExcludedApplication rely on this setter not
    // firing the changed signal when the de-duplicated / trimmed form
    // already matches storage.
    const QString before = m_store->read<QString>(ConfigDefaults::exclusionsGroup(), ConfigDefaults::applicationsKey());
    m_store->write(ConfigDefaults::exclusionsGroup(), ConfigDefaults::applicationsKey(), apps.join(QLatin1Char(',')));
    const QString after = m_store->read<QString>(ConfigDefaults::exclusionsGroup(), ConfigDefaults::applicationsKey());
    if (before == after) {
        return;
    }
    Q_EMIT excludedApplicationsChanged();
    Q_EMIT settingsChanged();
}

void Settings::addExcludedApplication(const QString& app)
{
    const QString trimmed = app.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }
    QStringList list = excludedApplications();
    if (list.contains(trimmed)) {
        return;
    }
    list.append(trimmed);
    setExcludedApplications(list);
}

void Settings::removeExcludedApplicationAt(int index)
{
    QStringList list = excludedApplications();
    if (index < 0 || index >= list.size()) {
        return;
    }
    list.removeAt(index);
    setExcludedApplications(list);
}

QStringList Settings::excludedWindowClasses() const
{
    return parseCommaList(
        m_store->read<QString>(ConfigDefaults::exclusionsGroup(), ConfigDefaults::windowClassesKey()));
}

void Settings::setExcludedWindowClasses(const QStringList& classes)
{
    // Post-write compare — see setDisabledMonitors for the canonicalisation
    // rationale.
    const QString before =
        m_store->read<QString>(ConfigDefaults::exclusionsGroup(), ConfigDefaults::windowClassesKey());
    m_store->write(ConfigDefaults::exclusionsGroup(), ConfigDefaults::windowClassesKey(),
                   classes.join(QLatin1Char(',')));
    const QString after = m_store->read<QString>(ConfigDefaults::exclusionsGroup(), ConfigDefaults::windowClassesKey());
    if (before == after) {
        return;
    }
    Q_EMIT excludedWindowClassesChanged();
    Q_EMIT settingsChanged();
}

void Settings::addExcludedWindowClass(const QString& cls)
{
    const QString trimmed = cls.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }
    QStringList list = excludedWindowClasses();
    if (list.contains(trimmed)) {
        return;
    }
    list.append(trimmed);
    setExcludedWindowClasses(list);
}

void Settings::removeExcludedWindowClassAt(int index)
{
    QStringList list = excludedWindowClasses();
    if (index < 0 || index >= list.size()) {
        return;
    }
    list.removeAt(index);
    setExcludedWindowClasses(list);
}

PZ_STORE_GET(bool, excludeTransientWindows, exclusionsGroup, transientWindowsKey, bool)
PZ_STORE_SET_BOOL(setExcludeTransientWindows, exclusionsGroup, transientWindowsKey, excludeTransientWindowsChanged)
PZ_STORE_GET(int, minimumWindowWidth, exclusionsGroup, minimumWindowWidthKey, int)
PZ_STORE_SET_INT(setMinimumWindowWidth, exclusionsGroup, minimumWindowWidthKey, minimumWindowWidthChanged)
PZ_STORE_GET(int, minimumWindowHeight, exclusionsGroup, minimumWindowHeightKey, int)
PZ_STORE_SET_INT(setMinimumWindowHeight, exclusionsGroup, minimumWindowHeightKey, minimumWindowHeightChanged)

// ── PhosphorZones::Zone Selector (PhosphorConfig::Store-backed) ────────────────────────────
// Three enum-ints exposed via both the typed setter and an Int adapter for
// QML binding. Stored as int, the schema clamps the range.

PZ_STORE_GET(bool, zoneSelectorEnabled, snappingZoneSelectorGroup, enabledKey, bool)
PZ_STORE_SET_BOOL(setZoneSelectorEnabled, snappingZoneSelectorGroup, enabledKey, zoneSelectorEnabledChanged)
PZ_STORE_GET(int, zoneSelectorTriggerDistance, snappingZoneSelectorGroup, triggerDistanceKey, int)
PZ_STORE_SET_INT(setZoneSelectorTriggerDistance, snappingZoneSelectorGroup, triggerDistanceKey,
                 zoneSelectorTriggerDistanceChanged)

ZoneSelectorPosition Settings::zoneSelectorPosition() const
{
    return static_cast<ZoneSelectorPosition>(
        m_store->read<int>(ConfigDefaults::snappingZoneSelectorGroup(), ConfigDefaults::positionKey()));
}
int Settings::zoneSelectorPositionInt() const
{
    return static_cast<int>(zoneSelectorPosition());
}
void Settings::setZoneSelectorPosition(ZoneSelectorPosition value)
{
    const int before = m_store->read<int>(ConfigDefaults::snappingZoneSelectorGroup(), ConfigDefaults::positionKey());
    m_store->write(ConfigDefaults::snappingZoneSelectorGroup(), ConfigDefaults::positionKey(), static_cast<int>(value));
    const int after = m_store->read<int>(ConfigDefaults::snappingZoneSelectorGroup(), ConfigDefaults::positionKey());
    if (after == before) {
        return;
    }
    Q_EMIT zoneSelectorPositionChanged();
    Q_EMIT settingsChanged();
}
void Settings::setZoneSelectorPositionInt(int value)
{
    if (value >= 0 && value <= 8) {
        setZoneSelectorPosition(static_cast<ZoneSelectorPosition>(value));
    }
}

ZoneSelectorLayoutMode Settings::zoneSelectorLayoutMode() const
{
    return static_cast<ZoneSelectorLayoutMode>(
        m_store->read<int>(ConfigDefaults::snappingZoneSelectorGroup(), ConfigDefaults::layoutModeKey()));
}
int Settings::zoneSelectorLayoutModeInt() const
{
    return static_cast<int>(zoneSelectorLayoutMode());
}
void Settings::setZoneSelectorLayoutMode(ZoneSelectorLayoutMode value)
{
    const int before = m_store->read<int>(ConfigDefaults::snappingZoneSelectorGroup(), ConfigDefaults::layoutModeKey());
    m_store->write(ConfigDefaults::snappingZoneSelectorGroup(), ConfigDefaults::layoutModeKey(),
                   static_cast<int>(value));
    const int after = m_store->read<int>(ConfigDefaults::snappingZoneSelectorGroup(), ConfigDefaults::layoutModeKey());
    if (after == before) {
        return;
    }
    Q_EMIT zoneSelectorLayoutModeChanged();
    Q_EMIT settingsChanged();
}
void Settings::setZoneSelectorLayoutModeInt(int value)
{
    if (value >= 0 && value <= static_cast<int>(ZoneSelectorLayoutMode::Vertical)) {
        setZoneSelectorLayoutMode(static_cast<ZoneSelectorLayoutMode>(value));
    }
}

PZ_STORE_GET(int, zoneSelectorPreviewWidth, snappingZoneSelectorGroup, previewWidthKey, int)
PZ_STORE_SET_INT(setZoneSelectorPreviewWidth, snappingZoneSelectorGroup, previewWidthKey,
                 zoneSelectorPreviewWidthChanged)
PZ_STORE_GET(int, zoneSelectorPreviewHeight, snappingZoneSelectorGroup, previewHeightKey, int)
PZ_STORE_SET_INT(setZoneSelectorPreviewHeight, snappingZoneSelectorGroup, previewHeightKey,
                 zoneSelectorPreviewHeightChanged)
PZ_STORE_GET(bool, zoneSelectorPreviewLockAspect, snappingZoneSelectorGroup, previewLockAspectKey, bool)
PZ_STORE_SET_BOOL(setZoneSelectorPreviewLockAspect, snappingZoneSelectorGroup, previewLockAspectKey,
                  zoneSelectorPreviewLockAspectChanged)
PZ_STORE_GET(int, zoneSelectorGridColumns, snappingZoneSelectorGroup, gridColumnsKey, int)
PZ_STORE_SET_INT(setZoneSelectorGridColumns, snappingZoneSelectorGroup, gridColumnsKey, zoneSelectorGridColumnsChanged)

ZoneSelectorSizeMode Settings::zoneSelectorSizeMode() const
{
    return static_cast<ZoneSelectorSizeMode>(
        m_store->read<int>(ConfigDefaults::snappingZoneSelectorGroup(), ConfigDefaults::sizeModeKey()));
}
int Settings::zoneSelectorSizeModeInt() const
{
    return static_cast<int>(zoneSelectorSizeMode());
}
void Settings::setZoneSelectorSizeMode(ZoneSelectorSizeMode value)
{
    const int before = m_store->read<int>(ConfigDefaults::snappingZoneSelectorGroup(), ConfigDefaults::sizeModeKey());
    m_store->write(ConfigDefaults::snappingZoneSelectorGroup(), ConfigDefaults::sizeModeKey(), static_cast<int>(value));
    const int after = m_store->read<int>(ConfigDefaults::snappingZoneSelectorGroup(), ConfigDefaults::sizeModeKey());
    if (after == before) {
        return;
    }
    Q_EMIT zoneSelectorSizeModeChanged();
    Q_EMIT settingsChanged();
}
void Settings::setZoneSelectorSizeModeInt(int value)
{
    if (value >= 0 && value <= static_cast<int>(ZoneSelectorSizeMode::Manual)) {
        setZoneSelectorSizeMode(static_cast<ZoneSelectorSizeMode>(value));
    }
}

PZ_STORE_GET(int, zoneSelectorMaxRows, snappingZoneSelectorGroup, maxRowsKey, int)
PZ_STORE_SET_INT(setZoneSelectorMaxRows, snappingZoneSelectorGroup, maxRowsKey, zoneSelectorMaxRowsChanged)

// ── Activation + Behavior (PhosphorConfig::Store-backed) ────────────────────

// The trigger list schema declares @c QVariantList with the
// @c canonicalTriggerList validator that caps at @c MaxTriggersPerAction
// and coerces each entry to {modifier:int, mouseButton:int}. Settings
// reads via @c Store::readVariant(...).toList() and writes via
// @c Store::write — the Store routes structured values through
// @c IGroup::writeJson so they land on disk as native JSON arrays.
//
// Both Settings::MaxTriggersPerAction and the schema's trigger-list cap
// derive from ConfigDefaults::maxTriggersPerAction() — see that accessor
// for the single source of truth. This assert is defence-in-depth in
// case a future refactor accidentally detaches Settings from the shared
// constant.
static_assert(Settings::MaxTriggersPerAction == ConfigDefaults::maxTriggersPerAction(),
              "Settings::MaxTriggersPerAction must equal ConfigDefaults::maxTriggersPerAction — "
              "single source of truth lives in ConfigDefaults.");

PZ_STORE_GET(bool, snappingEnabled, snappingGroup, enabledKey, bool)
PZ_STORE_SET_BOOL(setSnappingEnabled, snappingGroup, enabledKey, snappingEnabledChanged)
PZ_STORE_GET(bool, toggleActivation, snappingBehaviorGroup, toggleActivationKey, bool)
PZ_STORE_SET_BOOL(setToggleActivation, snappingBehaviorGroup, toggleActivationKey, toggleActivationChanged)

QVariantList Settings::dragActivationTriggers() const
{
    return m_store->readVariant(ConfigDefaults::snappingBehaviorGroup(), ConfigDefaults::triggersKey()).toList();
}
void Settings::setDragActivationTriggers(const QVariantList& triggers)
{
    // Post-write compare — the schema's canonicalTriggerList validator
    // drops non-map entries, strips unknown keys, and caps the list. A
    // pre-write equality check against the stored canonical form would fire
    // a spurious changed signal whenever the caller passed a list that
    // canonicalises to the same value (e.g. extra keys, sub-cap padding).
    const QVariantList before = dragActivationTriggers();
    m_store->write(ConfigDefaults::snappingBehaviorGroup(), ConfigDefaults::triggersKey(),
                   triggers.mid(0, MaxTriggersPerAction));
    const QVariantList after = dragActivationTriggers();
    if (before == after) {
        return;
    }
    Q_EMIT dragActivationTriggersChanged();
    Q_EMIT settingsChanged();
}

PZ_STORE_GET(bool, zoneSpanEnabled, snappingBehaviorZoneSpanGroup, enabledKey, bool)
PZ_STORE_SET_BOOL(setZoneSpanEnabled, snappingBehaviorZoneSpanGroup, enabledKey, zoneSpanEnabledChanged)

DragModifier Settings::zoneSpanModifier() const
{
    return static_cast<DragModifier>(
        m_store->read<int>(ConfigDefaults::snappingBehaviorZoneSpanGroup(), ConfigDefaults::modifierKey()));
}
int Settings::zoneSpanModifierInt() const
{
    return static_cast<int>(zoneSpanModifier());
}
void Settings::setZoneSpanModifier(DragModifier modifier)
{
    // Write-then-compare so the schema's validIntOr validator gets the first
    // word on whether the request is valid. A pre-write equality check like
    // `before == static_cast<int>(modifier)` would let an invalid modifier
    // (e.g. 99) sneak past: the store would snap it to Disabled=0 and skip
    // the disk write, but we'd then stamp a trigger list with modifier=99
    // baked in because the synthesis below ran with the raw input.
    const int before =
        m_store->read<int>(ConfigDefaults::snappingBehaviorZoneSpanGroup(), ConfigDefaults::modifierKey());
    m_store->write(ConfigDefaults::snappingBehaviorZoneSpanGroup(), ConfigDefaults::modifierKey(),
                   static_cast<int>(modifier));
    const int after =
        m_store->read<int>(ConfigDefaults::snappingBehaviorZoneSpanGroup(), ConfigDefaults::modifierKey());
    if (after == before) {
        return;
    }

    // Modifier actually changed. Sync the trigger list's first entry — using
    // the validator-coerced `after` value, not the raw input, so an invalid
    // request that was snapped to the default doesn't smuggle a
    // {modifier: 99} phantom into the trigger list.
    const QVariantList beforeTriggers = zoneSpanTriggers();
    QVariantList triggers = beforeTriggers;
    if (!triggers.isEmpty()) {
        QVariantMap first = triggers.first().toMap();
        first[ConfigDefaults::triggerModifierField()] = after;
        triggers[0] = first;
    } else {
        QVariantMap trigger;
        trigger[ConfigDefaults::triggerModifierField()] = after;
        trigger[ConfigDefaults::triggerMouseButtonField()] = 0;
        triggers = {trigger};
    }
    m_store->write(ConfigDefaults::snappingBehaviorZoneSpanGroup(), ConfigDefaults::triggersKey(), triggers);
    const QVariantList afterTriggers = zoneSpanTriggers();

    Q_EMIT zoneSpanModifierChanged();
    if (afterTriggers != beforeTriggers) {
        Q_EMIT zoneSpanTriggersChanged();
    }
    Q_EMIT settingsChanged();
}
void Settings::setZoneSpanModifierInt(int modifier)
{
    if (modifier >= 0 && modifier <= static_cast<int>(DragModifier::CtrlAltMeta)) {
        setZoneSpanModifier(static_cast<DragModifier>(modifier));
    }
}

QVariantList Settings::zoneSpanTriggers() const
{
    // ZoneSpan uniquely carries BOTH a legacy scalar "Modifier" key and a
    // v2 "Triggers" list — historical result of the migration path where
    // the old setting was a single modifier. SnapAssist and DragActivation
    // only have the trigger list, so no equivalent synthesis applies there.
    //
    // When Triggers is absent on disk, synthesize a single-entry list from
    // the actual ZoneSpanModifier value instead of returning the schema's
    // static default. That keeps the two settings in sync when only one was
    // explicitly persisted — e.g. a hand-edited config that sets
    // ZoneSpanModifier but leaves Triggers untouched should report the
    // edited modifier through zoneSpanTriggers(), not the compiled default.
    //
    // hasKey and zoneSpanModifier() both open a JsonGroup — JsonBackend
    // enforces a single active group at a time, so sample the modifier
    // BEFORE grabbing the hasKey group.
    bool hasTriggers = false;
    {
        auto g = m_configBackend->group(ConfigDefaults::snappingBehaviorZoneSpanGroup());
        hasTriggers = g->hasKey(ConfigDefaults::triggersKey());
    }
    if (!hasTriggers) {
        QVariantMap trigger;
        trigger[ConfigDefaults::triggerModifierField()] = static_cast<int>(zoneSpanModifier());
        trigger[ConfigDefaults::triggerMouseButtonField()] = 0;
        return {trigger};
    }
    return m_store->readVariant(ConfigDefaults::snappingBehaviorZoneSpanGroup(), ConfigDefaults::triggersKey())
        .toList();
}
void Settings::setZoneSpanTriggers(const QVariantList& triggers)
{
    // Post-write compare — see setDragActivationTriggers for the
    // canonicalisation rationale. Snapshot both triggers AND the legacy
    // modifier up front so we only emit the NOTIFY signals whose value
    // actually changed.
    const QVariantList beforeTriggers = zoneSpanTriggers();
    const int beforeModifier =
        m_store->read<int>(ConfigDefaults::snappingBehaviorZoneSpanGroup(), ConfigDefaults::modifierKey());

    m_store->write(ConfigDefaults::snappingBehaviorZoneSpanGroup(), ConfigDefaults::triggersKey(),
                   triggers.mid(0, MaxTriggersPerAction));

    // Sync legacy modifier member from first trigger with a non-zero modifier.
    // Derive from the validator-coerced post-write list so a
    // {modifier: 99} entry in the input doesn't leak past the clamp.
    const QVariantList afterTriggers = zoneSpanTriggers();
    DragModifier synced = DragModifier::Disabled;
    for (const auto& t : afterTriggers) {
        const int mod = t.toMap().value(ConfigDefaults::triggerModifierField(), 0).toInt();
        if (mod != 0) {
            synced = static_cast<DragModifier>(qBound(0, mod, static_cast<int>(DragModifier::CtrlAltMeta)));
            break;
        }
    }
    m_store->write(ConfigDefaults::snappingBehaviorZoneSpanGroup(), ConfigDefaults::modifierKey(),
                   static_cast<int>(synced));
    const int afterModifier =
        m_store->read<int>(ConfigDefaults::snappingBehaviorZoneSpanGroup(), ConfigDefaults::modifierKey());

    const bool triggersChanged = afterTriggers != beforeTriggers;
    const bool modifierChanged = afterModifier != beforeModifier;
    if (!triggersChanged && !modifierChanged) {
        return;
    }
    if (triggersChanged) {
        Q_EMIT zoneSpanTriggersChanged();
    }
    if (modifierChanged) {
        Q_EMIT zoneSpanModifierChanged();
    }
    Q_EMIT settingsChanged();
}

// Behavior: WindowHandling + SnapAssist.

PZ_STORE_GET(bool, keepWindowsInZonesOnResolutionChange, snappingBehaviorWindowHandlingGroup, keepOnResolutionChangeKey,
             bool)
PZ_STORE_SET_BOOL(setKeepWindowsInZonesOnResolutionChange, snappingBehaviorWindowHandlingGroup,
                  keepOnResolutionChangeKey, keepWindowsInZonesOnResolutionChangeChanged)
PZ_STORE_GET(bool, moveNewWindowsToLastZone, snappingBehaviorWindowHandlingGroup, moveNewToLastZoneKey, bool)
PZ_STORE_SET_BOOL(setMoveNewWindowsToLastZone, snappingBehaviorWindowHandlingGroup, moveNewToLastZoneKey,
                  moveNewWindowsToLastZoneChanged)
PZ_STORE_GET(bool, restoreOriginalSizeOnUnsnap, snappingBehaviorWindowHandlingGroup, restoreOnUnsnapKey, bool)
PZ_STORE_SET_BOOL(setRestoreOriginalSizeOnUnsnap, snappingBehaviorWindowHandlingGroup, restoreOnUnsnapKey,
                  restoreOriginalSizeOnUnsnapChanged)

StickyWindowHandling Settings::stickyWindowHandling() const
{
    return static_cast<StickyWindowHandling>(m_store->read<int>(ConfigDefaults::snappingBehaviorWindowHandlingGroup(),
                                                                ConfigDefaults::stickyWindowHandlingKey()));
}
int Settings::stickyWindowHandlingInt() const
{
    return static_cast<int>(stickyWindowHandling());
}
void Settings::setStickyWindowHandling(StickyWindowHandling handling)
{
    const int before = m_store->read<int>(ConfigDefaults::snappingBehaviorWindowHandlingGroup(),
                                          ConfigDefaults::stickyWindowHandlingKey());
    m_store->write(ConfigDefaults::snappingBehaviorWindowHandlingGroup(), ConfigDefaults::stickyWindowHandlingKey(),
                   static_cast<int>(handling));
    const int after = m_store->read<int>(ConfigDefaults::snappingBehaviorWindowHandlingGroup(),
                                         ConfigDefaults::stickyWindowHandlingKey());
    if (after == before) {
        return;
    }
    Q_EMIT stickyWindowHandlingChanged();
    Q_EMIT settingsChanged();
}
void Settings::setStickyWindowHandlingInt(int handling)
{
    if (handling >= static_cast<int>(StickyWindowHandling::TreatAsNormal)
        && handling <= static_cast<int>(StickyWindowHandling::IgnoreAll)) {
        setStickyWindowHandling(static_cast<StickyWindowHandling>(handling));
    }
}

PZ_STORE_GET(bool, restoreWindowsToZonesOnLogin, snappingBehaviorWindowHandlingGroup, restoreOnLoginKey, bool)
PZ_STORE_SET_BOOL(setRestoreWindowsToZonesOnLogin, snappingBehaviorWindowHandlingGroup, restoreOnLoginKey,
                  restoreWindowsToZonesOnLoginChanged)

QString Settings::defaultLayoutId() const
{
    return m_store->read<QString>(ConfigDefaults::snappingBehaviorWindowHandlingGroup(),
                                  ConfigDefaults::defaultLayoutIdKey());
}
void Settings::setDefaultLayoutId(const QString& layoutId)
{
    const QString normalized = normalizeUuidString(layoutId);
    if (defaultLayoutId() == normalized) {
        return;
    }
    m_store->write(ConfigDefaults::snappingBehaviorWindowHandlingGroup(), ConfigDefaults::defaultLayoutIdKey(),
                   normalized);
    Q_EMIT defaultLayoutIdChanged();
    Q_EMIT settingsChanged();
}

PZ_STORE_GET(bool, snapAssistFeatureEnabled, snappingBehaviorSnapAssistGroup, featureEnabledKey, bool)
PZ_STORE_SET_BOOL(setSnapAssistFeatureEnabled, snappingBehaviorSnapAssistGroup, featureEnabledKey,
                  snapAssistFeatureEnabledChanged)
PZ_STORE_GET(bool, snapAssistEnabled, snappingBehaviorSnapAssistGroup, enabledKey, bool)
PZ_STORE_SET_BOOL(setSnapAssistEnabled, snappingBehaviorSnapAssistGroup, enabledKey, snapAssistEnabledChanged)

QVariantList Settings::snapAssistTriggers() const
{
    return m_store->readVariant(ConfigDefaults::snappingBehaviorSnapAssistGroup(), ConfigDefaults::triggersKey())
        .toList();
}
void Settings::setSnapAssistTriggers(const QVariantList& triggers)
{
    // Post-write compare — see setDragActivationTriggers for the
    // canonicalisation rationale.
    const QVariantList before = snapAssistTriggers();
    m_store->write(ConfigDefaults::snappingBehaviorSnapAssistGroup(), ConfigDefaults::triggersKey(),
                   triggers.mid(0, MaxTriggersPerAction));
    const QVariantList after = snapAssistTriggers();
    if (before == after) {
        return;
    }
    Q_EMIT snapAssistTriggersChanged();
    Q_EMIT settingsChanged();
}

// ── Autotiling (PhosphorConfig::Store-backed) ──────────────────────────────
// Largest group — seven sub-groups. defaultAutotileAlgorithm passes through
// PhosphorTiles::AlgorithmRegistry for validation; per-algorithm settings round-trip as a
// JSON string and sanitize via AutotileConfig::perAlgoFromVariantMap.

PZ_STORE_GET(bool, autotileEnabled, tilingGroup, enabledKey, bool)
PZ_STORE_SET_BOOL(setAutotileEnabled, tilingGroup, enabledKey, autotileEnabledChanged)

QString Settings::defaultAutotileAlgorithm() const
{
    return m_store->read<QString>(ConfigDefaults::tilingAlgorithmGroup(), ConfigDefaults::defaultKey());
}
void Settings::setDefaultAutotileAlgorithm(const QString& algorithm)
{
    QString validated = algorithm;
    if (!algorithm.startsWith(QLatin1String("script:"))
        && !PhosphorTiles::AlgorithmRegistry::instance()->algorithm(algorithm)) {
        qCWarning(lcConfig) << "Unknown autotile algorithm:" << algorithm << "- using default";
        validated = PhosphorTiles::AlgorithmRegistry::defaultAlgorithmId();
    }
    if (defaultAutotileAlgorithm() == validated) {
        return;
    }
    m_store->write(ConfigDefaults::tilingAlgorithmGroup(), ConfigDefaults::defaultKey(), validated);
    Q_EMIT defaultAutotileAlgorithmChanged();
    Q_EMIT settingsChanged();
}

PZ_STORE_GET(qreal, autotileSplitRatio, tilingAlgorithmGroup, splitRatioKey, double)
PZ_STORE_SET_DOUBLE(setAutotileSplitRatio, tilingAlgorithmGroup, splitRatioKey, autotileSplitRatioChanged)
PZ_STORE_GET(qreal, autotileSplitRatioStep, tilingAlgorithmGroup, splitRatioStepKey, double)
PZ_STORE_SET_DOUBLE(setAutotileSplitRatioStep, tilingAlgorithmGroup, splitRatioStepKey, autotileSplitRatioStepChanged)
PZ_STORE_GET(int, autotileMasterCount, tilingAlgorithmGroup, masterCountKey, int)
PZ_STORE_SET_INT(setAutotileMasterCount, tilingAlgorithmGroup, masterCountKey, autotileMasterCountChanged)
PZ_STORE_GET(int, autotileMaxWindows, tilingAlgorithmGroup, maxWindowsKey, int)
PZ_STORE_SET_INT(setAutotileMaxWindows, tilingAlgorithmGroup, maxWindowsKey, autotileMaxWindowsChanged)

QVariantMap Settings::autotilePerAlgorithmSettings() const
{
    // Schema declares this key as QVariantMap with sanitizePerAlgorithmSettings
    // as the validator, so read/write both pass through
    // AutotileConfig::perAlgo{From,To}VariantMap without duplicating the logic
    // here. The Store routes the map through IGroup::writeJson → native
    // JSON object on disk.
    return m_store->readVariant(ConfigDefaults::tilingAlgorithmGroup(), ConfigDefaults::perAlgorithmSettingsKey())
        .toMap();
}
void Settings::setAutotilePerAlgorithmSettings(const QVariantMap& value)
{
    // Pre-sanitize so the equality check compares against the canonicalised
    // form (the schema validator would canonicalise on both sides anyway,
    // but avoiding the redundant write keeps the settingsChanged signal
    // from firing when a caller writes a semantically equal unsorted map).
    const QVariantMap sanitized = AutotileConfig::perAlgoToVariantMap(AutotileConfig::perAlgoFromVariantMap(value));
    if (autotilePerAlgorithmSettings() == sanitized) {
        return;
    }
    m_store->write(ConfigDefaults::tilingAlgorithmGroup(), ConfigDefaults::perAlgorithmSettingsKey(), sanitized);
    Q_EMIT autotilePerAlgorithmSettingsChanged();
    Q_EMIT settingsChanged();
}

// Tiling.Gaps
PZ_STORE_GET(int, autotileInnerGap, tilingGapsGroup, innerKey, int)
PZ_STORE_SET_INT(setAutotileInnerGap, tilingGapsGroup, innerKey, autotileInnerGapChanged)
PZ_STORE_GET(int, autotileOuterGap, tilingGapsGroup, outerKey, int)
PZ_STORE_SET_INT(setAutotileOuterGap, tilingGapsGroup, outerKey, autotileOuterGapChanged)
PZ_STORE_GET(bool, autotileUsePerSideOuterGap, tilingGapsGroup, usePerSideKey, bool)
PZ_STORE_SET_BOOL(setAutotileUsePerSideOuterGap, tilingGapsGroup, usePerSideKey, autotileUsePerSideOuterGapChanged)
PZ_STORE_GET(int, autotileOuterGapTop, tilingGapsGroup, topKey, int)
PZ_STORE_SET_INT(setAutotileOuterGapTop, tilingGapsGroup, topKey, autotileOuterGapTopChanged)
PZ_STORE_GET(int, autotileOuterGapBottom, tilingGapsGroup, bottomKey, int)
PZ_STORE_SET_INT(setAutotileOuterGapBottom, tilingGapsGroup, bottomKey, autotileOuterGapBottomChanged)
PZ_STORE_GET(int, autotileOuterGapLeft, tilingGapsGroup, leftKey, int)
PZ_STORE_SET_INT(setAutotileOuterGapLeft, tilingGapsGroup, leftKey, autotileOuterGapLeftChanged)
PZ_STORE_GET(int, autotileOuterGapRight, tilingGapsGroup, rightKey, int)
PZ_STORE_SET_INT(setAutotileOuterGapRight, tilingGapsGroup, rightKey, autotileOuterGapRightChanged)
PZ_STORE_GET(bool, autotileSmartGaps, tilingGapsGroup, smartGapsKey, bool)
PZ_STORE_SET_BOOL(setAutotileSmartGaps, tilingGapsGroup, smartGapsKey, autotileSmartGapsChanged)

// Tiling.Behavior
PZ_STORE_GET(bool, autotileFocusNewWindows, tilingBehaviorGroup, focusNewWindowsKey, bool)
PZ_STORE_SET_BOOL(setAutotileFocusNewWindows, tilingBehaviorGroup, focusNewWindowsKey, autotileFocusNewWindowsChanged)
PZ_STORE_GET(bool, autotileFocusFollowsMouse, tilingBehaviorGroup, focusFollowsMouseKey, bool)
PZ_STORE_SET_BOOL(setAutotileFocusFollowsMouse, tilingBehaviorGroup, focusFollowsMouseKey,
                  autotileFocusFollowsMouseChanged)
PZ_STORE_GET(bool, autotileRespectMinimumSize, tilingBehaviorGroup, respectMinimumSizeKey, bool)
PZ_STORE_SET_BOOL(setAutotileRespectMinimumSize, tilingBehaviorGroup, respectMinimumSizeKey,
                  autotileRespectMinimumSizeChanged)

Settings::AutotileInsertPosition Settings::autotileInsertPosition() const
{
    return static_cast<AutotileInsertPosition>(
        m_store->read<int>(ConfigDefaults::tilingBehaviorGroup(), ConfigDefaults::insertPositionKey()));
}
int Settings::autotileInsertPositionInt() const
{
    return static_cast<int>(autotileInsertPosition());
}
void Settings::setAutotileInsertPosition(AutotileInsertPosition position)
{
    const int before = m_store->read<int>(ConfigDefaults::tilingBehaviorGroup(), ConfigDefaults::insertPositionKey());
    m_store->write(ConfigDefaults::tilingBehaviorGroup(), ConfigDefaults::insertPositionKey(),
                   static_cast<int>(position));
    const int after = m_store->read<int>(ConfigDefaults::tilingBehaviorGroup(), ConfigDefaults::insertPositionKey());
    if (after == before) {
        return;
    }
    Q_EMIT autotileInsertPositionChanged();
    Q_EMIT settingsChanged();
}
void Settings::setAutotileInsertPositionInt(int position)
{
    if (position >= ConfigDefaults::autotileInsertPositionMin()
        && position <= ConfigDefaults::autotileInsertPositionMax()) {
        setAutotileInsertPosition(static_cast<AutotileInsertPosition>(position));
    }
}

StickyWindowHandling Settings::autotileStickyWindowHandling() const
{
    return static_cast<StickyWindowHandling>(
        m_store->read<int>(ConfigDefaults::tilingBehaviorGroup(), ConfigDefaults::stickyWindowHandlingKey()));
}
int Settings::autotileStickyWindowHandlingInt() const
{
    return static_cast<int>(autotileStickyWindowHandling());
}
void Settings::setAutotileStickyWindowHandling(StickyWindowHandling handling)
{
    const int before =
        m_store->read<int>(ConfigDefaults::tilingBehaviorGroup(), ConfigDefaults::stickyWindowHandlingKey());
    m_store->write(ConfigDefaults::tilingBehaviorGroup(), ConfigDefaults::stickyWindowHandlingKey(),
                   static_cast<int>(handling));
    const int after =
        m_store->read<int>(ConfigDefaults::tilingBehaviorGroup(), ConfigDefaults::stickyWindowHandlingKey());
    if (after == before) {
        return;
    }
    Q_EMIT autotileStickyWindowHandlingChanged();
    Q_EMIT settingsChanged();
}
void Settings::setAutotileStickyWindowHandlingInt(int handling)
{
    if (handling >= static_cast<int>(StickyWindowHandling::TreatAsNormal)
        && handling <= static_cast<int>(StickyWindowHandling::IgnoreAll)) {
        setAutotileStickyWindowHandling(static_cast<StickyWindowHandling>(handling));
    }
}

AutotileDragBehavior Settings::autotileDragBehavior() const
{
    return static_cast<AutotileDragBehavior>(
        m_store->read<int>(ConfigDefaults::tilingBehaviorGroup(), ConfigDefaults::dragBehaviorKey()));
}
int Settings::autotileDragBehaviorInt() const
{
    return static_cast<int>(autotileDragBehavior());
}
void Settings::setAutotileDragBehavior(AutotileDragBehavior behavior)
{
    const int before = m_store->read<int>(ConfigDefaults::tilingBehaviorGroup(), ConfigDefaults::dragBehaviorKey());
    m_store->write(ConfigDefaults::tilingBehaviorGroup(), ConfigDefaults::dragBehaviorKey(),
                   static_cast<int>(behavior));
    const int after = m_store->read<int>(ConfigDefaults::tilingBehaviorGroup(), ConfigDefaults::dragBehaviorKey());
    if (after == before) {
        return;
    }
    Q_EMIT autotileDragBehaviorChanged();
    Q_EMIT settingsChanged();
}
void Settings::setAutotileDragBehaviorInt(int behavior)
{
    if (behavior == static_cast<int>(AutotileDragBehavior::Float)
        || behavior == static_cast<int>(AutotileDragBehavior::Reorder)) {
        setAutotileDragBehavior(static_cast<AutotileDragBehavior>(behavior));
    }
}

AutotileOverflowBehavior Settings::autotileOverflowBehavior() const
{
    return static_cast<AutotileOverflowBehavior>(
        m_store->read<int>(ConfigDefaults::tilingBehaviorGroup(), ConfigDefaults::overflowBehaviorKey()));
}
int Settings::autotileOverflowBehaviorInt() const
{
    return static_cast<int>(autotileOverflowBehavior());
}
void Settings::setAutotileOverflowBehavior(AutotileOverflowBehavior behavior)
{
    const int before = m_store->read<int>(ConfigDefaults::tilingBehaviorGroup(), ConfigDefaults::overflowBehaviorKey());
    m_store->write(ConfigDefaults::tilingBehaviorGroup(), ConfigDefaults::overflowBehaviorKey(),
                   static_cast<int>(behavior));
    const int after = m_store->read<int>(ConfigDefaults::tilingBehaviorGroup(), ConfigDefaults::overflowBehaviorKey());
    if (after == before) {
        return;
    }
    Q_EMIT autotileOverflowBehaviorChanged();
    Q_EMIT settingsChanged();
}
void Settings::setAutotileOverflowBehaviorInt(int behavior)
{
    if (behavior == static_cast<int>(AutotileOverflowBehavior::Float)
        || behavior == static_cast<int>(AutotileOverflowBehavior::Unlimited)) {
        setAutotileOverflowBehavior(static_cast<AutotileOverflowBehavior>(behavior));
    }
}

QStringList Settings::lockedScreens() const
{
    return parseCommaList(
        m_store->read<QString>(ConfigDefaults::tilingBehaviorGroup(), ConfigDefaults::lockedScreensKey()));
}
void Settings::setLockedScreens(const QStringList& screens)
{
    // Post-write compare — see setDisabledMonitors for the canonicalisation
    // rationale.
    const QString before =
        m_store->read<QString>(ConfigDefaults::tilingBehaviorGroup(), ConfigDefaults::lockedScreensKey());
    m_store->write(ConfigDefaults::tilingBehaviorGroup(), ConfigDefaults::lockedScreensKey(),
                   screens.join(QLatin1Char(',')));
    const QString after =
        m_store->read<QString>(ConfigDefaults::tilingBehaviorGroup(), ConfigDefaults::lockedScreensKey());
    if (before == after) {
        return;
    }
    Q_EMIT lockedScreensChanged();
    Q_EMIT settingsChanged();
}

bool Settings::isScreenLocked(const QString& screenIdOrName) const
{
    return isContextLocked(screenIdOrName, 0, QString());
}
void Settings::setScreenLocked(const QString& screenIdOrName, bool locked)
{
    setContextLocked(screenIdOrName, 0, QString(), locked);
}

bool Settings::isContextLocked(const QString& screenIdOrName, int virtualDesktop, const QString& activity) const
{
    const QStringList locked = lockedScreens();
    QStringList namesToCheck = {screenIdOrName};
    if (Utils::isConnectorName(screenIdOrName)) {
        const QString resolved = Utils::screenIdForName(screenIdOrName);
        if (resolved != screenIdOrName) {
            namesToCheck.append(resolved);
        }
    } else {
        const QString connector = Utils::screenNameForId(screenIdOrName);
        if (!connector.isEmpty() && connector != screenIdOrName) {
            namesToCheck.append(connector);
        }
    }
    for (const QString& name : std::as_const(namesToCheck)) {
        if (virtualDesktop > 0 && !activity.isEmpty()) {
            const QString k =
                name + QStringLiteral(":") + QString::number(virtualDesktop) + QStringLiteral(":") + activity;
            if (locked.contains(k)) {
                return true;
            }
        }
        if (virtualDesktop > 0) {
            const QString k = name + QStringLiteral(":") + QString::number(virtualDesktop);
            if (locked.contains(k)) {
                return true;
            }
        }
        if (locked.contains(name)) {
            return true;
        }
    }
    return false;
}

void Settings::setContextLocked(const QString& screenIdOrName, int virtualDesktop, const QString& activity, bool locked)
{
    QString key = screenIdOrName;
    if (virtualDesktop > 0) {
        key += QStringLiteral(":") + QString::number(virtualDesktop);
        if (!activity.isEmpty()) {
            key += QStringLiteral(":") + activity;
        }
    }
    QStringList current = lockedScreens();
    if (locked && !current.contains(key)) {
        current.append(key);
        setLockedScreens(current);
    } else if (!locked && current.removeAll(key) > 0) {
        setLockedScreens(current);
    }
}

QVariantList Settings::autotileDragInsertTriggers() const
{
    return m_store->readVariant(ConfigDefaults::tilingBehaviorGroup(), ConfigDefaults::triggersKey()).toList();
}
void Settings::setAutotileDragInsertTriggers(const QVariantList& triggers)
{
    // Post-write compare — see setDragActivationTriggers for the
    // canonicalisation rationale.
    const QVariantList before = autotileDragInsertTriggers();
    m_store->write(ConfigDefaults::tilingBehaviorGroup(), ConfigDefaults::triggersKey(),
                   triggers.mid(0, MaxTriggersPerAction));
    const QVariantList after = autotileDragInsertTriggers();
    if (before == after) {
        return;
    }
    Q_EMIT autotileDragInsertTriggersChanged();
    Q_EMIT settingsChanged();
}

PZ_STORE_GET(bool, autotileDragInsertToggle, tilingBehaviorGroup, toggleActivationKey, bool)
PZ_STORE_SET_BOOL(setAutotileDragInsertToggle, tilingBehaviorGroup, toggleActivationKey,
                  autotileDragInsertToggleChanged)

// Tiling.Appearance
PZ_STORE_GET(QColor, autotileBorderColor, tilingAppearanceColorsGroup, activeKey, QColor)
PZ_STORE_SET_COLOR(setAutotileBorderColor, tilingAppearanceColorsGroup, activeKey, autotileBorderColorChanged)
PZ_STORE_GET(QColor, autotileInactiveBorderColor, tilingAppearanceColorsGroup, inactiveKey, QColor)
PZ_STORE_SET_COLOR(setAutotileInactiveBorderColor, tilingAppearanceColorsGroup, inactiveKey,
                   autotileInactiveBorderColorChanged)

PZ_STORE_GET(bool, autotileUseSystemBorderColors, tilingAppearanceColorsGroup, useSystemKey, bool)
void Settings::setAutotileUseSystemBorderColors(bool use)
{
    if (autotileUseSystemBorderColors() == use) {
        return;
    }
    m_store->write(ConfigDefaults::tilingAppearanceColorsGroup(), ConfigDefaults::useSystemKey(), use);
    if (use) {
        applyAutotileBorderSystemColor();
    }
    Q_EMIT autotileUseSystemBorderColorsChanged();
    Q_EMIT settingsChanged();
}

PZ_STORE_GET(bool, autotileHideTitleBars, tilingAppearanceDecorationsGroup, hideTitleBarsKey, bool)
PZ_STORE_SET_BOOL(setAutotileHideTitleBars, tilingAppearanceDecorationsGroup, hideTitleBarsKey,
                  autotileHideTitleBarsChanged)
PZ_STORE_GET(bool, autotileShowBorder, tilingAppearanceBordersGroup, showBorderKey, bool)
PZ_STORE_SET_BOOL(setAutotileShowBorder, tilingAppearanceBordersGroup, showBorderKey, autotileShowBorderChanged)
PZ_STORE_GET(int, autotileBorderWidth, tilingAppearanceBordersGroup, widthKey, int)
PZ_STORE_SET_INT(setAutotileBorderWidth, tilingAppearanceBordersGroup, widthKey, autotileBorderWidthChanged)
PZ_STORE_GET(int, autotileBorderRadius, tilingAppearanceBordersGroup, radiusKey, int)
PZ_STORE_SET_INT(setAutotileBorderRadius, tilingAppearanceBordersGroup, radiusKey, autotileBorderRadiusChanged)

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

// ── Shortcuts (PhosphorConfig::Store-backed) ────────────────────────────────
// Every shortcut is a flat string; schema registers them without validators.
// Change-detection goes through the PZ_STORE_SET_STRING macro.

// Global shortcuts — meta actions, zone navigation, snap-to-zone numbered,
// layout rotation, virtual-screen swap/rotate.
PZ_STORE_GET(QString, openEditorShortcut, shortcutsGlobalGroup, openEditorKey, QString)
PZ_STORE_SET_STRING(setOpenEditorShortcut, shortcutsGlobalGroup, openEditorKey, openEditorShortcutChanged)
PZ_STORE_GET(QString, openSettingsShortcut, shortcutsGlobalGroup, openSettingsKey, QString)
PZ_STORE_SET_STRING(setOpenSettingsShortcut, shortcutsGlobalGroup, openSettingsKey, openSettingsShortcutChanged)
PZ_STORE_GET(QString, previousLayoutShortcut, shortcutsGlobalGroup, previousLayoutKey, QString)
PZ_STORE_SET_STRING(setPreviousLayoutShortcut, shortcutsGlobalGroup, previousLayoutKey, previousLayoutShortcutChanged)
PZ_STORE_GET(QString, nextLayoutShortcut, shortcutsGlobalGroup, nextLayoutKey, QString)
PZ_STORE_SET_STRING(setNextLayoutShortcut, shortcutsGlobalGroup, nextLayoutKey, nextLayoutShortcutChanged)

// quickLayoutN and snapToZoneN arrays — dispatch to per-index key.
// Each wrapper reads/writes the same store using ConfigDefaults::quickLayoutKey(n).

#define PZ_QUICK_LAYOUT(N)                                                                                             \
    QString Settings::quickLayout##N##Shortcut() const                                                                 \
    {                                                                                                                  \
        return m_store->read<QString>(ConfigDefaults::shortcutsGlobalGroup(), ConfigDefaults::quickLayoutKey(N));      \
    }                                                                                                                  \
    void Settings::setQuickLayout##N##Shortcut(const QString& shortcut)                                                \
    {                                                                                                                  \
        setQuickLayoutShortcut(N - 1, shortcut);                                                                       \
    }

PZ_QUICK_LAYOUT(1)
PZ_QUICK_LAYOUT(2)
PZ_QUICK_LAYOUT(3)
PZ_QUICK_LAYOUT(4)
PZ_QUICK_LAYOUT(5)
PZ_QUICK_LAYOUT(6)
PZ_QUICK_LAYOUT(7)
PZ_QUICK_LAYOUT(8)
PZ_QUICK_LAYOUT(9)
#undef PZ_QUICK_LAYOUT

QString Settings::quickLayoutShortcut(int index) const
{
    if (index < 0 || index >= 9) {
        return {};
    }
    return m_store->read<QString>(ConfigDefaults::shortcutsGlobalGroup(), ConfigDefaults::quickLayoutKey(index + 1));
}

void Settings::setQuickLayoutShortcut(int index, const QString& shortcut)
{
    if (index < 0 || index >= 9) {
        return;
    }
    const QString key = ConfigDefaults::quickLayoutKey(index + 1);
    if (m_store->read<QString>(ConfigDefaults::shortcutsGlobalGroup(), key) == shortcut) {
        return;
    }
    m_store->write(ConfigDefaults::shortcutsGlobalGroup(), key, shortcut);

    static constexpr ShortcutSignalFn signals[9] = {
        &Settings::quickLayout1ShortcutChanged, &Settings::quickLayout2ShortcutChanged,
        &Settings::quickLayout3ShortcutChanged, &Settings::quickLayout4ShortcutChanged,
        &Settings::quickLayout5ShortcutChanged, &Settings::quickLayout6ShortcutChanged,
        &Settings::quickLayout7ShortcutChanged, &Settings::quickLayout8ShortcutChanged,
        &Settings::quickLayout9ShortcutChanged,
    };
    Q_EMIT(this->*signals[index])();
    Q_EMIT settingsChanged();
}

// Navigation shortcuts.
PZ_STORE_GET(QString, moveWindowLeftShortcut, shortcutsGlobalGroup, moveWindowLeftKey, QString)
PZ_STORE_SET_STRING(setMoveWindowLeftShortcut, shortcutsGlobalGroup, moveWindowLeftKey, moveWindowLeftShortcutChanged)
PZ_STORE_GET(QString, moveWindowRightShortcut, shortcutsGlobalGroup, moveWindowRightKey, QString)
PZ_STORE_SET_STRING(setMoveWindowRightShortcut, shortcutsGlobalGroup, moveWindowRightKey,
                    moveWindowRightShortcutChanged)
PZ_STORE_GET(QString, moveWindowUpShortcut, shortcutsGlobalGroup, moveWindowUpKey, QString)
PZ_STORE_SET_STRING(setMoveWindowUpShortcut, shortcutsGlobalGroup, moveWindowUpKey, moveWindowUpShortcutChanged)
PZ_STORE_GET(QString, moveWindowDownShortcut, shortcutsGlobalGroup, moveWindowDownKey, QString)
PZ_STORE_SET_STRING(setMoveWindowDownShortcut, shortcutsGlobalGroup, moveWindowDownKey, moveWindowDownShortcutChanged)
PZ_STORE_GET(QString, focusZoneLeftShortcut, shortcutsGlobalGroup, focusZoneLeftKey, QString)
PZ_STORE_SET_STRING(setFocusZoneLeftShortcut, shortcutsGlobalGroup, focusZoneLeftKey, focusZoneLeftShortcutChanged)
PZ_STORE_GET(QString, focusZoneRightShortcut, shortcutsGlobalGroup, focusZoneRightKey, QString)
PZ_STORE_SET_STRING(setFocusZoneRightShortcut, shortcutsGlobalGroup, focusZoneRightKey, focusZoneRightShortcutChanged)
PZ_STORE_GET(QString, focusZoneUpShortcut, shortcutsGlobalGroup, focusZoneUpKey, QString)
PZ_STORE_SET_STRING(setFocusZoneUpShortcut, shortcutsGlobalGroup, focusZoneUpKey, focusZoneUpShortcutChanged)
PZ_STORE_GET(QString, focusZoneDownShortcut, shortcutsGlobalGroup, focusZoneDownKey, QString)
PZ_STORE_SET_STRING(setFocusZoneDownShortcut, shortcutsGlobalGroup, focusZoneDownKey, focusZoneDownShortcutChanged)
PZ_STORE_GET(QString, pushToEmptyZoneShortcut, shortcutsGlobalGroup, pushToEmptyZoneKey, QString)
PZ_STORE_SET_STRING(setPushToEmptyZoneShortcut, shortcutsGlobalGroup, pushToEmptyZoneKey,
                    pushToEmptyZoneShortcutChanged)
PZ_STORE_GET(QString, restoreWindowSizeShortcut, shortcutsGlobalGroup, restoreWindowSizeKey, QString)
PZ_STORE_SET_STRING(setRestoreWindowSizeShortcut, shortcutsGlobalGroup, restoreWindowSizeKey,
                    restoreWindowSizeShortcutChanged)
PZ_STORE_GET(QString, toggleWindowFloatShortcut, shortcutsGlobalGroup, toggleWindowFloatKey, QString)
PZ_STORE_SET_STRING(setToggleWindowFloatShortcut, shortcutsGlobalGroup, toggleWindowFloatKey,
                    toggleWindowFloatShortcutChanged)
PZ_STORE_GET(QString, swapWindowLeftShortcut, shortcutsGlobalGroup, swapWindowLeftKey, QString)
PZ_STORE_SET_STRING(setSwapWindowLeftShortcut, shortcutsGlobalGroup, swapWindowLeftKey, swapWindowLeftShortcutChanged)
PZ_STORE_GET(QString, swapWindowRightShortcut, shortcutsGlobalGroup, swapWindowRightKey, QString)
PZ_STORE_SET_STRING(setSwapWindowRightShortcut, shortcutsGlobalGroup, swapWindowRightKey,
                    swapWindowRightShortcutChanged)
PZ_STORE_GET(QString, swapWindowUpShortcut, shortcutsGlobalGroup, swapWindowUpKey, QString)
PZ_STORE_SET_STRING(setSwapWindowUpShortcut, shortcutsGlobalGroup, swapWindowUpKey, swapWindowUpShortcutChanged)
PZ_STORE_GET(QString, swapWindowDownShortcut, shortcutsGlobalGroup, swapWindowDownKey, QString)
PZ_STORE_SET_STRING(setSwapWindowDownShortcut, shortcutsGlobalGroup, swapWindowDownKey, swapWindowDownShortcutChanged)

// snapToZone1..9 — same dispatch pattern as quickLayout.
#define PZ_SNAP_TO_ZONE(N)                                                                                             \
    QString Settings::snapToZone##N##Shortcut() const                                                                  \
    {                                                                                                                  \
        return m_store->read<QString>(ConfigDefaults::shortcutsGlobalGroup(), ConfigDefaults::snapToZoneKey(N));       \
    }                                                                                                                  \
    void Settings::setSnapToZone##N##Shortcut(const QString& shortcut)                                                 \
    {                                                                                                                  \
        setSnapToZoneShortcut(N - 1, shortcut);                                                                        \
    }

PZ_SNAP_TO_ZONE(1)
PZ_SNAP_TO_ZONE(2)
PZ_SNAP_TO_ZONE(3)
PZ_SNAP_TO_ZONE(4)
PZ_SNAP_TO_ZONE(5)
PZ_SNAP_TO_ZONE(6)
PZ_SNAP_TO_ZONE(7)
PZ_SNAP_TO_ZONE(8)
PZ_SNAP_TO_ZONE(9)
#undef PZ_SNAP_TO_ZONE

QString Settings::snapToZoneShortcut(int index) const
{
    if (index < 0 || index >= 9) {
        return {};
    }
    return m_store->read<QString>(ConfigDefaults::shortcutsGlobalGroup(), ConfigDefaults::snapToZoneKey(index + 1));
}

void Settings::setSnapToZoneShortcut(int index, const QString& shortcut)
{
    if (index < 0 || index >= 9) {
        return;
    }
    const QString key = ConfigDefaults::snapToZoneKey(index + 1);
    if (m_store->read<QString>(ConfigDefaults::shortcutsGlobalGroup(), key) == shortcut) {
        return;
    }
    m_store->write(ConfigDefaults::shortcutsGlobalGroup(), key, shortcut);
    static constexpr ShortcutSignalFn signals[9] = {
        &Settings::snapToZone1ShortcutChanged, &Settings::snapToZone2ShortcutChanged,
        &Settings::snapToZone3ShortcutChanged, &Settings::snapToZone4ShortcutChanged,
        &Settings::snapToZone5ShortcutChanged, &Settings::snapToZone6ShortcutChanged,
        &Settings::snapToZone7ShortcutChanged, &Settings::snapToZone8ShortcutChanged,
        &Settings::snapToZone9ShortcutChanged,
    };
    Q_EMIT(this->*signals[index])();
    Q_EMIT settingsChanged();
}

PZ_STORE_GET(QString, rotateWindowsClockwiseShortcut, shortcutsGlobalGroup, rotateWindowsClockwiseKey, QString)
PZ_STORE_SET_STRING(setRotateWindowsClockwiseShortcut, shortcutsGlobalGroup, rotateWindowsClockwiseKey,
                    rotateWindowsClockwiseShortcutChanged)
PZ_STORE_GET(QString, rotateWindowsCounterclockwiseShortcut, shortcutsGlobalGroup, rotateWindowsCounterclockwiseKey,
             QString)
PZ_STORE_SET_STRING(setRotateWindowsCounterclockwiseShortcut, shortcutsGlobalGroup, rotateWindowsCounterclockwiseKey,
                    rotateWindowsCounterclockwiseShortcutChanged)
PZ_STORE_GET(QString, cycleWindowForwardShortcut, shortcutsGlobalGroup, cycleWindowForwardKey, QString)
PZ_STORE_SET_STRING(setCycleWindowForwardShortcut, shortcutsGlobalGroup, cycleWindowForwardKey,
                    cycleWindowForwardShortcutChanged)
PZ_STORE_GET(QString, cycleWindowBackwardShortcut, shortcutsGlobalGroup, cycleWindowBackwardKey, QString)
PZ_STORE_SET_STRING(setCycleWindowBackwardShortcut, shortcutsGlobalGroup, cycleWindowBackwardKey,
                    cycleWindowBackwardShortcutChanged)
PZ_STORE_GET(QString, resnapToNewLayoutShortcut, shortcutsGlobalGroup, resnapToNewLayoutKey, QString)
PZ_STORE_SET_STRING(setResnapToNewLayoutShortcut, shortcutsGlobalGroup, resnapToNewLayoutKey,
                    resnapToNewLayoutShortcutChanged)
PZ_STORE_GET(QString, snapAllWindowsShortcut, shortcutsGlobalGroup, snapAllWindowsKey, QString)
PZ_STORE_SET_STRING(setSnapAllWindowsShortcut, shortcutsGlobalGroup, snapAllWindowsKey, snapAllWindowsShortcutChanged)
PZ_STORE_GET(QString, layoutPickerShortcut, shortcutsGlobalGroup, layoutPickerKey, QString)
PZ_STORE_SET_STRING(setLayoutPickerShortcut, shortcutsGlobalGroup, layoutPickerKey, layoutPickerShortcutChanged)
PZ_STORE_GET(QString, toggleLayoutLockShortcut, shortcutsGlobalGroup, toggleLayoutLockKey, QString)
PZ_STORE_SET_STRING(setToggleLayoutLockShortcut, shortcutsGlobalGroup, toggleLayoutLockKey,
                    toggleLayoutLockShortcutChanged)
PZ_STORE_GET(QString, swapVirtualScreenLeftShortcut, shortcutsGlobalGroup, swapVirtualScreenLeftKey, QString)
PZ_STORE_SET_STRING(setSwapVirtualScreenLeftShortcut, shortcutsGlobalGroup, swapVirtualScreenLeftKey,
                    swapVirtualScreenLeftShortcutChanged)
PZ_STORE_GET(QString, swapVirtualScreenRightShortcut, shortcutsGlobalGroup, swapVirtualScreenRightKey, QString)
PZ_STORE_SET_STRING(setSwapVirtualScreenRightShortcut, shortcutsGlobalGroup, swapVirtualScreenRightKey,
                    swapVirtualScreenRightShortcutChanged)
PZ_STORE_GET(QString, swapVirtualScreenUpShortcut, shortcutsGlobalGroup, swapVirtualScreenUpKey, QString)
PZ_STORE_SET_STRING(setSwapVirtualScreenUpShortcut, shortcutsGlobalGroup, swapVirtualScreenUpKey,
                    swapVirtualScreenUpShortcutChanged)
PZ_STORE_GET(QString, swapVirtualScreenDownShortcut, shortcutsGlobalGroup, swapVirtualScreenDownKey, QString)
PZ_STORE_SET_STRING(setSwapVirtualScreenDownShortcut, shortcutsGlobalGroup, swapVirtualScreenDownKey,
                    swapVirtualScreenDownShortcutChanged)
PZ_STORE_GET(QString, rotateVirtualScreensClockwiseShortcut, shortcutsGlobalGroup, rotateVirtualScreensClockwiseKey,
             QString)
PZ_STORE_SET_STRING(setRotateVirtualScreensClockwiseShortcut, shortcutsGlobalGroup, rotateVirtualScreensClockwiseKey,
                    rotateVirtualScreensClockwiseShortcutChanged)
PZ_STORE_GET(QString, rotateVirtualScreensCounterclockwiseShortcut, shortcutsGlobalGroup,
             rotateVirtualScreensCounterclockwiseKey, QString)
PZ_STORE_SET_STRING(setRotateVirtualScreensCounterclockwiseShortcut, shortcutsGlobalGroup,
                    rotateVirtualScreensCounterclockwiseKey, rotateVirtualScreensCounterclockwiseShortcutChanged)

// Tiling shortcuts.
PZ_STORE_GET(QString, autotileToggleShortcut, shortcutsTilingGroup, toggleKey, QString)
PZ_STORE_SET_STRING(setAutotileToggleShortcut, shortcutsTilingGroup, toggleKey, autotileToggleShortcutChanged)
PZ_STORE_GET(QString, autotileFocusMasterShortcut, shortcutsTilingGroup, focusMasterKey, QString)
PZ_STORE_SET_STRING(setAutotileFocusMasterShortcut, shortcutsTilingGroup, focusMasterKey,
                    autotileFocusMasterShortcutChanged)
PZ_STORE_GET(QString, autotileSwapMasterShortcut, shortcutsTilingGroup, swapMasterKey, QString)
PZ_STORE_SET_STRING(setAutotileSwapMasterShortcut, shortcutsTilingGroup, swapMasterKey,
                    autotileSwapMasterShortcutChanged)
PZ_STORE_GET(QString, autotileIncMasterRatioShortcut, shortcutsTilingGroup, incMasterRatioKey, QString)
PZ_STORE_SET_STRING(setAutotileIncMasterRatioShortcut, shortcutsTilingGroup, incMasterRatioKey,
                    autotileIncMasterRatioShortcutChanged)
PZ_STORE_GET(QString, autotileDecMasterRatioShortcut, shortcutsTilingGroup, decMasterRatioKey, QString)
PZ_STORE_SET_STRING(setAutotileDecMasterRatioShortcut, shortcutsTilingGroup, decMasterRatioKey,
                    autotileDecMasterRatioShortcutChanged)
PZ_STORE_GET(QString, autotileIncMasterCountShortcut, shortcutsTilingGroup, incMasterCountKey, QString)
PZ_STORE_SET_STRING(setAutotileIncMasterCountShortcut, shortcutsTilingGroup, incMasterCountKey,
                    autotileIncMasterCountShortcutChanged)
PZ_STORE_GET(QString, autotileDecMasterCountShortcut, shortcutsTilingGroup, decMasterCountKey, QString)
PZ_STORE_SET_STRING(setAutotileDecMasterCountShortcut, shortcutsTilingGroup, decMasterCountKey,
                    autotileDecMasterCountShortcutChanged)
PZ_STORE_GET(QString, autotileRetileShortcut, shortcutsTilingGroup, retileKey, QString)
PZ_STORE_SET_STRING(setAutotileRetileShortcut, shortcutsTilingGroup, retileKey, autotileRetileShortcutChanged)

// Editor shortcuts.
PZ_STORE_GET(QString, editorDuplicateShortcut, editorShortcutsGroup, duplicateKey, QString)
PZ_STORE_SET_STRING(setEditorDuplicateShortcut, editorShortcutsGroup, duplicateKey, editorDuplicateShortcutChanged)
PZ_STORE_GET(QString, editorSplitHorizontalShortcut, editorShortcutsGroup, splitHorizontalKey, QString)
PZ_STORE_SET_STRING(setEditorSplitHorizontalShortcut, editorShortcutsGroup, splitHorizontalKey,
                    editorSplitHorizontalShortcutChanged)
PZ_STORE_GET(QString, editorSplitVerticalShortcut, editorShortcutsGroup, splitVerticalKey, QString)
PZ_STORE_SET_STRING(setEditorSplitVerticalShortcut, editorShortcutsGroup, splitVerticalKey,
                    editorSplitVerticalShortcutChanged)
PZ_STORE_GET(QString, editorFillShortcut, editorShortcutsGroup, fillKey, QString)
PZ_STORE_SET_STRING(setEditorFillShortcut, editorShortcutsGroup, fillKey, editorFillShortcutChanged)

// Editor snapping + fill-on-drop toggles.
PZ_STORE_GET(bool, editorGridSnappingEnabled, editorSnappingGroup, gridEnabledKey, bool)
PZ_STORE_SET_BOOL(setEditorGridSnappingEnabled, editorSnappingGroup, gridEnabledKey, editorGridSnappingEnabledChanged)
PZ_STORE_GET(bool, editorEdgeSnappingEnabled, editorSnappingGroup, edgeEnabledKey, bool)
PZ_STORE_SET_BOOL(setEditorEdgeSnappingEnabled, editorSnappingGroup, edgeEnabledKey, editorEdgeSnappingEnabledChanged)
PZ_STORE_GET(qreal, editorSnapIntervalX, editorSnappingGroup, intervalXKey, double)
PZ_STORE_SET_DOUBLE(setEditorSnapIntervalX, editorSnappingGroup, intervalXKey, editorSnapIntervalXChanged)
PZ_STORE_GET(qreal, editorSnapIntervalY, editorSnappingGroup, intervalYKey, double)
PZ_STORE_SET_DOUBLE(setEditorSnapIntervalY, editorSnappingGroup, intervalYKey, editorSnapIntervalYChanged)
PZ_STORE_GET(int, editorSnapOverrideModifier, editorSnappingGroup, overrideModifierKey, int)
PZ_STORE_SET_INT(setEditorSnapOverrideModifier, editorSnappingGroup, overrideModifierKey,
                 editorSnapOverrideModifierChanged)
PZ_STORE_GET(bool, fillOnDropEnabled, editorFillOnDropGroup, enabledKey, bool)
PZ_STORE_SET_BOOL(setFillOnDropEnabled, editorFillOnDropGroup, enabledKey, fillOnDropEnabledChanged)
PZ_STORE_GET(int, fillOnDropModifier, editorFillOnDropGroup, modifierKey, int)
PZ_STORE_SET_INT(setFillOnDropModifier, editorFillOnDropGroup, modifierKey, fillOnDropModifierChanged)

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
    highlight.setAlpha(::PhosphorZones::ZoneDefaults::HighlightAlpha);
    setHighlightColor(highlight);

    QColor inactive = pal.color(QPalette::Active, QPalette::Text);
    inactive.setAlpha(::PhosphorZones::ZoneDefaults::InactiveAlpha);
    setInactiveColor(inactive);

    QColor border = pal.color(QPalette::Active, QPalette::Text);
    border.setAlpha(::PhosphorZones::ZoneDefaults::BorderAlpha);
    setBorderColor(border);

    const QColor fontColor = pal.color(QPalette::Active, QPalette::Text);
    setLabelFontColor(fontColor);
}

void Settings::applyAutotileBorderSystemColor()
{
    // Use the exact snapping zone highlight/inactive colors including their
    // alpha. Route through the setters so the Store stays the source of
    // truth (and the NOTIFY signals fire as a side effect).
    setAutotileBorderColor(highlightColor());
    setAutotileInactiveBorderColor(inactiveColor());
}

#undef PZ_STORE_GET
#undef PZ_STORE_SET_BOOL
#undef PZ_STORE_SET_INT
#undef PZ_STORE_SET_DOUBLE
#undef PZ_STORE_SET_COLOR
#undef PZ_STORE_SET_STRING

} // namespace PlasmaZones
