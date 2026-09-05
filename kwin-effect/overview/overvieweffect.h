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
    /// The QUuid handle WindowThumbnail wants, for a daemon window id; an
    /// invalid QVariant when the window is gone.
    Q_INVOKABLE QVariant windowHandle(const QString& windowId) const;
    /// The window's index in the compositor's stacking order (bottom = 0);
    /// a window missing from that list sorts last.
    Q_INVOKABLE int stackingIndex(const QString& windowId) const;
    Q_INVOKABLE bool windowExists(const QString& windowId) const;
    Q_INVOKABLE void closeWindow(const QString& windowId);
    /// The live per-output desktop swipe offset (KWin's desktopChanging), so a
    /// touchpad desktop swipe shows inside an open overview.
    Q_INVOKABLE QPointF desktopOffsetForScreen(KWin::LogicalOutput* screen) const;

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
    void desktopOffsetChanged(KWin::LogicalOutput* screen);

protected:
    QVariantMap initialProperties(KWin::LogicalOutput* screen) override;

private Q_SLOTS:
    /// Re-fetch the two settings the effect reads (global animation duration
    /// and the motion profile tree). Bound to the daemon's settingsChanged and
    /// motionProfileTreeChanged broadcasts by name, hence a slot.
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

    KWin::EffectTogglableState* const m_state;
    QTimer* const m_shutdownTimer;
    DaemonClient* const m_daemon;
    QHash<KWin::LogicalOutput*, QPointF> m_screenDesktopOffsets;
    mutable QHash<QString, QString> m_screenIdCache;
    bool m_daemonOpen = false;

    int m_animationDuration = 300;
    qreal m_zoom = 0.5;
    QColor m_backdropColor = QColor(0x26, 0x26, 0x26);
    bool m_showWorkspaceNames = true;
    bool m_wheelSwitchesWorkspaces = true;

    PhosphorAnimation::CurveRegistry m_curveRegistry;
    PhosphorAnimation::Profile m_globalMotion;
    PhosphorAnimation::ProfileTree m_motionTree;
};

} // namespace PlasmaZones::Overview
