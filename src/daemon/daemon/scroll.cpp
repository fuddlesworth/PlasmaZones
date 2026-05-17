// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../daemon.h"

#include "../../core/logging.h"
#include "../../dbus/scrolladaptor.h"
#include "../../dbus/windowtrackingadaptor.h"
#include "../config/settings.h"
#include <PhosphorEngine/IPlacementEngine.h>
#include <PhosphorIdentity/VirtualScreenId.h>
#include <PhosphorProtocol/WindowTypes.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorScreens/ScreenIdentity.h>
#include <PhosphorScrollEngine/ScrollEngine.h>
#include <PhosphorScrollEngine/ScrollLayout.h>
#include <PhosphorScrollEngine/ScrollScreenState.h>
#include <PhosphorZones/LayoutRegistry.h>

#include <QHash>
#include <QRect>
#include <QRectF>
#include <QScreen>
#include <QSet>
#include <QString>
#include <QStringList>

namespace PlasmaZones {

namespace {
// Scroll-mode layout gaps in logical pixels. The outer gap insets the strip
// from the working-area edges; the inner gap separates adjacent columns and
// the tiles within a column.
constexpr qreal SCROLL_OUTER_GAP = 8.0;
constexpr qreal SCROLL_INNER_GAP = 8.0;
} // anonymous namespace

void Daemon::updateScrollScreens()
{
    if (!m_scrollEngine || !m_layoutManager || !m_screenManager) {
        return;
    }

    const int desktop = currentDesktop();
    const QString activity = currentActivity();

    // Align the engine's per-context key with the daemon's current
    // desktop/activity before resolving — mirrors the setCurrentDesktop /
    // setCurrentActivity-before-update pattern used for autotile.
    m_scrollEngine->setCurrentDesktop(desktop);
    m_scrollEngine->setCurrentActivity(activity);

    QSet<QString> scrollScreens;
    const QStringList effectiveIds = m_screenManager->effectiveScreenIds();
    for (const QString& screenId : effectiveIds) {
        if (isContextDisabled(m_settings.get(), PhosphorZones::AssignmentEntry::Scroll, screenId, desktop, activity)) {
            continue;
        }
        const QString assignmentId = m_layoutManager->assignmentIdForScreen(screenId, desktop, activity);
        if (PhosphorLayout::LayoutId::isScroll(assignmentId)) {
            scrollScreens.insert(screenId);
        }
    }

    const bool screensChanged = (m_scrollEngine->activeScreens() != scrollScreens);
    m_scrollEngine->setActiveScreens(scrollScreens);
    if (screensChanged && m_scrollAdaptor) {
        // Tell the KWin effect which screens are scroll-mode so it reports
        // their windows to the org.plasmazones.Scroll interface.
        Q_EMIT m_scrollAdaptor->scrollScreensChanged(QStringList(scrollScreens.cbegin(), scrollScreens.cend()));
    }
    qCDebug(lcDaemon) << "Updated scroll screens=" << scrollScreens;
}

void Daemon::onScrollPlacementChanged(const QString& screenId)
{
    if (screenId.isEmpty() || !m_scrollEngine || !m_screenManager || !m_windowTrackingAdaptor) {
        return;
    }
    // ScrollEngine is geometry-agnostic — it stores the strip; the daemon
    // resolves it to pixels because only the daemon knows the working area.
    auto* scroll = dynamic_cast<PhosphorScrollEngine::ScrollEngine*>(m_scrollEngine.get());
    if (!scroll) {
        return;
    }
    const auto* state = dynamic_cast<const PhosphorScrollEngine::ScrollScreenState*>(scroll->stateForScreen(screenId));
    if (!state) {
        return;
    }

    // Panel-excluded working area, virtual-screen aware — mirrors
    // AutotileEngine::screenGeometry().
    QRect workArea;
    if (PhosphorIdentity::VirtualScreenId::isVirtual(screenId)) {
        workArea = m_screenManager->screenAvailableGeometry(screenId);
    } else if (QScreen* screen = Phosphor::Screens::ScreenIdentity::findByIdOrName(screenId)) {
        workArea = m_screenManager->actualAvailableGeometry(screen);
    }
    if (!workArea.isValid()) {
        return;
    }

    PhosphorScrollEngine::ScrollLayoutConfig config;
    config.outerGap = SCROLL_OUTER_GAP;
    config.innerGap = SCROLL_INNER_GAP;
    config.presetWindowHeights = scroll->presetWindowHeights();

    const QHash<QString, QRectF> geometries =
        PhosphorScrollEngine::resolveScrollLayout(*state, QRectF(workArea), config);
    if (geometries.isEmpty()) {
        return;
    }

    PhosphorProtocol::WindowGeometryList batch;
    batch.reserve(geometries.size());
    for (auto it = geometries.cbegin(); it != geometries.cend(); ++it) {
        batch.append(PhosphorProtocol::WindowGeometryEntry::fromRect(it.key(), it.value().toRect(), screenId));
    }
    Q_EMIT m_windowTrackingAdaptor->applyGeometriesBatch(batch, QStringLiteral("scroll"));
}

} // namespace PlasmaZones
