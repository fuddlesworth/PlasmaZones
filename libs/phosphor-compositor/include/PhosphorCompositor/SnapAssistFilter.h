// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorcompositor_export.h>

#include <PhosphorCompositor/ICompositorBridge.h>
#include <PhosphorIdentity/WindowId.h>
#include <PhosphorProtocol/WireTypes.h>

#include <QSet>
#include <QString>

namespace PhosphorCompositor {

using PhosphorProtocol::SnapAssistCandidate;
using PhosphorProtocol::SnapAssistCandidateList;

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
 * already snapped (by exact ID or appId with single instance), wrong physical
 * monitor.
 *
 * @param bridge            Compositor bridge for window access
 * @param excludeWindowId   Window to exclude (the one that was just snapped)
 * @param screenId          Only include windows on the same *physical* monitor
 *                          as this ID (empty = all). Sibling virtual screens of
 *                          the same physical monitor are accepted — matched via
 *                          @c PhosphorIdentity::VirtualScreenId::samePhysical.
 * @param snappedWindowIds  Set of window IDs that are already snapped
 * @return Typed list of candidates (windowId, icon, caption; compositorHandle left empty — compositor fills it)
 */
PHOSPHORCOMPOSITOR_EXPORT SnapAssistCandidateList buildCandidates(ICompositorBridge* bridge,
                                                                  const QString& excludeWindowId,
                                                                  const QString& screenId,
                                                                  const QSet<QString>& snappedWindowIds);

} // namespace SnapAssistFilter
} // namespace PhosphorCompositor
