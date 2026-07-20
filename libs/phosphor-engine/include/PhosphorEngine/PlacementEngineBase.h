// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorEngine/EngineTypes.h>
#include <PhosphorEngine/IPlacementEngine.h>
#include <phosphorengine_export.h>

#include <QObject>
#include <QPointer>
#include <QRect>
#include <QSet>
#include <QString>
#include <QStringList>

namespace PhosphorEngine {

/// Abstract base class for placement engines.
///
/// Handles the universal mechanics every engine shares: settings injection and
/// stale-window pruning. Float-back / free geometry is NOT stored here — it lives
/// in the single unified WindowPlacementStore (one record per window, shared
/// freeGeometryByScreen), reached through IWindowTrackingService. The previous
/// per-engine m_unmanagedGeometries store was removed: two parallel float-back
/// stores drifted and leaked the zone/tile rect into float restores.
///
/// Engines subclass this and implement the placement-specific hooks.
class PHOSPHORENGINE_EXPORT PlacementEngineBase : public QObject, public IPlacementEngine
{
    Q_OBJECT

public:
    /// Drop any per-engine bookkeeping for windows not in @p aliveWindowIds.
    /// The base keeps no per-window state of its own now, so it returns 0;
    /// engines override and add their own pruning (then call the base).
    virtual int pruneStaleWindows(const QSet<QString>& aliveWindowIds);

    /// The exact rect this engine last APPLIED to @p windowId while managing it
    /// (its tile rect), remembered PAST the window's transition out of the
    /// managed state. On a float toggle the engine flips its managed bit before
    /// the compositor repositions the window, so state predicates
    /// (isWindowTiled et al.) already answer "not managed" while the live frame
    /// still IS the managed rect — the capture orchestrator compares against
    /// this to refuse adopting such a frame as free/float geometry. Invalid
    /// when the engine never applied a rect (the base default: engines whose
    /// managed rects are resolvable from persisted state, like snap's zone
    /// geometry, don't need it).
    virtual QRect lastManagedRect(const QString& windowId) const
    {
        Q_UNUSED(windowId)
        return {};
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Settings — universal pattern for all engines
    //
    // The daemon calls setEngineSettings() once at startup with a QObject*
    // that implements the engine's specific settings interface (e.g.
    // IAutotileSettings, ISnapSettings). Engines qobject_cast at point of
    // use to their interface type. No caching, no bridge, no signal wiring
    // inside the engine — the daemon handles change signals externally.
    // ═══════════════════════════════════════════════════════════════════════════

    void setEngineSettings(QObject* settings);
    QObject* engineSettings() const
    {
        return m_engineSettings;
    }

    // Public dtor required for unique_ptr<PlacementEngineBase> in Daemon.
    ~PlacementEngineBase() override;

protected:
    explicit PlacementEngineBase(QObject* parent = nullptr);

Q_SIGNALS:
    void geometryRestoreRequested(const QString& windowId, const QRect& geometry, const QString& screenId);

    void navigationFeedback(bool success, const QString& action, const QString& reason, const QString& sourceId,
                            const QString& targetId, const QString& screenId);
    void windowFloatingChanged(const QString& windowId, bool floating, const QString& screenId);
    void activateWindowRequested(const QString& windowId);

    /// Emitted when directional navigation moves a window across virtual
    /// desktops: the engine has already re-keyed its own tiling state, and the
    /// compositor must move the real window to @p desktop (1-based). Relayed
    /// over D-Bus to the KWin effect, which calls windowToDesktops.
    void windowDesktopMoveRequested(const QString& windowId, int desktop);

    /// Emitted when daemon-initiated directional navigation moves a window
    /// across physical outputs: the engine has already migrated its own tiling
    /// state (removed from the source key, re-added on @p targetScreenId) and
    /// scheduled both reflows. The compositor's resulting KWin::Window::
    /// outputChanged for this window is therefore EXPECTED and must NOT be
    /// re-processed as a fresh close/open — doing so re-resolves the window to
    /// the already-updated destination key and tears down the daemon's
    /// placement (the source monitor's gap then never reflows). The effect
    /// records this one-shot and, on the matching outputChanged, only refreshes
    /// its bookkeeping + moves the decoration claim. Genuine USER-DRAG
    /// cross-output moves carry no such marker and still drive close/open.
    void windowOutputMoveExpected(const QString& windowId, const QString& targetScreenId);

    /// Emitted when a directional MOVE reaches a context boundary whose target
    /// is a DIFFERENT tiling mode than the source — the source engine cannot
    /// place the window itself (it has no state for the other mode), so it defers
    /// to the daemon. The daemon resolves the target mode, relinquishes the
    /// window from this engine (handoffRelease) and hands it to the target engine
    /// (handoffReceive): autotile inserts it into the stack, snap snaps it into
    /// the entry zone (monitor crossing) or equivalent zone (desktop crossing).
    /// @p targetDesktop is 0 for a same-desktop monitor crossing, or the 1-based
    /// destination desktop for a virtual-desktop crossing. @p direction is the
    /// move direction ("left"/"right"/"up"/"down").
    void crossModeMoveRequested(const QString& windowId, const QString& targetScreenId, int targetDesktop,
                                const QString& direction);

    /// Emitted when a directional SWAP reaches a context boundary whose target is
    /// a DIFFERENT tiling mode than the source — the two-way cross-mode exchange.
    /// The daemon resolves the target's entry-edge window (the partner facing the
    /// source in @p direction) and trades the two: the focused window crosses to
    /// the partner's position on the target surface, the partner returns to the
    /// focused window's vacated position on the source. With no partner (empty
    /// entry edge) it degrades to a plain cross-mode move. Same parameter meaning
    /// as crossModeMoveRequested: @p targetDesktop is 0 for a monitor crossing,
    /// else the 1-based destination desktop; @p direction is the swap direction.
    void crossModeSwapRequested(const QString& windowId, const QString& targetScreenId, int targetDesktop,
                                const QString& direction);

    /// Emitted to sync floating state without restoring geometry.
    /// Passive state-sync: engine-internal divergence correction.
    void windowFloatingStateSynced(const QString& windowId, bool floating, const QString& screenId);

    /// Emitted when overflow windows are batch-floated during applyTiling.
    void windowsBatchFloated(const QStringList& windowIds, const QString& screenId);

    /// Emitted when the active tiling algorithm changes.
    void algorithmChanged(const QString& algorithmId);

    /// Emitted when the placement layout changes for a screen.
    void placementChanged(const QString& screenId);

    /// Emitted when windows are released from engine management.
    void windowsReleased(const QStringList& windowIds, const QSet<QString>& releasedScreenIds);

    /// Emitted when the engine writes tuning values back to the settings
    /// object and wants the daemon to persist them to disk.
    void settingsPersistRequested();

private:
    QPointer<QObject> m_engineSettings;
};

} // namespace PhosphorEngine
