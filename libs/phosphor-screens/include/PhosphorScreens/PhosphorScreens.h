// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

// PhosphorScreens — domain-free screen-topology library for Qt6 Wayland.
//
// This is the umbrella header. Right now it is intentionally empty: the
// library is at the step-1 scaffold from
// docs/phosphor-screens-api-design.md and ships no public types yet.
//
#include <PhosphorScreens/IConfigStore.h>
#include <PhosphorScreens/IPanelSource.h>
#include <PhosphorScreens/InMemoryConfigStore.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorScreens/NoOpPanelSource.h>
#include <PhosphorScreens/PlasmaPanelSource.h>
#include <PhosphorScreens/Resolver.h>
#include <PhosphorScreens/ScreenIdentity.h>
#include <PhosphorScreens/Swapper.h>
#include <PhosphorScreens/VirtualScreen.h>

namespace Phosphor::Screens {

// Reserved. Additional types arrive in the lift-and-shift PRs.

} // namespace Phosphor::Screens
