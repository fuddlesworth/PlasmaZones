// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

/// Umbrella convenience header — pulls in the entire PhosphorProtocol
/// surface. Prefer the per-interface headers (e.g. `DragTypes.h`,
/// `AutotileMarshalling.h`) so a translation unit recompiles only when the
/// interface it actually uses changes.

#include <PhosphorProtocol/AutotileMarshalling.h>
#include <PhosphorProtocol/BridgeMarshalling.h>
#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/DragMarshalling.h>
#include <PhosphorProtocol/NavigationMarshalling.h>
#include <PhosphorProtocol/Registration.h>
#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorProtocol/WindowMarshalling.h>
#include <PhosphorProtocol/ZoneMarshalling.h>
