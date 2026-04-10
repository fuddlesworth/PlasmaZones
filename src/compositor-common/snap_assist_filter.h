// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "compositor_bridge.h"
#include "dbus_types.h"
#include "window_id.h"

#include <QSet>
#include <QString>

namespace PlasmaZones {

/**
 * @brief Compositor-agnostic snap assist candidate builder
 *
 * Filters windows through the ICompositorBridge interface to build
 * the candidate JSON array for the snap assist overlay.
 */
namespace SnapAssistFilter {

/**
 * @brief Build snap assist candidate list from all visible windows.
 *
 * Filters out: minimized, wrong desktop/activity, excluded by shouldHandle,
 * already snapped (by exact ID or appId with single instance), wrong screen.
 *
 * @param bridge            Compositor bridge for window access
 * @param excludeWindowId   Window to exclude (the one that was just snapped)
 * @param screenId          Only include windows on this screen (empty = all)
 * @param snappedWindowIds  Set of window IDs that are already snapped
 * @return Typed list of candidates (windowId, icon, caption; compositorHandle left empty — compositor fills it)
 */
SnapAssistCandidateList buildCandidates(ICompositorBridge* bridge, const QString& excludeWindowId,
                                        const QString& screenId, const QSet<QString>& snappedWindowIds);

} // namespace SnapAssistFilter
} // namespace PlasmaZones
