// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Forwarding shim. The canonical home for the ScreenManager class is
// <PhosphorScreens/Manager.h>; the daemon's process-global accessors and
// the previously-static fallback helpers live in
// <core/screenmanagerservice.h>.
//
// This file keeps the legacy `core/screenmanager.h` include path working
// for daemon-internal callers that haven't yet migrated. New code should
// include the canonical headers directly.

#include "screenmanagerservice.h" // pulls in <PhosphorScreens/Manager.h> + global helpers
