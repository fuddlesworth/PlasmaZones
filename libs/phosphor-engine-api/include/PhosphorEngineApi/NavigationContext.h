// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QString>

namespace PhosphorEngineApi {

/// Target window + screen for a navigation or lifecycle operation.
///
/// Populated by the daemon from compositor shadow state (windowActivated /
/// cursorScreenChanged) before dispatching through the placement engine.
/// Both fields may be empty on very-early-startup shortcuts or when no
/// window is focused — each IPlacementEngine method documents its
/// behaviour when the fields are empty.
struct NavigationContext
{
    QString windowId;
    QString screenId;
};

} // namespace PhosphorEngineApi
