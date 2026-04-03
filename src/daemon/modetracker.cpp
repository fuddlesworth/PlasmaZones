// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "modetracker.h"

#include "../autotile/AlgorithmRegistry.h"
#include "../config/settings.h"
#include "../core/constants.h"
#include "../core/layoutmanager.h"
#include "../core/logging.h"
#include "../core/screenmanager.h"

namespace PlasmaZones {

ModeTracker::ModeTracker(Settings* settings, LayoutManager* layoutManager, QObject* parent)
    : QObject(parent)
    , m_settings(settings)
    , m_layoutManager(layoutManager)
{
}

ModeTracker::~ModeTracker() = default;

void ModeTracker::setContext(const QString& screenId, int desktop, const QString& activity)
{
    m_screenId = screenId;
    m_desktop = desktop;
    m_activity = activity;
}

TilingMode ModeTracker::currentMode() const
{
    if (!m_layoutManager || m_screenId.isEmpty()) {
        return TilingMode::Manual;
    }
    auto mode = m_layoutManager->modeForScreen(m_screenId, m_desktop, m_activity);
    return (mode == AssignmentEntry::Autotile) ? TilingMode::Autotile : TilingMode::Manual;
}

bool ModeTracker::isAnyScreenAutotile() const
{
    if (!m_layoutManager) {
        return false;
    }
    const QStringList effectiveIds = ScreenManager::effectiveScreenIdsWithFallback();
    for (const QString& screenId : effectiveIds) {
        const QString assignmentId = m_layoutManager->assignmentIdForScreen(screenId, m_desktop, m_activity);
        if (LayoutId::isAutotile(assignmentId)) {
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
        return m_settings ? m_settings->defaultAutotileAlgorithm() : AlgorithmRegistry::defaultAlgorithmId();
    }
    QString algo = m_layoutManager->tilingAlgorithmForScreen(m_screenId, m_desktop, m_activity);
    if (algo.isEmpty() && m_settings) {
        algo = m_settings->defaultAutotileAlgorithm();
    }
    return algo.isEmpty() ? AlgorithmRegistry::defaultAlgorithmId() : algo;
}

} // namespace PlasmaZones
