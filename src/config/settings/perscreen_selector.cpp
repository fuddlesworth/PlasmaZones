// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// The per-screen override stores of the two drag selectors: the snapping
// popup (ZoneSelector:) and the strip-mode popup on scrolling screens
// (ScrollingZoneSelector:). Split out of perscreen.cpp for file-size; the
// shared entry-lookup helpers and boundary guards live in perscreen_detail.h,
// and the load/save passes stay in perscreen.cpp, driving loadPerScreenGroup
// with the key tables and validators this file exports.
//
// The two stores share ONE key vocabulary (ZoneSelectorConfigKey) and resolve
// to ONE struct (ZoneSelectorConfig), because both popups are laid out by
// computeZoneSelectorLayout. They are still separate stores under separate
// group prefixes, and the strip store admits only a subset of the vocabulary.

#include "config/settings.h"
#include "config/settings/perscreen_detail.h"
#include "config/configdefaults.h"
#include "core/platform/logging.h"

#include <PhosphorIdentity/VirtualScreenId.h>

#include <algorithm>
#include <iterator>

namespace PlasmaZones {

using PerScreenDetail::applyPerScreenSetting;
using PerScreenDetail::boundedInt;
using PerScreenDetail::enumInRange;
using PerScreenDetail::findPerScreenEntry;
using PerScreenDetail::removePerScreenEntry;

namespace PerScreenDetail {

const QLatin1String kZoneSelectorKeys[9] = {
    QLatin1String(ZoneSelectorConfigKey::Position),          QLatin1String(ZoneSelectorConfigKey::LayoutMode),
    QLatin1String(ZoneSelectorConfigKey::SizeMode),          QLatin1String(ZoneSelectorConfigKey::MaxRows),
    QLatin1String(ZoneSelectorConfigKey::PreviewWidth),      QLatin1String(ZoneSelectorConfigKey::PreviewHeight),
    QLatin1String(ZoneSelectorConfigKey::PreviewLockAspect), QLatin1String(ZoneSelectorConfigKey::GridColumns),
    QLatin1String(ZoneSelectorConfigKey::TriggerDistance),
};

// The strip selector's overrides reuse the vocabulary above minus the three
// grid-arrangement keys, which a single horizontal card row has nothing to do
// with. The subset is ENFORCED on write, not merely left out of the load list:
// the write path shares validateZoneSelectorValue with the snapping twin,
// which would accept a LayoutMode pushed over D-Bus and let it displace the
// Horizontal the strip resolver stamps.
const QLatin1String kStripSelectorKeys[6] = {
    QLatin1String(ZoneSelectorConfigKey::Position),          QLatin1String(ZoneSelectorConfigKey::SizeMode),
    QLatin1String(ZoneSelectorConfigKey::PreviewWidth),      QLatin1String(ZoneSelectorConfigKey::PreviewHeight),
    QLatin1String(ZoneSelectorConfigKey::PreviewLockAspect), QLatin1String(ZoneSelectorConfigKey::TriggerDistance),
};

QVariant readZoneSelectorEntry(PhosphorConfig::IGroup& group, const QString& key)
{
    namespace K = ZoneSelectorConfigKey;
    if (key == QLatin1String(K::PreviewLockAspect))
        return QVariant(group.readBool(key, ConfigDefaults::previewLockAspect()));
    return QVariant(group.readInt(key, 0));
}

QVariant validateZoneSelectorValue(const QString& key, const QVariant& value)
{
    namespace K = ZoneSelectorConfigKey;
    if (key == QLatin1String(K::Position)) {
        return enumInRange(value, static_cast<int>(ZoneSelectorPosition::BottomRight));
    }
    if (key == QLatin1String(K::LayoutMode)) {
        return enumInRange(value, static_cast<int>(ZoneSelectorLayoutMode::Vertical));
    }
    if (key == QLatin1String(K::SizeMode)) {
        return enumInRange(value, static_cast<int>(ZoneSelectorSizeMode::Manual));
    }
    if (key == QLatin1String(K::MaxRows))
        return boundedInt(value, ConfigDefaults::maxRowsMin(), ConfigDefaults::maxRowsMax());
    if (key == QLatin1String(K::PreviewWidth))
        return boundedInt(value, ConfigDefaults::previewWidthMin(), ConfigDefaults::previewWidthMax());
    if (key == QLatin1String(K::PreviewHeight))
        return boundedInt(value, ConfigDefaults::previewHeightMin(), ConfigDefaults::previewHeightMax());
    if (key == QLatin1String(K::PreviewLockAspect))
        // Type-gated like every sibling arm: bare toBool() would convert ANY
        // payload type, making this the one key a malformed D-Bus write
        // silently "succeeds" on.
        return value.typeId() == QMetaType::Bool ? QVariant(value.toBool()) : QVariant();
    if (key == QLatin1String(K::GridColumns))
        return boundedInt(value, ConfigDefaults::gridColumnsMin(), ConfigDefaults::gridColumnsMax());
    if (key == QLatin1String(K::TriggerDistance))
        return boundedInt(value, ConfigDefaults::triggerDistanceMin(), ConfigDefaults::triggerDistanceMax());
    return QVariant();
}

QVariant validateStripSelectorValue(const QString& key, const QVariant& value)
{
    const bool inSubset =
        std::any_of(std::begin(kStripSelectorKeys), std::end(kStripSelectorKeys), [&key](QLatin1String allowed) {
            return key == allowed;
        });
    return inSubset ? validateZoneSelectorValue(key, value) : QVariant();
}

} // namespace PerScreenDetail

namespace {

using PerScreenDetail::validateZoneSelectorValue;

void applyPerScreenOverrides(ZoneSelectorConfig& config, const QVariantMap& overrides)
{
    namespace K = ZoneSelectorConfigKey;
    auto applyInt = [&](const char* key, int& field) {
        auto it = overrides.constFind(QLatin1String(key));
        if (it != overrides.constEnd()) {
            QVariant v = validateZoneSelectorValue(QLatin1String(key), it.value());
            if (v.isValid()) {
                field = v.toInt();
            }
        }
    };
    applyInt(K::Position, config.position);
    applyInt(K::LayoutMode, config.layoutMode);
    applyInt(K::SizeMode, config.sizeMode);
    applyInt(K::MaxRows, config.maxRows);
    applyInt(K::PreviewWidth, config.previewWidth);
    applyInt(K::PreviewHeight, config.previewHeight);
    applyInt(K::GridColumns, config.gridColumns);
    applyInt(K::TriggerDistance, config.triggerDistance);
    auto lockIt = overrides.constFind(QLatin1String(K::PreviewLockAspect));
    if (lockIt != overrides.constEnd()) {
        QVariant v = validateZoneSelectorValue(QLatin1String(K::PreviewLockAspect), lockIt.value());
        if (v.isValid()) {
            config.previewLockAspect = v.toBool();
        }
    }
}

} // anonymous namespace

// ── Per-Screen PhosphorZones::Zone Selector Config ──────────────────────────

ZoneSelectorConfig Settings::resolvedZoneSelectorConfig(const QString& screenIdOrName) const
{
    ZoneSelectorConfig config = {static_cast<int>(zoneSelectorPosition()),
                                 static_cast<int>(zoneSelectorLayoutMode()),
                                 static_cast<int>(zoneSelectorSizeMode()),
                                 zoneSelectorMaxRows(),
                                 zoneSelectorPreviewWidth(),
                                 zoneSelectorPreviewHeight(),
                                 zoneSelectorPreviewLockAspect(),
                                 zoneSelectorGridColumns(),
                                 zoneSelectorTriggerDistance()};

    auto it = findPerScreenEntry(m_perScreenZoneSelectorSettings, screenIdOrName);
    // Virtual-screen fallback: an override stored on the physical monitor must
    // still apply when the selector runs on one of its virtual sub-screens.
    // Resolve a physical-monitor override for a virtual sub-screen id, matching
    // the virtual->physical fallback the per-screen reads elsewhere perform.
    if (it == m_perScreenZoneSelectorSettings.constEnd()
        && PhosphorIdentity::VirtualScreenId::isVirtual(screenIdOrName)) {
        it = findPerScreenEntry(m_perScreenZoneSelectorSettings,
                                PhosphorIdentity::VirtualScreenId::extractPhysicalId(screenIdOrName));
    }
    if (it == m_perScreenZoneSelectorSettings.constEnd()) {
        return config;
    }

    applyPerScreenOverrides(config, it.value());
    return config;
}

QVariantMap Settings::getPerScreenZoneSelectorSettings(const QString& screenIdOrName) const
{
    auto it = findPerScreenEntry(m_perScreenZoneSelectorSettings, screenIdOrName);
    return (it != m_perScreenZoneSelectorSettings.constEnd()) ? it.value() : QVariantMap();
}

void Settings::setPerScreenZoneSelectorSetting(const QString& screenIdOrName, const QString& key, const QVariant& value)
{
    if (screenIdOrName.isEmpty() || key.isEmpty()) {
        return;
    }

    QVariant validated = PerScreenDetail::validateZoneSelectorValue(key, value);
    if (!validated.isValid()) {
        // DEBUG, and the value is not echoed — the same reasoning as the autotile twin
        // in perscreen.cpp, reached over the same bus by the same caller. Demoting one of
        // the two and leaving the other is not a fix, it is a door left open beside a
        // locked one.
        qCDebug(lcConfig) << "Rejected per-screen zone selector setting" << key;
        return;
    }

    if (applyPerScreenSetting(m_perScreenZoneSelectorSettings, screenIdOrName, key, validated)) {
        Q_EMIT perScreenZoneSelectorSettingsChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::clearPerScreenZoneSelectorSettings(const QString& screenIdOrName)
{
    if (removePerScreenEntry(m_perScreenZoneSelectorSettings, screenIdOrName)) {
        Q_EMIT perScreenZoneSelectorSettingsChanged();
        Q_EMIT settingsChanged();
    }
}

bool Settings::hasPerScreenZoneSelectorSettings(const QString& screenIdOrName) const
{
    return findPerScreenEntry(m_perScreenZoneSelectorSettings, screenIdOrName)
        != m_perScreenZoneSelectorSettings.constEnd();
}

// ── Per-Screen Strip Selector Config ────────────────────────────────────────

ZoneSelectorConfig Settings::resolvedScrollingZoneSelectorConfig(const QString& screenIdOrName) const
{
    // The base implementation reads the global accessors and stamps
    // layoutMode = Horizontal; only the override merge is added here.
    ZoneSelectorConfig config = IScrollingZoneSelectorSettings::resolvedScrollingZoneSelectorConfig(screenIdOrName);

    auto it = findPerScreenEntry(m_perScreenScrollingZoneSelectorSettings, screenIdOrName);
    // Virtual-screen fallback, same reasoning as the zone-selector twin above.
    if (it == m_perScreenScrollingZoneSelectorSettings.constEnd()
        && PhosphorIdentity::VirtualScreenId::isVirtual(screenIdOrName)) {
        it = findPerScreenEntry(m_perScreenScrollingZoneSelectorSettings,
                                PhosphorIdentity::VirtualScreenId::extractPhysicalId(screenIdOrName));
    }
    if (it == m_perScreenScrollingZoneSelectorSettings.constEnd()) {
        return config;
    }

    // Safe to share the twin's merge: the writer admits only the six-key
    // subset, so the grid-arrangement branches it also carries find nothing.
    applyPerScreenOverrides(config, it.value());
    return config;
}

QVariantMap Settings::getPerScreenScrollingZoneSelectorSettings(const QString& screenIdOrName) const
{
    auto it = findPerScreenEntry(m_perScreenScrollingZoneSelectorSettings, screenIdOrName);
    return (it != m_perScreenScrollingZoneSelectorSettings.constEnd()) ? it.value() : QVariantMap();
}

void Settings::setPerScreenScrollingZoneSelectorSetting(const QString& screenIdOrName, const QString& key,
                                                        const QVariant& value)
{
    if (screenIdOrName.isEmpty() || key.isEmpty()) {
        return;
    }

    QVariant validated = PerScreenDetail::validateStripSelectorValue(key, value);
    if (!validated.isValid()) {
        // DEBUG and no value echo, for the same reason as the zone-selector twin.
        qCDebug(lcConfig) << "Rejected per-screen strip selector setting" << key;
        return;
    }

    if (applyPerScreenSetting(m_perScreenScrollingZoneSelectorSettings, screenIdOrName, key, validated)) {
        Q_EMIT perScreenScrollingZoneSelectorSettingsChanged();
        Q_EMIT settingsChanged();
    }
}

void Settings::clearPerScreenScrollingZoneSelectorSettings(const QString& screenIdOrName)
{
    if (removePerScreenEntry(m_perScreenScrollingZoneSelectorSettings, screenIdOrName)) {
        Q_EMIT perScreenScrollingZoneSelectorSettingsChanged();
        Q_EMIT settingsChanged();
    }
}

// DELIBERATE ASYMMETRY, shared with the zone-selector twin and the gap
// overrides: has/clear do NOT carry the resolved accessor's virtual→physical
// fallback. On a virtual sub-screen an override inherited from the physical
// parent therefore APPLIES (the resolver falls back) while has() reports no
// override of the sub-screen's OWN and clear() removes nothing — because a
// clear that fell back would delete the PARENT's override and change every
// sibling sub-screen. Changing this is a both-families design decision, not a
// per-store fix.
bool Settings::hasPerScreenScrollingZoneSelectorSettings(const QString& screenIdOrName) const
{
    return findPerScreenEntry(m_perScreenScrollingZoneSelectorSettings, screenIdOrName)
        != m_perScreenScrollingZoneSelectorSettings.constEnd();
}

// ── Per-card sub-domains of the two selector stores ─────────────────────────
// Each settings card's scope chip drives its OWN key subset (the invariant
// the autotile Algorithm pair states: "the card's dot/reset only touches its
// own keys"), so resetting the Position card cannot silently discard the
// Preview Size card's overrides or the reverse. The three predicates
// partition the nine-key vocabulary; the strip store simply never holds an
// arrangement key.

namespace {

bool isSelectorPositionKey(const QString& key)
{
    namespace K = ZoneSelectorConfigKey;
    return key == QLatin1String(K::Position) || key == QLatin1String(K::TriggerDistance);
}

bool isSelectorArrangementKey(const QString& key)
{
    namespace K = ZoneSelectorConfigKey;
    return key == QLatin1String(K::LayoutMode) || key == QLatin1String(K::GridColumns)
        || key == QLatin1String(K::MaxRows);
}

bool isSelectorSizeKey(const QString& key)
{
    namespace K = ZoneSelectorConfigKey;
    return key == QLatin1String(K::SizeMode) || key == QLatin1String(K::PreviewWidth)
        || key == QLatin1String(K::PreviewHeight) || key == QLatin1String(K::PreviewLockAspect);
}

} // namespace

bool Settings::hasPerScreenZoneSelectorPositionSettings(const QString& screenIdOrName) const
{
    return PerScreenDetail::hasPerScreenKeySubset(m_perScreenZoneSelectorSettings, screenIdOrName,
                                                  isSelectorPositionKey, /*wantMatch=*/true);
}
void Settings::clearPerScreenZoneSelectorPositionSettings(const QString& screenIdOrName)
{
    if (PerScreenDetail::clearPerScreenKeySubset(m_perScreenZoneSelectorSettings, screenIdOrName, isSelectorPositionKey,
                                                 /*clearMatch=*/true)) {
        Q_EMIT perScreenZoneSelectorSettingsChanged();
        Q_EMIT settingsChanged();
    }
}

bool Settings::hasPerScreenZoneSelectorArrangementSettings(const QString& screenIdOrName) const
{
    return PerScreenDetail::hasPerScreenKeySubset(m_perScreenZoneSelectorSettings, screenIdOrName,
                                                  isSelectorArrangementKey, /*wantMatch=*/true);
}
void Settings::clearPerScreenZoneSelectorArrangementSettings(const QString& screenIdOrName)
{
    if (PerScreenDetail::clearPerScreenKeySubset(m_perScreenZoneSelectorSettings, screenIdOrName,
                                                 isSelectorArrangementKey, /*clearMatch=*/true)) {
        Q_EMIT perScreenZoneSelectorSettingsChanged();
        Q_EMIT settingsChanged();
    }
}

bool Settings::hasPerScreenZoneSelectorSizeSettings(const QString& screenIdOrName) const
{
    return PerScreenDetail::hasPerScreenKeySubset(m_perScreenZoneSelectorSettings, screenIdOrName, isSelectorSizeKey,
                                                  /*wantMatch=*/true);
}
void Settings::clearPerScreenZoneSelectorSizeSettings(const QString& screenIdOrName)
{
    if (PerScreenDetail::clearPerScreenKeySubset(m_perScreenZoneSelectorSettings, screenIdOrName, isSelectorSizeKey,
                                                 /*clearMatch=*/true)) {
        Q_EMIT perScreenZoneSelectorSettingsChanged();
        Q_EMIT settingsChanged();
    }
}

bool Settings::hasPerScreenScrollingZoneSelectorPositionSettings(const QString& screenIdOrName) const
{
    return PerScreenDetail::hasPerScreenKeySubset(m_perScreenScrollingZoneSelectorSettings, screenIdOrName,
                                                  isSelectorPositionKey, /*wantMatch=*/true);
}
void Settings::clearPerScreenScrollingZoneSelectorPositionSettings(const QString& screenIdOrName)
{
    if (PerScreenDetail::clearPerScreenKeySubset(m_perScreenScrollingZoneSelectorSettings, screenIdOrName,
                                                 isSelectorPositionKey, /*clearMatch=*/true)) {
        Q_EMIT perScreenScrollingZoneSelectorSettingsChanged();
        Q_EMIT settingsChanged();
    }
}

bool Settings::hasPerScreenScrollingZoneSelectorSizeSettings(const QString& screenIdOrName) const
{
    return PerScreenDetail::hasPerScreenKeySubset(m_perScreenScrollingZoneSelectorSettings, screenIdOrName,
                                                  isSelectorSizeKey, /*wantMatch=*/true);
}
void Settings::clearPerScreenScrollingZoneSelectorSizeSettings(const QString& screenIdOrName)
{
    if (PerScreenDetail::clearPerScreenKeySubset(m_perScreenScrollingZoneSelectorSettings, screenIdOrName,
                                                 isSelectorSizeKey, /*clearMatch=*/true)) {
        Q_EMIT perScreenScrollingZoneSelectorSettingsChanged();
        Q_EMIT settingsChanged();
    }
}

} // namespace PlasmaZones
