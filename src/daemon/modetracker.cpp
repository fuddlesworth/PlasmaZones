// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "modetracker.h"

#include <PhosphorTiles/AlgorithmRegistry.h>
#include "../config/settings.h"
#include "../core/constants.h"
#include <PhosphorZones/LayoutManager.h>
#include "../core/logging.h"
#include <PhosphorScreens/Manager.h>

namespace PlasmaZones {

ModeTracker::ModeTracker(Settings* settings, PhosphorZones::LayoutManager* layoutManager,
                         Phosphor::Screens::ScreenManager* screenManager, QObject* parent)
    : QObject(parent)
    , m_settings(settings)
    , m_layoutManager(layoutManager)
    , m_screenManager(screenManager)
{
}

ModeTracker::~ModeTracker() = default;

void ModeTracker::setContext(const QString& screenId, int desktop, const QString& activity)
{
    const TilingMode oldMode = currentMode();
    m_screenId = screenId;
    m_desktop = desktop;
    m_activity = activity;
    const TilingMode newMode = currentMode();
    if (newMode != oldMode) {
        Q_EMIT currentModeChanged(newMode);
    }
}

TilingMode ModeTracker::currentMode() const
{
    if (!m_layoutManager || m_screenId.isEmpty()) {
        return TilingMode::Manual;
    }
    auto mode = m_layoutManager->modeForScreen(m_screenId, m_desktop, m_activity);
    return (mode == PhosphorZones::AssignmentEntry::Autotile) ? TilingMode::Autotile : TilingMode::Manual;
}

bool ModeTracker::isAnyScreenAutotile(int desktop, const QString& activity) const
{
    if (!m_layoutManager) {
        return false;
    }
    // Use caller-supplied values when provided; fall back to stored context.
    const int effectiveDesktop = (desktop >= 0) ? desktop : m_desktop;
    const QString& effectiveActivity = activity.isEmpty() ? m_activity : activity;
    const QStringList effectiveIds = m_screenManager ? m_screenManager->effectiveScreenIds() : QStringList();
    for (const QString& screenId : effectiveIds) {
        const QString assignmentId =
            m_layoutManager->assignmentIdForScreen(screenId, effectiveDesktop, effectiveActivity);
        if (PhosphorLayout::LayoutId::isAutotile(assignmentId)) {
            return true;
        }
    }
    return false;
}

QString ModeTracker::lastManualLayoutId() const
{
    if (!m_layoutManager || m_screenId.isEmpty()) {
        return {};
    }
    return m_layoutManager->snappingLayoutForScreen(m_screenId, m_desktop, m_activity);
}

QString ModeTracker::lastAutotileAlgorithm() const
{
    if (!m_layoutManager || m_screenId.isEmpty()) {
        return m_settings ? m_settings->defaultAutotileAlgorithm()
                          : PhosphorTiles::AlgorithmRegistry::staticDefaultAlgorithmId();
    }
    QString algo = m_layoutManager->tilingAlgorithmForScreen(m_screenId, m_desktop, m_activity);
    if (algo.isEmpty() && m_settings) {
        algo = m_settings->defaultAutotileAlgorithm();
    }
    return algo.isEmpty() ? PhosphorTiles::AlgorithmRegistry::staticDefaultAlgorithmId() : algo;
}

} // namespace PlasmaZones
