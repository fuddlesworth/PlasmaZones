// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "stagingservice.h"

#include "dbusutils.h"
#include "virtualscreenutils.h"
#include "../config/settings.h"
#include "../core/logging.h"

#include <PhosphorLayoutApi/LayoutId.h>
#include <PhosphorScreens/ScreenIdentity.h>
#include <PhosphorScreens/VirtualScreen.h>
#include <PhosphorZones/ZoneJsonKeys.h>

#include <QChar>
#include <QDBusMessage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLatin1String>
#include <QLoggingCategory>
#include <QStringLiteral>

namespace PlasmaZones {

namespace {

/// Emit a D-Bus setVirtualScreenConfig for @p physicalScreenId carrying @p screens.
/// Empty list ≡ remove the config.
void pushVirtualScreenConfigToDaemon(const QString& physicalScreenId, const QVariantList& screens)
{
    QJsonObject root;
    root[QLatin1String("physicalScreenId")] = physicalScreenId;

    QJsonArray screensArr;
    for (int i = 0; i < screens.size(); ++i) {
        Phosphor::Screens::VirtualScreenDef def =
            VirtualScreenUtils::variantMapToVirtualScreenDef(screens[i].toMap(), physicalScreenId, i);
        if (!def.isValid()) {
            qCWarning(lcConfig) << "Skipping invalid virtual screen def for" << physicalScreenId << "index" << i
                                << "region:" << def.region;
            continue;
        }
        QJsonObject screenObj;
        screenObj[QLatin1String("index")] = def.index;
        screenObj[QLatin1String("displayName")] = def.displayName;
        screenObj[QLatin1String("region")] = QJsonObject{{::PhosphorZones::ZoneJsonKeys::X, def.region.x()},
                                                         {::PhosphorZones::ZoneJsonKeys::Y, def.region.y()},
                                                         {::PhosphorZones::ZoneJsonKeys::Width, def.region.width()},
                                                         {::PhosphorZones::ZoneJsonKeys::Height, def.region.height()}};
        screensArr.append(screenObj);
    }
    root[QLatin1String("screens")] = screensArr;

    const QString json = QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
    DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::Screen),
                           QStringLiteral("setVirtualScreenConfig"), {physicalScreenId, json});
}

} // namespace

// ─── Assignment staging ──────────────────────────────────────────────

QString StagingService::assignmentCacheKey(const QString& screen, int desktop, const QString& activity)
{
    // Resolve connector names to EDID-based screen IDs so cache keys
    // match regardless of whether the caller passes "DP-3" or the full ID.
    const QString resolved = Phosphor::Screens::ScreenIdentity::idForName(screen);
    return resolved + QChar(0x1F) + QString::number(desktop) + QChar(0x1F) + activity;
}

StagingService::StagedAssignment& StagingService::assignmentEntry(const QString& screen, int desktop,
                                                                  const QString& activity)
{
    const QString key = assignmentCacheKey(screen, desktop, activity);
    auto it = m_assignments.find(key);
    if (it == m_assignments.end()) {
        StagedAssignment entry;
        entry.screenId = Phosphor::Screens::ScreenIdentity::idForName(screen);
        entry.virtualDesktop = desktop;
        entry.activityId = activity;
        it = m_assignments.insert(key, entry);
    }
    return *it;
}

const StagingService::StagedAssignment* StagingService::assignmentEntryConst(const QString& screen, int desktop,
                                                                             const QString& activity) const
{
    const QString key = assignmentCacheKey(screen, desktop, activity);
    auto it = m_assignments.constFind(key);
    return it != m_assignments.constEnd() ? &(*it) : nullptr;
}

void StagingService::clearAll()
{
    m_assignments.clear();
    m_virtualScreenConfigs.clear();
    m_snappingQuickSlots.clear();
    m_tilingQuickSlots.clear();
}

void StagingService::stageSnapping(const QString& screen, int desktop, const QString& activity, const QString& layoutId)
{
    auto& e = assignmentEntry(screen, desktop, activity);
    e.fullCleared = false;
    e.stagedMode = std::nullopt;
    e.snappingLayoutId = layoutId;
    // Snap and tile are independent fields — editing the snap slot from
    // the "Snapping > Assignments" page must NOT clobber a previously
    // staged tile update for the same context, and (more importantly)
    // must NOT flip the rendered mode at that context. The flush emits
    // a `setSnappingLayoutEntry` D-Bus call that updates only the snap
    // field on the daemon side; the tile field and the rendered mode
    // are preserved there. (Per-context mode flips remain the explicit
    // privilege of the Overview page via `stageAssignmentEntry`.)
}

void StagingService::stageTiling(const QString& screen, int desktop, const QString& activity, const QString& layoutId)
{
    auto& e = assignmentEntry(screen, desktop, activity);
    e.fullCleared = false;
    e.stagedMode = std::nullopt;
    e.tilingAlgorithmId = layoutId;
    // See `stageSnapping`: snap and tile are independent. The flush
    // emits `setTilingAlgorithmEntry`, which updates only the tile
    // field and preserves snap + mode on the daemon side.
}

void StagingService::stageFullClear(const QString& screen, int desktop, const QString& activity)
{
    auto& e = assignmentEntry(screen, desktop, activity);
    e.fullCleared = true;
    e.stagedMode = std::nullopt;
    e.snappingLayoutId = std::nullopt;
    e.tilingAlgorithmId = std::nullopt;
}

void StagingService::stageTilingClear(const QString& screen, int desktop, const QString& activity)
{
    auto& e = assignmentEntry(screen, desktop, activity);
    // Clearing the tile slot from the "Tiling > Assignments" page now
    // preserves both the snap field and the rendered mode: the flush
    // sends `setTilingAlgorithmEntry(..., "")` so only the tile field
    // is wiped on the daemon side. Drop any previously staged explicit
    // mode (from a coalesced Overview edit on the same context) so the
    // flush takes the partial-update path rather than re-sending the
    // Overview's mode through `setAssignmentEntry`.
    e.stagedMode = std::nullopt;
    e.tilingAlgorithmId = QString(); // empty = cleared
}

void StagingService::stageAssignmentEntry(const QString& screen, int desktop, const QString& activity, int mode,
                                          const QString& snappingLayoutId, const QString& tilingAlgorithmId)
{
    auto& e = assignmentEntry(screen, desktop, activity);
    e.fullCleared = false;
    e.stagedMode = mode;
    e.snappingLayoutId = snappingLayoutId.isEmpty() ? std::nullopt : std::optional<QString>(snappingLayoutId);
    e.tilingAlgorithmId = tilingAlgorithmId.isEmpty() ? std::nullopt : std::optional<QString>(tilingAlgorithmId);
}

bool StagingService::stagedSnappingLayout(const QString& screen, int desktop, const QString& activity,
                                          QString& out) const
{
    const auto* s = assignmentEntryConst(screen, desktop, activity);
    if (!s) {
        return false;
    }
    if (s->fullCleared && !s->snappingLayoutId.has_value()) {
        out = QString();
        return true;
    }
    if (s->snappingLayoutId.has_value()) {
        out = *s->snappingLayoutId;
        return true;
    }
    return false;
}

bool StagingService::stagedTilingLayout(const QString& screen, int desktop, const QString& activity, QString& out) const
{
    const auto* s = assignmentEntryConst(screen, desktop, activity);
    if (!s) {
        return false;
    }
    if (s->fullCleared && !s->tilingAlgorithmId.has_value()) {
        out = QString();
        return true;
    }
    if (s->tilingAlgorithmId.has_value()) {
        const QString& val = *s->tilingAlgorithmId;
        if (val.isEmpty()) {
            out = QString();
        } else {
            out = PhosphorLayout::LayoutId::isAutotile(val) ? val : PhosphorLayout::LayoutId::makeAutotileId(val);
        }
        return true;
    }
    return false;
}

const StagingService::StagedAssignment* StagingService::stagedAssignmentFor(const QString& screen, int desktop,
                                                                            const QString& activity) const
{
    return assignmentEntryConst(screen, desktop, activity);
}

void StagingService::flushAssignmentsToDaemon()
{
    qCDebug(lcCore) << "flushStagedAssignments: count=" << m_assignments.size();
    for (auto it = m_assignments.constBegin(); it != m_assignments.constEnd(); ++it) {
        const auto& s = it.value();
        const bool isActivity = !s.activityId.isEmpty();
        const bool isDesktop = s.virtualDesktop > 0;
        qCDebug(lcCore) << "  flush: screen=" << s.screenId << "fullCleared=" << s.fullCleared << "mode="
                        << (s.stagedMode.has_value() ? QString::number(*s.stagedMode) : QStringLiteral("(none)"))
                        << "snapping="
                        << (s.snappingLayoutId.has_value() ? *s.snappingLayoutId : QStringLiteral("(none)"))
                        << "tiling="
                        << (s.tilingAlgorithmId.has_value() ? *s.tilingAlgorithmId : QStringLiteral("(none)"));

        // Full clear — clear the entire entry for this context.
        if (s.fullCleared) {
            if (isActivity) {
                DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                                       QStringLiteral("clearAssignmentForScreenActivity"), {s.screenId, s.activityId});
            } else if (isDesktop) {
                DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                                       QStringLiteral("clearAssignmentForScreenDesktop"),
                                       {s.screenId, s.virtualDesktop});
            } else {
                DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                                       QStringLiteral("clearAssignment"), {s.screenId});
            }
            continue;
        }

        // Normalise the tiling id — callers may store either the raw algo id
        // or the `autotile:` prefixed form; the D-Bus surface wants the raw id.
        const auto normTile = [](const QString& val) {
            return PhosphorLayout::LayoutId::isAutotile(val) ? PhosphorLayout::LayoutId::extractAlgorithmId(val) : val;
        };

        // Explicit mode staging (Overview page) — setAssignmentEntry targets
        // a full context triple, matching the KCM batch-save path. This is
        // the ONLY path that may change the rendered mode at the context;
        // the per-field paths below preserve mode by design.
        if (s.stagedMode.has_value()) {
            const int mode = *s.stagedMode;
            const QString snapping = s.snappingLayoutId.value_or(QString());
            const QString tiling = s.tilingAlgorithmId.has_value() ? normTile(*s.tilingAlgorithmId) : QString();
            DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                                   QStringLiteral("setAssignmentEntry"),
                                   {s.screenId, s.virtualDesktop, s.activityId, mode, snapping, tiling});
            continue;
        }

        const bool hasSnap = s.snappingLayoutId.has_value();
        const bool hasTile = s.tilingAlgorithmId.has_value();
        if (!hasSnap && !hasTile) {
            continue;
        }

        // Per-field updates from the "Snapping > Assignments" / "Tiling >
        // Assignments" pages — record the user's slot-specific preference
        // without flipping the rendered mode at that context. Each field
        // routes through its own dedicated daemon method
        // (`setSnappingLayoutEntry` / `setTilingAlgorithmEntry`) so an
        // empty value means "clear that one field" and a non-empty value
        // means "store that one field"; the opposite field and the
        // mode are preserved on the daemon side. Snap and tile are
        // independent — when both are staged on the same context (a
        // session that crossed both pages) we emit both calls in
        // sequence.
        if (hasSnap) {
            const QString layoutId = *s.snappingLayoutId;
            DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                                   QStringLiteral("setSnappingLayoutEntry"),
                                   {s.screenId, s.virtualDesktop, s.activityId, layoutId});
        }
        if (hasTile) {
            const QString algo = normTile(*s.tilingAlgorithmId);
            DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                                   QStringLiteral("setTilingAlgorithmEntry"),
                                   {s.screenId, s.virtualDesktop, s.activityId, algo});
        }
    }
    m_assignments.clear();
}

// ─── Virtual screen staging ──────────────────────────────────────────

void StagingService::stageVirtualScreenConfig(const QString& physicalScreenId, const QVariantList& screens)
{
    m_virtualScreenConfigs.insert(physicalScreenId, screens);
}

void StagingService::stageVirtualScreenRemoval(const QString& physicalScreenId)
{
    m_virtualScreenConfigs.insert(physicalScreenId, QVariantList()); // empty = remove
}

bool StagingService::hasUnsavedVirtualScreenConfig(const QString& physicalScreenId) const
{
    return m_virtualScreenConfigs.contains(physicalScreenId);
}

QVariantList StagingService::stagedVirtualScreenConfig(const QString& physicalScreenId) const
{
    return m_virtualScreenConfigs.value(physicalScreenId);
}

void StagingService::flushVirtualScreensToSettings(Settings& settings)
{
    for (auto it = m_virtualScreenConfigs.constBegin(); it != m_virtualScreenConfigs.constEnd(); ++it) {
        Phosphor::Screens::VirtualScreenConfig vsConfig;
        vsConfig.physicalScreenId = it.key();
        if (!it.value().isEmpty()) {
            for (int i = 0; i < it.value().size(); ++i) {
                const Phosphor::Screens::VirtualScreenDef def =
                    VirtualScreenUtils::variantMapToVirtualScreenDef(it.value()[i].toMap(), it.key(), i);
                if (!def.isValid()) {
                    qCWarning(lcConfig) << "Skipping invalid virtual screen def for" << it.key() << "index" << i
                                        << "region:" << def.region;
                    continue;
                }
                vsConfig.screens.append(def);
            }
        }
        settings.setVirtualScreenConfig(it.key(), vsConfig);
    }
}

void StagingService::flushVirtualScreensToDaemon()
{
    for (auto it = m_virtualScreenConfigs.constBegin(); it != m_virtualScreenConfigs.constEnd(); ++it) {
        pushVirtualScreenConfigToDaemon(it.key(), it.value());
    }
    m_virtualScreenConfigs.clear();
}

// ─── Quick layout slots ──────────────────────────────────────────────

void StagingService::stageSnappingQuickSlot(int slotNumber, const QString& layoutId)
{
    m_snappingQuickSlots[slotNumber] = layoutId;
}

void StagingService::stageTilingQuickSlot(int slotNumber, const QString& layoutId)
{
    m_tilingQuickSlots[slotNumber] = layoutId;
}

bool StagingService::stagedSnappingQuickSlot(int slotNumber, QString& out) const
{
    auto it = m_snappingQuickSlots.constFind(slotNumber);
    if (it == m_snappingQuickSlots.constEnd()) {
        return false;
    }
    out = *it;
    return true;
}

bool StagingService::stagedTilingQuickSlot(int slotNumber, QString& out) const
{
    auto it = m_tilingQuickSlots.constFind(slotNumber);
    if (it == m_tilingQuickSlots.constEnd()) {
        return false;
    }
    out = *it;
    return true;
}

void StagingService::flushTilingQuickSlotsToSettings(Settings& settings)
{
    for (auto it = m_tilingQuickSlots.constBegin(); it != m_tilingQuickSlots.constEnd(); ++it) {
        settings.writeTilingQuickLayoutSlot(it.key(), it.value());
    }
    m_tilingQuickSlots.clear();
}

void StagingService::flushSnappingQuickSlotsToDaemon()
{
    for (auto it = m_snappingQuickSlots.constBegin(); it != m_snappingQuickSlots.constEnd(); ++it) {
        DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                               QStringLiteral("setQuickLayoutSlot"), {it.key(), it.value()});
    }
    m_snappingQuickSlots.clear();
}

} // namespace PlasmaZones
