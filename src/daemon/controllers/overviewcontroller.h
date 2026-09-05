// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "overviewmodelbuilder.h"

#include "core/interfaces/ioverviewpolicy.h"

#include <functional>

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QTimer>

namespace PhosphorEngine {
class IOverviewModelSource;
class WindowRegistry;
}
namespace PhosphorScreens {
class ScreenManager;
}
namespace PhosphorWorkspaces {
class ActivityManager;
class VirtualDesktopManager;
}
namespace PhosphorZones {
class LayoutRegistry;
}

namespace PlasmaZones {

class ISettings;
class WorkspaceController;

/// Daemon glue for the workspace overview. Constructed ONLY while the
/// workspaces feature is on (the gate wraps the object, as it does for
/// WorkspaceController). Owns the open state the KWin overview effect
/// reports, builds the per-(screen, desktop) model from the engines' read
/// surfaces while open, publishes it change-gated with generation stamps,
/// and executes the overview's verbs against the WorkspaceController and the
/// engines. The OverviewAdaptor is the wire; this is the policy.
class OverviewController : public IOverviewPolicy
{
    Q_OBJECT

public:
    /// The three engine read surfaces are passed by mode so the builder can
    /// route each workspace to the engine that owns its mode. Any may be
    /// null (engine absent); its workspaces then fall back to tracked
    /// geometry.
    struct Sources
    {
        PhosphorEngine::IOverviewModelSource* snapping = nullptr;
        PhosphorEngine::IOverviewModelSource* tiling = nullptr;
        PhosphorEngine::IOverviewModelSource* scrolling = nullptr;
    };

    OverviewController(WorkspaceController* workspaces, PhosphorWorkspaces::VirtualDesktopManager* vdm,
                       PhosphorScreens::ScreenManager* screens, PhosphorZones::LayoutRegistry* layouts,
                       ISettings* settings, PhosphorEngine::WindowRegistry* registry, const Sources& sources,
                       QObject* parent = nullptr);

    /// Inject the window → physical-screen resolver (the daemon backs it with
    /// the engines' tracking, then the placement store). The builder keys a
    /// window to a screen with it.
    void setWindowScreenResolver(std::function<QString(const QString& windowId)> resolver);
    /// Inject the sticky (on-all-desktops) predicate; unset means "not sticky".
    void setWindowStickyPredicate(std::function<bool(const QString& windowId)> predicate);
    /// Inject the current-activity source. Unset means the empty activity.
    void setActivityManager(PhosphorWorkspaces::ActivityManager* activities);

    /// The streaming gate. Opening builds and publishes immediately (the
    /// adaptor's replay reads the result); closing stops the stream and
    /// clears the replay so a reopen never serves a stale payload.
    void setOpen(bool open) override;
    bool isOpen() const override
    {
        return m_open;
    }

    /// Replay payload: the last published model, empty while closed.
    QString modelJson() const override
    {
        return m_open ? m_lastPublished : QString();
    }

    /// Coalesced rebuild request (0 ms single-shot). Every model input
    /// change routes here; a no-op while closed.
    void scheduleRebuild();

    /// Payload generation of the last publish (monotonic for the daemon's
    /// lifetime, so a reopen continues rather than restarts). Test seam.
    quint64 generation() const
    {
        return m_generation;
    }

    /// Build the model inputs from the live objects. Public so the daemon
    /// test can compare a live build against the builder with fakes.
    OverviewModelBuilder::Inputs buildInputs() const;

Q_SIGNALS:
    /// The open state changed (setOpen, or an activity change forcing a
    /// close). modelPublished and closeRequested are inherited from
    /// IOverviewPolicy.
    void openChanged(bool open);

private:
    void rebuildNow();
    QString currentActivity() const;

    WorkspaceController* m_workspaces;
    PhosphorWorkspaces::VirtualDesktopManager* m_vdm;
    PhosphorScreens::ScreenManager* m_screens;
    PhosphorZones::LayoutRegistry* m_layouts;
    ISettings* m_settings;
    PhosphorEngine::WindowRegistry* m_registry;
    PhosphorWorkspaces::ActivityManager* m_activities = nullptr;
    Sources m_sources;
    std::function<QString(const QString&)> m_windowScreenResolver;
    std::function<bool(const QString&)> m_windowStickyPredicate;

    bool m_open = false;
    QTimer m_rebuildTimer;
    /// The last payload WITHOUT its generation stamps, for the change gate,
    /// and the map generation it was built against.
    QJsonObject m_lastBody;
    quint64 m_lastMapGeneration = 0;
    QString m_lastPublished;
    quint64 m_generation = 0;
    QString m_openActivity;
};

} // namespace PlasmaZones
