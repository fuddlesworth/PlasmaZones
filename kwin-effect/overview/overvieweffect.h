// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <effect/effecttogglablestate.h>
#include <effect/quickeffect.h>

#include <PhosphorAnimation/CurveRegistry.h>
#include <PhosphorAnimation/Profile.h>
#include <PhosphorAnimation/ProfileTree.h>

#include <QColor>
#include <QHash>
#include <QPointF>
#include <QString>
#include <QVariantMap>

class QTimer;

namespace PlasmaZones::Overview {

class DaemonClient;

/// The workspace overview: a zoomed-out, per-monitor view of every workspace
/// of the dynamic-workspaces map, rendered by one QML view per output. The
/// daemon is the layout authority (the model names every window's rect per
/// workspace); this effect renders, grabs input, animates, and sends every
/// mutation back to the daemon as a verb.
///
/// Modelled on KWin's own Overview: one EffectTogglableState drives the open
/// state, a gesture partially activates it, the QML animates the progress
/// value itself, and a shutdown timer keeps the effect running for one
/// animation duration after deactivate() so the close animation plays.
class OverviewEffect : public KWin::QuickSceneEffect
{
    Q_OBJECT
    /// The raw open factor: the gesture value during a swipe, a 0/1 step on
    /// toggle. The QML animates its own progress toward this.
    Q_PROPERTY(qreal partialActivationFactor READ partialActivationFactor NOTIFY partialActivationFactorChanged)
    Q_PROPERTY(bool gestureInProgress READ gestureInProgress NOTIFY gestureInProgressChanged)
    Q_PROPERTY(int animationDuration READ animationDuration NOTIFY animationDurationChanged)
    /// Fully open zoom, 0.1 to 0.75 (niri's `overview.zoom`).
    Q_PROPERTY(qreal zoom READ zoom NOTIFY zoomChanged)
    Q_PROPERTY(QColor backdropColor READ backdropColor NOTIFY backdropColorChanged)
    Q_PROPERTY(bool showWorkspaceNames READ showWorkspaceNames NOTIFY showWorkspaceNamesChanged)
    Q_PROPERTY(bool wheelSwitchesWorkspaces READ wheelSwitchesWorkspaces NOTIFY wheelSwitchesWorkspacesChanged)
    /// The parsed dynamic-workspaces map ({v, generation, screenOrder,
    /// slices: {screenId: [{id, index, name?, current?}]}}).
    Q_PROPERTY(QVariantMap workspaceMap READ workspaceMap NOTIFY workspaceMapChanged)
    /// Bumped whenever a desktop is added, removed or renamed; a label binds
    /// to it so desktopName() is re-read when KWin's names move.
    Q_PROPERTY(int desktopNamesRevision READ desktopNamesRevision NOTIFY desktopNamesRevisionChanged)
    /// The parsed overview model (see dbus/org.plasmazones.Overview.xml).
    Q_PROPERTY(QVariantMap overviewModel READ overviewModel NOTIFY overviewModelChanged)
    Q_PROPERTY(bool daemonAvailable READ daemonAvailable NOTIFY daemonAvailableChanged)

public:
    OverviewEffect();
    ~OverviewEffect() override;

    qreal partialActivationFactor() const;
    bool gestureInProgress() const;
    int animationDuration() const
    {
        return m_animationDuration;
    }
    qreal zoom() const
    {
        return m_zoom;
    }
    QColor backdropColor() const
    {
        return m_backdropColor;
    }
    bool showWorkspaceNames() const
    {
        return m_showWorkspaceNames;
    }
    bool wheelSwitchesWorkspaces() const
    {
        return m_wheelSwitchesWorkspaces;
    }
    QVariantMap workspaceMap() const;
    QVariantMap overviewModel() const;
    bool daemonAvailable() const;

    int requestedEffectChainPosition() const override;

    /// The canonical screen id for an output, byte-identical to the main
    /// effect's outputScreenId (base id from manufacturer / model / serial /
    /// connector, plus a "/connector" suffix while a duplicate model is
    /// connected). Every id sent to the daemon comes from here.
    Q_INVOKABLE QString screenIdFor(KWin::LogicalOutput* output) const;
    /// KWin's current name for a desktop id (the name a rename pushed, or
    /// KWin's own default); empty when the desktop is gone.
    Q_INVOKABLE QString desktopName(const QString& desktopId) const;
    int desktopNamesRevision() const
    {
        return m_desktopNamesRevision;
    }
    /// The QUuid handle WindowThumbnail wants, for a daemon window id; an
    /// invalid QVariant when the window is gone.
    Q_INVOKABLE QVariant windowHandle(const QString& windowId) const;
    /// The window's index in the compositor's stacking order (bottom = 0);
    /// a window missing from that list sorts last.
    Q_INVOKABLE int stackingIndex(const QString& windowId) const;
    Q_INVOKABLE bool windowExists(const QString& windowId) const;
    Q_INVOKABLE void closeWindow(const QString& windowId);
    /// Activate (focus and raise) a window directly in the compositor. Not a
    /// daemon verb: the QML calls this after focusWorkspace so the switch and
    /// the activation land in the same close sequence.
    Q_INVOKABLE void activateWindow(const QString& windowId);
    /// The live per-output desktop swipe offset (KWin's desktopChanging), so a
    /// touchpad desktop swipe shows inside an open overview.
    Q_INVOKABLE QPointF desktopOffsetForScreen(KWin::LogicalOutput* screen) const;

    // Verb forwarders to org.plasmazones.Overview, one per daemon verb, all
    // fire-and-forget. A refusal is a daemon-side no-op and the next
    // overviewModelChanged repositions whatever the QML sprang back. Drop
    // coordinates are workspace-local logical pixels, unzoomed; slice indices
    // for the new-workspace verbs are 0-based gap indices (0 = above the
    // first workspace, N = below the last).
    Q_INVOKABLE void focusWorkspace(const QString& screenId, const QString& desktopId);
    Q_INVOKABLE void moveWindowToWorkspace(const QString& windowId, const QString& screenId, const QString& desktopId,
                                           int dropX, int dropY);
    Q_INVOKABLE void moveWindowToNewWorkspace(const QString& windowId, const QString& screenId, int sliceIndex,
                                              int dropX, int dropY);
    Q_INVOKABLE void reorderWorkspace(const QString& screenId, const QString& desktopId, int newSliceIndex);
    Q_INVOKABLE void moveWorkspaceToScreen(const QString& desktopId, const QString& targetScreenId, int sliceIndex);
    Q_INVOKABLE void renameWorkspace(const QString& desktopId, const QString& name);
    Q_INVOKABLE void pinWorkspace(const QString& desktopId, bool pinned);
    Q_INVOKABLE void panStrip(const QString& screenId, const QString& desktopId, int deltaPx);

public Q_SLOTS:
    void activate();
    void deactivate();
    void toggle();

Q_SIGNALS:
    void partialActivationFactorChanged();
    void gestureInProgressChanged();
    void animationDurationChanged();
    void zoomChanged();
    void backdropColorChanged();
    void showWorkspaceNamesChanged();
    void wheelSwitchesWorkspacesChanged();
    void workspaceMapChanged();
    void overviewModelChanged();
    void daemonAvailableChanged();
    void desktopNamesRevisionChanged();
    void desktopOffsetChanged(KWin::LogicalOutput* screen);

protected:
    QVariantMap initialProperties(KWin::LogicalOutput* screen) override;

private Q_SLOTS:
    /// Re-fetch everything the effect reads from the daemon: the global
    /// animation duration, the motion profile tree and the Workspaces.Overview
    /// group (zoom, backdrop, gesture, wheel, names). Bound to the daemon's
    /// settingsChanged and motionProfileTreeChanged broadcasts by name, hence
    /// a slot.
    void loadSettings();

private:
    void deactivateNow();
    /// Start the effect for an Activating/Active state: refused when the
    /// daemon is absent, the map is empty, the screen is locked, or KWin
    /// refuses the fullscreen slot / keyboard grab. A refusal rolls the state
    /// back to Inactive and reports it to the daemon.
    void tryStart();
    void resolveAnimationDuration();
    void setAnimationDuration(int duration);
    KWin::EffectWindow* windowFor(const QString& windowId) const;
    void watchDesktopName(KWin::VirtualDesktop* desktop);
    void bumpDesktopNames();

    KWin::EffectTogglableState* const m_state;
    QTimer* const m_shutdownTimer;
    DaemonClient* const m_daemon;
    QHash<KWin::LogicalOutput*, QPointF> m_screenDesktopOffsets;
    mutable QHash<QString, QString> m_screenIdCache;
    bool m_daemonOpen = false;
    int m_desktopNamesRevision = 0;

    int m_animationDuration = 300;
    qreal m_zoom = 0.5;
    QColor m_backdropColor = QColor(0x26, 0x26, 0x26);
    bool m_showWorkspaceNames = true;
    bool m_wheelSwitchesWorkspaces = true;
    // The swipe gesture is registered once with KWin (its QActions live on
    // the togglable state and cannot be unregistered), so the setting is a
    // gate in tryStart: a gesture-driven activation is rolled back while off.
    bool m_gestureEnabled = true;

    PhosphorAnimation::CurveRegistry m_curveRegistry;
    PhosphorAnimation::Profile m_globalMotion;
    PhosphorAnimation::ProfileTree m_motionTree;
};

} // namespace PlasmaZones::Overview
