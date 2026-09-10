// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "overviewcontroller.h"

#include "core/platform/logging.h"
#include "workspacecontroller.h"

#include <PhosphorWorkspaces/ActivityManager.h>
#include <PhosphorWorkspaces/WorkspaceReconciler.h>

#include <QJsonDocument>

namespace PlasmaZones {

OverviewController::OverviewController(WorkspaceController* workspaces, PhosphorWorkspaces::VirtualDesktopManager* vdm,
                                       PhosphorScreens::ScreenManager* screens, PhosphorZones::LayoutRegistry* layouts,
                                       ISettings* settings, PhosphorEngine::WindowRegistry* registry,
                                       const Sources& sources, QObject* parent)
    : IOverviewPolicy(parent)
    , m_workspaces(workspaces)
    , m_vdm(vdm)
    , m_screens(screens)
    , m_layouts(layouts)
    , m_settings(settings)
    , m_registry(registry)
    , m_sources(sources)
{
    m_rebuildTimer.setSingleShot(true);
    m_rebuildTimer.setInterval(0);
    connect(&m_rebuildTimer, &QTimer::timeout, this, &OverviewController::rebuildNow);
}

void OverviewController::setWindowScreenResolver(std::function<QString(const QString&)> resolver)
{
    m_windowScreenResolver = std::move(resolver);
}

void OverviewController::setWindowStickyPredicate(std::function<bool(const QString&)> predicate)
{
    m_windowStickyPredicate = std::move(predicate);
}

void OverviewController::setActivityManager(PhosphorWorkspaces::ActivityManager* activities)
{
    if (m_activities) {
        disconnect(m_activities, nullptr, this, nullptr);
    }
    m_activities = activities;
    if (!m_activities) {
        return;
    }
    // The model carries ONE activity and the workspace map has no activity
    // dimension, so an activity change underneath an open overview has no
    // honest rendering. Ask the effect to close; the effect answers with
    // setOverviewOpen(false), and the gate closes here regardless so the
    // stream cannot keep publishing the old activity's model meanwhile.
    connect(m_activities, &PhosphorWorkspaces::ActivityManager::currentActivityChanged, this,
            [this](const QString& activity) {
                if (!m_open || activity == m_openActivity) {
                    return;
                }
                qCDebug(lcDaemon) << "overview: activity changed while open, closing";
                Q_EMIT closeRequested();
                setOpen(false);
            });
}

QString OverviewController::currentActivity() const
{
    return m_activities ? m_activities->currentActivity() : QString();
}

void OverviewController::setOpen(bool open)
{
    if (m_open == open) {
        return;
    }
    m_open = open;
    if (open) {
        m_openActivity = currentActivity();
        // Synchronous first build: the adaptor's setOverviewOpen(true) is
        // followed by an overviewModel() replay on the same round-trip, and a
        // deferred build would answer that replay with an empty model.
        rebuildNow();
    } else {
        m_rebuildTimer.stop();
        m_lastBody = QJsonObject();
        m_lastPublished.clear();
    }
    Q_EMIT openChanged(m_open);
}

void OverviewController::scheduleRebuild()
{
    if (!m_open) {
        return;
    }
    m_rebuildTimer.start();
}

void OverviewController::rebuildNow()
{
    if (!m_open) {
        return;
    }
    const QJsonObject body = OverviewModelBuilder::build(buildInputs());
    // Change gate on the UNSTAMPED body: the generation counters are the only
    // thing that would differ between two identical builds, and stamping
    // before comparing would make every rebuild a "change". The map
    // generation the body was built against is compared beside it: a reorder
    // that moved no window rect still changes it, and the effect keys its
    // consistency check on that field, so it must republish then.
    const quint64 mapGeneration = m_workspaces ? m_workspaces->reconciler().generation() : 0;
    if (!m_lastPublished.isEmpty() && body == m_lastBody && mapGeneration == m_lastMapGeneration) {
        return;
    }
    m_lastBody = body;
    m_lastMapGeneration = mapGeneration;
    ++m_generation;
    QJsonObject stamped = body;
    stamped.insert(QLatin1String("workspaceMapGeneration"), static_cast<double>(mapGeneration));
    stamped.insert(QLatin1String("generation"), static_cast<double>(m_generation));
    m_lastPublished = QString::fromUtf8(QJsonDocument(stamped).toJson(QJsonDocument::Compact));
    Q_EMIT modelPublished(m_lastPublished);
}

} // namespace PlasmaZones
