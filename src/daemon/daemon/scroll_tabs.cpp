// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// The scroll tab-strip enrichment trio, split out of init_engines.cpp (which
// sits at the file-size ceiling): the raw-payload cache + parse/enrich apply,
// the coalescing refresh scheduler, and the all-screens re-enrichment pass. The
// wiring that drives these lives with the rest of the engine wiring in
// init_engines.cpp.

#include "daemon/daemon.h"
#include "core/platform/logging.h"
#include "stripzones.h"

#include "daemon/overlayservice.h"
#include "dbus/windowtrackingadaptor/windowtrackingadaptor.h"

#include <PhosphorEngine/WindowRegistry.h>
#include <PhosphorIdentity/WindowId.h>

#include <QJsonParseError>
#include <QTimer>

namespace PlasmaZones {

void Daemon::applyScrollTabStrips(const QString& screenId, const QString& stripsJson)
{
    if (!m_overlayService) {
        return;
    }
    QJsonParseError parseError;
    const auto strips = StripZones::parseTabStripPayload(
        stripsJson,
        [this](const QString& windowId) -> QString {
            if (!m_windowRegistry) {
                return QString();
            }
            const auto meta = m_windowRegistry->metadata(PhosphorIdentity::WindowId::extractInstanceId(windowId));
            return meta ? meta->title : QString();
        },
        [this](const QString& windowId) -> bool {
            if (!m_windowRegistry) {
                return false;
            }
            const auto meta = m_windowRegistry->metadata(PhosphorIdentity::WindowId::extractInstanceId(windowId));
            // value_or(false): a disengaged optional means the compositor never
            // reported urgency for this window, which must read as "not urgent"
            // rather than lighting the tab up on an unknown.
            return meta ? meta->isDemandingAttention.value_or(false) : false;
        },
        [this](const QString& windowId) -> QVariantMap {
            // niri's top resolution tier: a window rule recolours that window's
            // own tab, outranking the per-context colours and the config.
            if (!m_windowTrackingAdaptor) {
                return {};
            }
            return m_windowTrackingAdaptor->tabColorRuleParams(windowId);
        },
        &parseError);
    // A parse failure means we know nothing about the strips, which is not the
    // same as "there are none": clearing on it would wipe the live tab
    // indicators and leave the columns looking untabbed until the next
    // relayout. Warn and leave the overlay untouched.
    if (!strips) {
        qCWarning(lcDaemon) << "Tab strips JSON unparseable, keeping previous indicators screen=" << screenId
                            << "error=" << parseError.errorString() << "offset=" << parseError.offset;
        return;
    }
    // Retain the RAW payload. Enrichment (titles, urgency, per-window colours)
    // is resolved from live state that the engine knows nothing about, so it
    // can go stale while the structural payload is unchanged — and the engine's
    // emit is change-gated on exactly that payload, so it will not re-fire.
    // Keeping the JSON is what lets refreshScrollTabEnrichment re-run the
    // enrichment without inventing a second producer.
    //
    // Keyed on the PARSED result, not on stripsJson.isEmpty(): the engine's
    // clear paths emit the literal "[]", never an empty string, so testing the
    // raw payload would never prune and every screen that ever carried a strip
    // would keep a dead entry forever.
    if (strips->isEmpty()) {
        m_lastScrollTabStripsJson.remove(screenId);
    } else {
        m_lastScrollTabStripsJson.insert(screenId, stripsJson);
    }
    m_overlayService->updateScrollTabStrips(screenId, *strips);
}

void Daemon::scheduleScrollTabEnrichmentRefresh()
{
    // Coalesce. Retitling is a chatty signal (lifecycle.cpp carries a dedicated
    // caption-only path for exactly that reason), and each refresh re-parses
    // and re-resolves every cached screen, so a burst of title ticks must
    // collapse into one pass rather than N.
    if (m_scrollTabEnrichmentPending) {
        return;
    }
    m_scrollTabEnrichmentPending = true;
    QTimer::singleShot(0, this, [this]() {
        m_scrollTabEnrichmentPending = false;
        refreshScrollTabEnrichment();
    });
}

void Daemon::refreshScrollTabEnrichment()
{
    // Re-enrich every screen holding a cached payload. Deliberately NOT
    // filtered to the screens containing the changed window: the payload's ids
    // would have to be re-parsed to test membership, which is the same work as
    // re-enriching. Strips exist only on scrolling screens that have a tabbed
    // column, so the set is small.
    //
    // Defensive snapshot. applyScrollTabStrips writes the very map being
    // iterated, though only its INSERT branch is reachable from here (a
    // refresh only ever replays non-empty cached payloads, and re-parsing the
    // same JSON is deterministic), so the remove that would actually
    // invalidate an iterator cannot fire today. Cheap to keep: QHash is
    // implicitly shared, and when the member's insert detaches it from this
    // copy, the copy's iteration stays valid — which is the point.
    const QHash<QString, QString> cached = m_lastScrollTabStripsJson;
    for (auto it = cached.constBegin(); it != cached.constEnd(); ++it) {
        applyScrollTabStrips(it.key(), it.value());
    }
}

} // namespace PlasmaZones
