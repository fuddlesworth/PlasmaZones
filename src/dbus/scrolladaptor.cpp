// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "scrolladaptor.h"

#include "core/logging.h"

#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorScrollEngine/ScrollEngine.h>

#include <QSet>

namespace PlasmaZones {

ScrollAdaptor::ScrollAdaptor(PhosphorScrollEngine::ScrollEngine* engine, QObject* parent)
    : QDBusAbstractAdaptor(parent)
    , m_engine(engine)
{
    // A null engine at construction is a programming bug, not a runtime
    // condition. Daemon::start always passes a non-null engine; the late-stage
    // shutdown null-out path is clearEngine(), not the constructor. Match
    // SettingsAdaptor's contract.
    Q_ASSERT(engine);
}

QStringList ScrollAdaptor::scrollScreens() const
{
    if (!m_engine) {
        return {};
    }
    const QSet<QString> screens = m_engine->activeScreens();
    // Sorted for a deterministic property value — QSet iteration order is
    // unspecified.
    QStringList ids(screens.cbegin(), screens.cend());
    ids.sort();
    return ids;
}

bool ScrollAdaptor::enabled() const
{
    if (!m_engine) {
        return false;
    }
    return m_engine->isEnabled();
}

void ScrollAdaptor::clearEngine()
{
    m_engine = nullptr;
}

bool ScrollAdaptor::ensureEngine(const char* methodName) const
{
    if (!m_engine) {
        qCWarning(lcDaemon) << "ScrollAdaptor::" << methodName << "called with no engine";
        return false;
    }
    return true;
}

void ScrollAdaptor::windowOpened(const QString& windowId, const QString& screenId)
{
    if (!ensureEngine("windowOpened")) {
        return;
    }
    m_engine->windowOpened(windowId, screenId);
}

void ScrollAdaptor::windowsOpenedBatch(const PhosphorProtocol::WindowOpenedList& entries)
{
    if (!ensureEngine("windowsOpenedBatch")) {
        return;
    }
    // Cap is shared with the autotile / snap windowsOpenedBatch surfaces;
    // see PhosphorProtocol::Service::MaxWindowsOpenedBatchEntries for the
    // rationale (session-bus DOS protection).
    if (entries.size() > PhosphorProtocol::Service::MaxWindowsOpenedBatchEntries) {
        qCWarning(lcDaemon) << "ScrollAdaptor::windowsOpenedBatch rejected: entry count" << entries.size()
                            << "exceeds cap" << PhosphorProtocol::Service::MaxWindowsOpenedBatchEntries;
        return;
    }
    // WindowOpenedList is shared with org.plasmazones.Autotile and carries
    // per-entry minWidth/minHeight; scroll's strip model is size-agnostic and
    // ignores them (non-resizable windows are fitted to the tile slot
    // effect-side), so only windowId/screenId are forwarded. Empty ids are
    // skipped at this boundary so a malformed entry can't corrupt the
    // reconcile set (an empty-string id would never match any live window
    // and would silently prune a real one).
    //
    // Active-screen validation: a misbehaving effect (or a stale wire payload
    // racing a mode-switch) could otherwise forward windows on screens that
    // are NOT in scroll mode, which would create dormant ScrollScreenStates
    // for phantom screens that nothing ever prunes. Reject per-entry against
    // the engine's live `activeScreens()` set rather than failing the whole
    // batch — a partial payload still reconciles the screens that ARE valid.
    const QSet<QString> activeScreens = m_engine->activeScreens();
    QSet<QString> liveWindowIds;
    liveWindowIds.reserve(entries.size());
    for (const PhosphorProtocol::WindowOpenedEntry& entry : entries) {
        if (entry.windowId.isEmpty() || entry.screenId.isEmpty()) {
            continue;
        }
        if (!activeScreens.contains(entry.screenId)) {
            qCWarning(lcDaemon) << "ScrollAdaptor::windowsOpenedBatch dropping entry for inactive screen"
                                << entry.screenId << "windowId=" << entry.windowId;
            continue;
        }
        // Dedupe before forwarding: a malformed wire payload with the same id
        // listed twice would otherwise drive two windowOpened calls for the
        // same window. The engine is idempotent in the same-screen case, but
        // a same-id-different-screen duplicate would migrate the window mid-
        // batch and yield the second screen as the "live" location. Reject
        // the second occurrence — first wins.
        //
        // Keying the dedup set on windowId alone (rather than a
        // {windowId, screenId} pair) is sufficient because the compositor
        // guarantees windowIds are globally unique across screens — a single
        // window cannot legitimately appear under two screen ids in the same
        // batch. A duplicate windowId on a different screen is therefore
        // always a malformed payload, never a legitimate two-screen window;
        // first-wins is the documented rejection contract for both shapes.
        if (liveWindowIds.contains(entry.windowId)) {
            continue;
        }
        m_engine->windowOpened(entry.windowId, entry.screenId);
        liveWindowIds.insert(entry.windowId);
    }
    // The effect's first batch after a daemon (re)connect is the complete live
    // scroll-window set across every scroll screen — reconcile a just-restored
    // strip against it so a window closed while the daemon was down leaves no
    // phantom column. A no-op on every later (routine) batch.
    m_engine->reconcileRestoredWindows(liveWindowIds);
    // Notify the daemon so it can invalidate its per-screen geometry cache and
    // force a re-push of resolved geometry to the effect. The effect's
    // m_notifiedWindows is now populated, so the re-push's recordAppliedGeometry
    // will seed drift detection. See setBatchProcessedCallback for the full
    // race-condition rationale.
    if (m_batchProcessedCallback) {
        m_batchProcessedCallback();
    }
}

void ScrollAdaptor::windowClosed(const QString& windowId)
{
    if (!ensureEngine("windowClosed")) {
        return;
    }
    m_engine->windowClosed(windowId);
}

void ScrollAdaptor::notifyWindowFocused(const QString& windowId, const QString& screenId)
{
    if (!ensureEngine("notifyWindowFocused")) {
        return;
    }
    m_engine->windowFocused(windowId, screenId);
}

void ScrollAdaptor::windowMinimizedChanged(const QString& windowId, bool minimized)
{
    if (!ensureEngine("windowMinimizedChanged")) {
        return;
    }
    m_engine->windowMinimizedChanged(windowId, minimized);
}

void ScrollAdaptor::windowDropped(const QString& draggedWindowId, const QString& anchorWindowId, bool placeAfter)
{
    if (!ensureEngine("windowDropped")) {
        return;
    }
    m_engine->windowDropped(draggedWindowId, anchorWindowId, placeAfter);
}

} // namespace PlasmaZones
