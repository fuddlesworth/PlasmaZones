// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "stagingservice.h"

#include "settings/utils/dbusutils.h"
#include "settings/utils/virtualscreenutils.h"
#include "config/settings.h"
#include "core/platform/logging.h"

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
/// Empty list ≡ remove the config. Returns false when the daemon answered with
/// an error, so the caller can retain the staged config for the next Save.
bool pushVirtualScreenConfigToDaemon(const QString& physicalScreenId, const QVariantList& screens)
{
    QJsonObject root;
    root[QLatin1String("physicalScreenId")] = physicalScreenId;

    QJsonArray screensArr;
    for (int i = 0; i < screens.size(); ++i) {
        PhosphorScreens::VirtualScreenDef def =
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
    const QDBusMessage reply =
        DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::Screen),
                               QStringLiteral("setVirtualScreenConfig"), {physicalScreenId, json});
    if (reply.type() == QDBusMessage::ErrorMessage) {
        qCWarning(lcConfig) << "flushVirtualScreensToDaemon: setVirtualScreenConfig failed for" << physicalScreenId
                            << ":" << reply.errorMessage();
        return false;
    }
    return true;
}

} // namespace

// ─── Assignment staging ──────────────────────────────────────────────

QString StagingService::assignmentCacheKey(const QString& screen, int desktop, const QString& activity)
{
    // Resolve connector names to EDID-based screen IDs so cache keys
    // match regardless of whether the caller passes "DP-3" or the full ID.
    const QString resolved = PhosphorScreens::ScreenIdentity::idForName(screen);
    return resolved + QChar(0x1F) + QString::number(desktop) + QChar(0x1F) + activity;
}

StagingService::StagedAssignment& StagingService::assignmentEntry(const QString& screen, int desktop,
                                                                  const QString& activity)
{
    const QString key = assignmentCacheKey(screen, desktop, activity);
    auto it = m_assignments.find(key);
    if (it == m_assignments.end()) {
        StagedAssignment entry;
        entry.screenId = PhosphorScreens::ScreenIdentity::idForName(screen);
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

// Snapping and tiling slots are mutually exclusive in the unified Rule
// model: a single context (screen × desktop × activity) carries either a
// snapping layout OR a tiling algorithm, never both. Staging one therefore
// clears the other so the flush emits a coherent `setAssignmentEntry` with
// matching engine mode. The earlier per-page "Snapping > Assignments" and
// "Tiling > Assignments" flows treated the two slots as independent fields,
// but those pages were retired by the Rule refactor — callers now
// stage via `stageAssignmentEntry` (Overview page composite write) or via
// the Rule pipeline. The per-field `stageSnapping` / `stageTiling`
// entry points are kept for any future single-slot mutator that wants the
// mutually-exclusive contract.
void StagingService::stageSnapping(const QString& screen, int desktop, const QString& activity, const QString& layoutId)
{
    auto& e = assignmentEntry(screen, desktop, activity);
    e.stagedMode = std::nullopt;
    e.snappingLayoutId = layoutId;
    e.tilingAlgorithmId = std::nullopt;
}

void StagingService::stageTiling(const QString& screen, int desktop, const QString& activity, const QString& layoutId)
{
    auto& e = assignmentEntry(screen, desktop, activity);
    e.stagedMode = std::nullopt;
    e.tilingAlgorithmId = layoutId;
    e.snappingLayoutId = std::nullopt;
}

void StagingService::removeStagedAssignment(const QString& screen, int desktop, const QString& activity)
{
    // Erase the map entry entirely (keyed the same way assignmentEntry
    // keys it) so the flush never sees this context at all.
    m_assignments.remove(assignmentCacheKey(screen, desktop, activity));
}

void StagingService::stageTilingClear(const QString& screen, int desktop, const QString& activity)
{
    auto& e = assignmentEntry(screen, desktop, activity);
    // Clearing tiling reverts to snapping mode — drop any previously staged
    // explicit mode so the flush takes the "tiling clear" branch
    // (setAssignmentEntry with mode=0) rather than sending the stale
    // Overview-page mode back to the daemon with an empty algorithm id.
    e.stagedMode = std::nullopt;
    e.tilingAlgorithmId = QString(); // empty = cleared
}

void StagingService::stageAssignmentEntry(const QString& screen, int desktop, const QString& activity, int mode,
                                          const QString& snappingLayoutId, const QString& tilingAlgorithmId)
{
    auto& e = assignmentEntry(screen, desktop, activity);
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

bool StagingService::flushAssignmentsToDaemon()
{
    qCDebug(lcCore) << "flushStagedAssignments: count=" << m_assignments.size();
    bool ok = true;
    // Every branch below routes its reply through here so a failure on ANY
    // entry is both logged and carried out to the caller's commit gate.
    const auto check = [&ok](const QDBusMessage& reply, const char* what, const QString& screenId) {
        if (reply.type() == QDBusMessage::ErrorMessage) {
            qCWarning(lcCore) << "  " << what << "FAILED for screen" << screenId << ":" << reply.errorMessage();
            ok = false;
        }
    };
    for (auto it = m_assignments.constBegin(); it != m_assignments.constEnd(); ++it) {
        const auto& s = it.value();
        const bool isActivity = !s.activityId.isEmpty();
        const bool isDesktop = s.virtualDesktop > 0;
        qCDebug(lcCore) << "  flush: screen=" << s.screenId << "mode="
                        << (s.stagedMode.has_value() ? QString::number(*s.stagedMode) : QStringLiteral("(none)"))
                        << "snapping="
                        << (s.snappingLayoutId.has_value() ? *s.snappingLayoutId : QStringLiteral("(none)"))
                        << "tiling="
                        << (s.tilingAlgorithmId.has_value() ? *s.tilingAlgorithmId : QStringLiteral("(none)"));

        // Normalise the tiling id — callers may store either the raw algo id
        // or the `autotile:` prefixed form; the D-Bus surface wants the raw id.
        const auto normTile = [](const QString& val) {
            return PhosphorLayout::LayoutId::isAutotile(val) ? PhosphorLayout::LayoutId::extractAlgorithmId(val) : val;
        };

        // Explicit mode staging (Overview page) — setAssignmentEntry targets
        // a full context triple, matching the KCM batch-save path.
        if (s.stagedMode.has_value()) {
            const int mode = *s.stagedMode;
            const QString snapping = s.snappingLayoutId.value_or(QString());
            const QString tiling = s.tilingAlgorithmId.has_value() ? normTile(*s.tilingAlgorithmId) : QString();
            check(DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                                         QStringLiteral("setAssignmentEntry"),
                                         {s.screenId, s.virtualDesktop, s.activityId, mode, snapping, tiling}),
                  "setAssignmentEntry", s.screenId);
            continue;
        }

        const bool hasSnap = s.snappingLayoutId.has_value();
        const bool hasTile = s.tilingAlgorithmId.has_value();
        if (!hasSnap && !hasTile) {
            continue;
        }

        // If BOTH fields are staged (e.g. snap assign followed by tiling clear
        // on the same context), the two per-field D-Bus calls are not atomic:
        // `assignLayoutToScreen(snap)` followed by `setAssignmentEntry(mode=0,
        // "", "")` clobbers the snap we just assigned. Coalesce into a single
        // `setAssignmentEntry` so the daemon writes the combined state in one
        // shot. Mode = 1 when a non-empty tiling algo is staged, 0 otherwise.
        if (hasSnap && hasTile) {
            const QString snap = *s.snappingLayoutId;
            const QString tile = normTile(*s.tilingAlgorithmId);
            const int mode = tile.isEmpty() ? 0 : 1;
            check(DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                                         QStringLiteral("setAssignmentEntry"),
                                         {s.screenId, s.virtualDesktop, s.activityId, mode, snap, tile}),
                  "setAssignmentEntry", s.screenId);
            continue;
        }

        // Only snap staged — use the per-field path. An empty staged value
        // here has no dedicated D-Bus surface (there is no `clearSnappingOnly`
        // method), so it is skipped: the page routes a "Default" pick through
        // stageAssignmentEntry, whose explicit-mode branch above carries the
        // empty slot alongside the mode pin.
        if (hasSnap) {
            const QString& layoutId = *s.snappingLayoutId;
            if (layoutId.isEmpty()) {
                continue;
            }
            QDBusMessage reply;
            if (isActivity) {
                reply = DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                                               QStringLiteral("assignLayoutToScreenActivity"),
                                               {s.screenId, s.activityId, layoutId});
            } else if (isDesktop) {
                reply = DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                                               QStringLiteral("assignLayoutToScreenDesktop"),
                                               {s.screenId, s.virtualDesktop, layoutId});
            } else {
                reply = DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                                               QStringLiteral("assignLayoutToScreen"), {s.screenId, layoutId});
            }
            check(reply, "assignLayout", s.screenId);
            continue;
        }

        // Only tile staged. Empty ≡ tiling-clear (reverts to snapping mode 0).
        const QString tile = normTile(*s.tilingAlgorithmId);
        const int mode = tile.isEmpty() ? 0 : 1;
        check(DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                                     QStringLiteral("setAssignmentEntry"),
                                     {s.screenId, s.virtualDesktop, s.activityId, mode, QString(), tile}),
              "setAssignmentEntry", s.screenId);
    }
    // Retain the whole map on failure. Partial retention would need per-entry
    // bookkeeping for no gain: the daemon setters are idempotent, so re-sending
    // the entries that already landed costs one round trip each and leaves the
    // same state behind.
    if (ok) {
        m_assignments.clear();
    }
    return ok;
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
        PhosphorScreens::VirtualScreenConfig vsConfig;
        vsConfig.physicalScreenId = it.key();
        if (!it.value().isEmpty()) {
            for (int i = 0; i < it.value().size(); ++i) {
                const PhosphorScreens::VirtualScreenDef def =
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

bool StagingService::flushVirtualScreensToDaemon()
{
    bool ok = true;
    for (auto it = m_virtualScreenConfigs.constBegin(); it != m_virtualScreenConfigs.constEnd(); ++it) {
        if (!pushVirtualScreenConfigToDaemon(it.key(), it.value())) {
            ok = false;
        }
    }
    // Retained on failure for the same reason the assignment flush retains:
    // clearing here would leave the retry Save with nothing to send while the
    // daemon still holds the old split.
    if (ok) {
        m_virtualScreenConfigs.clear();
    }
    return ok;
}

void StagingService::clearVirtualScreenConfigs()
{
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

void StagingService::stageScrollingQuickSlot(int slotNumber, const QString& templateId)
{
    m_scrollingQuickSlots[slotNumber] = templateId;
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

bool StagingService::stagedScrollingQuickSlot(int slotNumber, QString& out) const
{
    auto it = m_scrollingQuickSlots.constFind(slotNumber);
    if (it == m_scrollingQuickSlots.constEnd()) {
        return false;
    }
    out = *it;
    return true;
}

void StagingService::clearSnappingQuickSlots()
{
    m_snappingQuickSlots.clear();
}

void StagingService::clearTilingQuickSlots()
{
    m_tilingQuickSlots.clear();
}

void StagingService::clearScrollingQuickSlots()
{
    m_scrollingQuickSlots.clear();
}

bool StagingService::flushQuickSlotsToDaemon()
{
    // Quick slots are mode-keyed in the daemon's LayoutRegistry: snapping
    // slots hold zone-layout UUIDs, tiling slots hold autotile algorithm
    // IDs, and scrolling slots hold native template ids. The wire mode
    // matches AssignmentEntry::Mode (Snapping = 0, Autotile = 1,
    // Scrolling = 2).
    constexpr int kSnappingMode = 0;
    constexpr int kAutotileMode = 1;
    constexpr int kScrollingMode = 2;
    const auto flush = [](int mode, QHash<int, QString>& slots) {
        bool ok = true;
        for (auto it = slots.constBegin(); it != slots.constEnd(); ++it) {
            const QDBusMessage reply =
                DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                                       QStringLiteral("setQuickLayoutSlot"), {mode, it.key(), it.value()});
            if (reply.type() == QDBusMessage::ErrorMessage) {
                qCWarning(lcCore) << "flushQuickSlotsToDaemon: setQuickLayoutSlot failed for mode" << mode << "slot"
                                  << it.key() << ":" << reply.errorMessage();
                ok = false;
            }
        }
        // Per-mode retention: a snapping-side failure must not throw away the
        // tiling slots that did land, and vice versa.
        if (ok) {
            slots.clear();
        }
        return ok;
    };
    // Both modes are attempted before the verdict — `&&` would short-circuit
    // the second flush on a snapping failure and silently skip it.
    const bool snappingOk = flush(kSnappingMode, m_snappingQuickSlots);
    const bool tilingOk = flush(kAutotileMode, m_tilingQuickSlots);
    const bool scrollingOk = flush(kScrollingMode, m_scrollingQuickSlots);
    return snappingOk && tilingOk && scrollingOk;
}

} // namespace PlasmaZones
