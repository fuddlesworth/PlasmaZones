// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../settings.h"
#include "../configdefaults.h"
#include "../../core/constants.h"
#include "../../core/logging.h"
#include "../../core/utils.h"
#include "../../autotile/AutotileConfig.h"

#include <QJsonDocument>
#include <QJsonObject>

namespace PlasmaZones {

// ── load() helpers ───────────────────────────────────────────────────────────

void Settings::loadActivationConfig(PhosphorConfig::IBackend* backend)
{
    Q_UNUSED(backend);
    // Activation (Snapping.Enabled + Snapping.Behavior.{Triggers,
    // ToggleActivation, ZoneSpan.*}) is backed by PhosphorConfig::Store.
}

void Settings::loadDisplayConfig(PhosphorConfig::IBackend* backend)
{
    Q_UNUSED(backend);
    // Display is backed by PhosphorConfig::Store. Disabled-monitor
    // connector-name resolution lives in the getter (see
    // Settings::disabledMonitors in settings.cpp) so stored connector names
    // get resolved to stable screen ids on every read.
}

// loadAppearanceConfig removed: Appearance group (Colors, Labels, Opacity,
// Border) plus Effects.Blur are now backed by PhosphorConfig::Store and
// loaded on-demand via the Settings getters. See settingsschema.cpp for
// the declarative schema.

void Settings::loadZoneGeometryConfig(PhosphorConfig::IBackend* backend)
{
    Q_UNUSED(backend);
    // Zone geometry + Performance are backed by m_store — clamp validators
    // in the schema handle what the old readValidatedInt call chain did.
}

void Settings::loadBehaviorConfig(PhosphorConfig::IBackend* backend)
{
    Q_UNUSED(backend);
    // Behavior (WindowHandling + SnapAssist) and Exclusions are backed by
    // PhosphorConfig::Store.
}

void Settings::loadZoneSelectorConfig(PhosphorConfig::IBackend* backend)
{
    Q_UNUSED(backend);
    // Zone selector is backed by PhosphorConfig::Store.
}

void Settings::loadShortcutConfig(PhosphorConfig::IBackend* backend)
{
    Q_UNUSED(backend);
    // Global shortcuts are backed by PhosphorConfig::Store — getters read
    // through the store on demand.
}

void Settings::loadAutotilingConfig(PhosphorConfig::IBackend* backend)
{
    Q_UNUSED(backend);
    // Autotiling is backed by PhosphorConfig::Store. Note: PerAlgorithmSettings
    // has a sanitization/clamp step (AutotileConfig::perAlgoFromVariantMap)
    // that used to run in the load path; it's now applied inside the
    // autotilePerAlgorithmSettings getter below so on-disk garbage gets
    // coerced on every read.
}

void Settings::loadEditorConfig(PhosphorConfig::IBackend* backend)
{
    Q_UNUSED(backend);
    // Editor settings (Shortcuts + Snapping + FillOnDrop) are backed by
    // PhosphorConfig::Store. Post-load NOTIFY fan-out is handled by the
    // generic Q_PROPERTY re-emit loop in load() — no group-specific logic
    // needed here.
}

// ── save() helpers ───────────────────────────────────────────────────────────

void Settings::saveEditorConfig(PhosphorConfig::IBackend* backend)
{
    Q_UNUSED(backend);
    // Editor settings are backed by PhosphorConfig::Store.
}

void Settings::saveActivationConfig(PhosphorConfig::IBackend* backend)
{
    Q_UNUSED(backend);
    // Activation is backed by PhosphorConfig::Store.
}

void Settings::saveDisplayConfig(PhosphorConfig::IBackend* backend)
{
    Q_UNUSED(backend);
    // Display is backed by PhosphorConfig::Store.
}

// saveAppearanceConfig removed: Appearance setters persist writes through
// PhosphorConfig::Store immediately; the top-level Settings::save() still
// calls m_configBackend->sync() to flush alongside other groups.

void Settings::saveZoneGeometryConfig(PhosphorConfig::IBackend* backend)
{
    Q_UNUSED(backend);
    // Zone geometry + Performance are backed by m_store — setters persist
    // immediately; save() flushes via m_configBackend->sync().
}

void Settings::saveBehaviorConfig(PhosphorConfig::IBackend* backend)
{
    Q_UNUSED(backend);
    // Behavior (WindowHandling + SnapAssist) is backed by PhosphorConfig::Store.
}

void Settings::saveZoneSelectorConfig(PhosphorConfig::IBackend* backend)
{
    Q_UNUSED(backend);
    // Zone selector is backed by PhosphorConfig::Store.
}

void Settings::saveShortcutConfig(PhosphorConfig::IBackend* backend)
{
    Q_UNUSED(backend);
    // Global shortcuts are backed by PhosphorConfig::Store.
}

void Settings::saveAutotilingConfig(PhosphorConfig::IBackend* backend)
{
    Q_UNUSED(backend);
    // Autotiling (all sub-groups) is backed by PhosphorConfig::Store.
}

// ── Virtual screen config load/save ──────────────────────────────────────────

void Settings::loadVirtualScreenConfigs(PhosphorConfig::IBackend* backend)
{
    m_virtualScreenConfigs.clear();
    const QStringList allGroups = backend->groupList();
    const QString prefix = ConfigDefaults::virtualScreenGroupPrefix();

    for (const QString& groupName : allGroups) {
        if (!groupName.startsWith(prefix))
            continue;

        const QString physId = groupName.mid(prefix.size());
        if (physId.isEmpty())
            continue;

        auto group = backend->group(groupName);
        int count = group->readInt(ConfigDefaults::virtualScreenCountKey(), 0);
        count = qBound(0, count, ConfigDefaults::maxVirtualScreensPerPhysical());
        if (count <= 0) {
            qCWarning(lcConfig) << "VirtualScreen config for" << physId << "has invalid count:" << count;
            continue;
        }

        VirtualScreenConfig config;
        config.physicalScreenId = physId;

        for (int i = 0; i < count; ++i) {
            const QString p = QString::number(i) + QLatin1Char('/');
            VirtualScreenDef vs;
            vs.physicalScreenId = physId;
            vs.index = i;
            vs.id = VirtualScreenId::make(physId, i);
            vs.displayName = group->readString(p + ConfigDefaults::virtualScreenNameKey(),
                                               ConfigDefaults::defaultVirtualScreenName(i));
            const QRectF defaultRegion = ConfigDefaults::defaultVirtualScreenRegion();
            qreal x = group->readDouble(p + ConfigDefaults::virtualScreenXKey(), defaultRegion.x());
            qreal y = group->readDouble(p + ConfigDefaults::virtualScreenYKey(), defaultRegion.y());
            qreal w = group->readDouble(p + ConfigDefaults::virtualScreenWidthKey(), defaultRegion.width());
            qreal h = group->readDouble(p + ConfigDefaults::virtualScreenHeightKey(), defaultRegion.height());
            vs.region = QRectF(x, y, w, h);
            config.screens.append(vs);
        }

        // Validate loaded regions — skip invalid entries instead of discarding entire config
        QVector<VirtualScreenDef> validScreens;
        for (const auto& vs : config.screens) {
            if (!vs.isValid()) {
                qCWarning(lcConfig) << "Skipping VirtualScreen" << vs.id << "with invalid region:" << vs.region;
                continue;
            }
            validScreens.append(vs);
        }
        // Renumber surviving entries with contiguous indices (0..N-1) so that
        // save round-trips don't cause ID drift when interior entries were invalid.
        for (int i = 0; i < validScreens.size(); ++i) {
            validScreens[i].index = i;
            validScreens[i].id = VirtualScreenId::make(physId, i);
        }
        config.screens = validScreens;

        // Need at least minVirtualScreensPerPhysical() screens for a meaningful subdivision
        if (config.screens.size() < ConfigDefaults::minVirtualScreensPerPhysical())
            continue;

        // Validate no overlapping regions (pairwise intersection, tolerance-aware)
        {
            bool hasOverlap = false;
            for (int i = 0; i < config.screens.size(); ++i) {
                for (int j = i + 1; j < config.screens.size(); ++j) {
                    QRectF intersection = config.screens[i].region.intersected(config.screens[j].region);
                    if (intersection.width() > VirtualScreenDef::Tolerance
                        && intersection.height() > VirtualScreenDef::Tolerance) {
                        qCWarning(lcConfig)
                            << "loadVirtualScreenConfigs: overlapping regions between" << config.screens[i].id << "and"
                            << config.screens[j].id << "for" << physId << "- skipping config";
                        hasOverlap = true;
                        break;
                    }
                }
                if (hasOverlap)
                    break;
            }
            if (hasOverlap)
                continue;
        }

        // Validate total area coverage is approximately 1.0
        {
            qreal totalArea = 0.0;
            for (const auto& vs : config.screens) {
                totalArea += vs.region.width() * vs.region.height();
            }
            constexpr qreal tol = ConfigDefaults::areaCoverageTolerance();
            if (totalArea < 1.0 - tol || totalArea > 1.0 + tol) {
                qCWarning(lcConfig) << "loadVirtualScreenConfigs: total area" << totalArea << "outside tolerance for"
                                    << physId << "- skipping config";
                continue;
            }
        }

        m_virtualScreenConfigs.insert(physId, config);
    }
}

void Settings::saveVirtualScreenConfigs(PhosphorConfig::IBackend* backend)
{
    // Remove old VirtualScreen: groups that are no longer in the config
    const QStringList allGroups = backend->groupList();
    const QString prefix = ConfigDefaults::virtualScreenGroupPrefix();
    for (const QString& groupName : allGroups) {
        if (groupName.startsWith(prefix)) {
            backend->deleteGroup(groupName);
        }
    }

    // Write current configs — normalize indices to be contiguous (0..N-1) so that
    // the load path (which reconstructs index and id from the loop counter) produces
    // identical IDs to what was saved.
    for (auto it = m_virtualScreenConfigs.constBegin(); it != m_virtualScreenConfigs.constEnd(); ++it) {
        const QString& physId = it.key();
        const VirtualScreenConfig& config = it.value();
        if (config.screens.isEmpty())
            continue;

        auto group = backend->group(prefix + physId);
        group->writeInt(ConfigDefaults::virtualScreenCountKey(), config.screens.size());

        for (int i = 0; i < config.screens.size(); ++i) {
            VirtualScreenDef vs = config.screens[i];
            // Normalize index and id to match the save position so round-trip is stable
            vs.index = i;
            vs.id = VirtualScreenId::make(physId, i);
            const QString p = QString::number(i) + QLatin1Char('/');
            group->writeString(p + ConfigDefaults::virtualScreenNameKey(), vs.displayName);
            group->writeDouble(p + ConfigDefaults::virtualScreenXKey(), vs.region.x());
            group->writeDouble(p + ConfigDefaults::virtualScreenYKey(), vs.region.y());
            group->writeDouble(p + ConfigDefaults::virtualScreenWidthKey(), vs.region.width());
            group->writeDouble(p + ConfigDefaults::virtualScreenHeightKey(), vs.region.height());
        }
    }
}

} // namespace PlasmaZones
