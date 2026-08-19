// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "stagingservice.h"

#include "settings/controller/settingscontroller_pagekeys.h"
#include "settings/utils/dbusutils.h"
#include "settings/utils/virtualscreenutils.h"
#include "config/settings.h"
#include "core/platform/logging.h"

#include <PhosphorLayoutApi/LayoutId.h>
#include <PhosphorScreens/ScreenIdentity.h>
#include <PhosphorScreens/VirtualScreen.h>
#include <PhosphorZones/AssignmentEntry.h>
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
/// An empty @p screens list ≡ remove the config. Returns false when the daemon
/// answered with an error, or when the caller asked for screens and not one of
/// them survived validation, so the caller can retain the staged config for the
/// next Save.
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
    // An empty array is the daemon's REMOVAL sentinel, which is the right
    // message only when the caller staged an empty list. Reaching empty
    // because every def failed validation means the opposite — the user asked
    // for a split and authored a bad one — and sending it anyway would delete
    // the split they already had. Refuse instead, so the verdict is false, the
    // staged config is retained and the badge stays lit.
    if (!screens.isEmpty() && screensArr.isEmpty()) {
        qCWarning(lcConfig) << "flushVirtualScreensToDaemon: every virtual screen def for" << physicalScreenId
                            << "failed validation, refusing the push rather than sending a removal";
        return false;
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
    m_scrollingQuickSlots.clear();
}

void StagingService::removeStagedAssignment(const QString& screen, int desktop, const QString& activity)
{
    // Erase the map entry entirely (keyed the same way assignmentEntry
    // keys it) so the flush never sees this context at all.
    m_assignments.remove(assignmentCacheKey(screen, desktop, activity));
}

void StagingService::stageAssignmentEntry(const QString& screen, int desktop, const QString& activity, int mode,
                                          const QString& snappingLayoutId, const QString& tilingAlgorithmId)
{
    auto& e = assignmentEntry(screen, desktop, activity);
    e.stagedMode = mode;
    e.snappingLayoutId = snappingLayoutId.isEmpty() ? std::nullopt : std::optional<QString>(snappingLayoutId);
    e.tilingAlgorithmId = tilingAlgorithmId.isEmpty() ? std::nullopt : std::optional<QString>(tilingAlgorithmId);
}

void StagingService::stageScrollingTemplate(const QString& screen, int desktop, const QString& activity,
                                            const QString& templateId)
{
    // Deliberately touches ONLY its own slot, leaving any staged mode and
    // layouts alone. The template is orthogonal to the snapping/tiling pair
    // that stageAssignmentEntry writes together: a scrolling context can carry
    // a preserved snapping layout AND a template, so there is nothing here to
    // clear for consistency's sake.
    assignmentEntry(screen, desktop, activity).scrollingTemplateId = templateId;
}

const StagingService::StagedAssignment* StagingService::stagedAssignmentFor(const QString& screen, int desktop,
                                                                            const QString& activity) const
{
    return assignmentEntryConst(screen, desktop, activity);
}

bool StagingService::flushAssignmentsToDaemon(const std::function<bool(const QString&)>& templateExists,
                                              QStringList* refusedTemplateIds)
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
    // A MUTATING walk: a refused template id is erased from its entry below so
    // the retained map cannot re-refuse it forever. Only entry VALUES are
    // touched, never the key set, so the iterator stays valid throughout.
    for (auto it = m_assignments.begin(); it != m_assignments.end(); ++it) {
        auto& s = it.value();
        qCDebug(lcCore) << "  flush: screen=" << s.screenId << "mode="
                        << (s.stagedMode.has_value() ? QString::number(*s.stagedMode) : QStringLiteral("(none)"))
                        << "snapping="
                        << (s.snappingLayoutId.has_value() ? *s.snappingLayoutId : QStringLiteral("(none)"))
                        << "tiling="
                        << (s.tilingAlgorithmId.has_value() ? *s.tilingAlgorithmId : QStringLiteral("(none)"))
                        // The template is the one slot whose write is
                        // conditional (staged mode must be Scrolling) and the
                        // one that can be refused, so it is the slot most
                        // worth having in the trace.
                        << "template="
                        << (s.scrollingTemplateId.has_value() ? *s.scrollingTemplateId : QStringLiteral("(none)"));

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

            // The template existence pre-check runs BEFORE the entry write, so
            // a refusal leaves the daemon untouched for the slot it refuses
            // rather than half-applying. It is needed at all because the
            // daemon's refusal is invisible on the wire: setScrollingTemplateLayout
            // is a void slot that warns and returns for an id naming no live
            // template, which replies successfully. Only a UUID form is
            // checked — the empty string and the reserved "explicitly none"
            // word are both legal values the daemon always accepts.
            //
            // Gated on the staged mode being Scrolling for the same reason the
            // write below is: setScrollingTemplateLayout stamps Scrolling on
            // the entry it upserts (its own contract), so a template staged
            // against a context the user then switched AWAY from scrolling is
            // never sent, and must not be refused either.
            if (s.scrollingTemplateId.has_value()
                && mode == static_cast<int>(PhosphorZones::AssignmentEntry::Scrolling)) {
                const QString& templateId = *s.scrollingTemplateId;
                const bool needsExistenceCheck =
                    templateExists && !templateId.isEmpty() && templateId != PhosphorZones::NoScrollingTemplate;
                if (needsExistenceCheck && !templateExists(templateId)) {
                    qCWarning(lcCore) << "flushAssignmentsToDaemon: staged scrolling template" << templateId
                                      << "for screen" << s.screenId << "no longer exists, dropping the template pick";
                    if (refusedTemplateIds) {
                        refusedTemplateIds->append(templateId);
                    }
                    // Erased, not retained. The verdict below still goes false
                    // so the badge stays lit and the caller can tell the user,
                    // but a deleted template never comes back: keeping the id
                    // would make every future Save refuse the same entry
                    // forever with no way out but Discard. The mode and layout
                    // slots survive and re-flush normally.
                    s.scrollingTemplateId.reset();
                    ok = false;
                }
            }

            check(DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                                         QStringLiteral("setAssignmentEntry"),
                                         {s.screenId, s.virtualDesktop, s.activityId, mode, snapping, tiling}),
                  "setAssignmentEntry", s.screenId);
            // The template rides its own setter AFTER the entry write, because
            // setAssignmentEntry carries no template argument. Order matters:
            // the entry write stamps the mode and both preserved siblings, and
            // this second call touches only the template slot. A slot the
            // pre-check just erased reads as unstaged here and is skipped.
            if (s.scrollingTemplateId.has_value()
                && mode == static_cast<int>(PhosphorZones::AssignmentEntry::Scrolling)) {
                check(DaemonDBus::callDaemon(QString(PhosphorProtocol::Service::Interface::LayoutRegistry),
                                             QStringLiteral("setScrollingTemplateLayout"),
                                             {s.screenId, s.virtualDesktop, s.activityId, *s.scrollingTemplateId}),
                      "setScrollingTemplateLayout", s.screenId);
            }
            continue;
        }

        // Nothing to do for an entry with no staged mode. Only two writers
        // reach this map: stageAssignmentEntry always sets the mode and so
        // takes the branch above, and stageScrollingTemplate sets only the
        // template slot, which the daemon writes through setAssignmentEntry's
        // companion setter and never on its own. So a template staged against
        // a context whose mode was never staged is deliberately not sent.
        //
        // Per-field snapping / tiling branches used to stand here for the
        // retired stageSnapping / stageTiling / stageTilingClear mutators.
        // They were deleted with those mutators rather than kept as
        // scaffolding for a caller that does not exist.
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

bool StagingService::flushVirtualScreensToSettings(Settings& settings)
{
    bool ok = true;
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
            // The persistence twin of the guard in pushVirtualScreenConfigToDaemon:
            // an empty screen list means "no split" on disk too, so writing one
            // built from a non-empty input whose defs all failed validation
            // would persist the deletion of a split the user still has. Skip
            // the write and report, leaving the previous config on disk.
            if (vsConfig.screens.isEmpty()) {
                qCWarning(lcConfig) << "flushVirtualScreensToSettings: every virtual screen def for" << it.key()
                                    << "failed validation, leaving the saved config untouched";
                ok = false;
                continue;
            }
        }
        settings.setVirtualScreenConfig(it.key(), vsConfig);
    }
    return ok;
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
    // IDs, and scrolling slots hold native template ids. The wire mode comes
    // from the shared QuickSlotMode* aliases of AssignmentEntry::Mode.
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
    // All three modes are attempted before the verdict — `&&` would
    // short-circuit the later flushes on a snapping failure and silently skip
    // them.
    const bool snappingOk = flush(QuickSlotModeSnapping, m_snappingQuickSlots);
    const bool tilingOk = flush(QuickSlotModeTiling, m_tilingQuickSlots);
    const bool scrollingOk = flush(QuickSlotModeScrolling, m_scrollingQuickSlots);
    return snappingOk && tilingOk && scrollingOk;
}

} // namespace PlasmaZones
