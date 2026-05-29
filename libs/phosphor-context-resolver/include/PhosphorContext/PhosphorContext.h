// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

/**
 * @file PhosphorContext.h
 * @brief Umbrella header pulling in the entire public PhosphorContext API.
 *
 * Consumers can include this for convenience or pick individual headers
 * for a smaller compile-time footprint. The lib's public surface is
 * intentionally narrow: one value type (`ContextHandle`), one enum
 * (`DisabledReason`), three adapter interfaces (`IWorkspaceState`,
 * `IModeProvider`, `IContextGateSource`), one façade interface
 * (`IContextResolver`), and one concrete resolver (`ContextResolver`).
 */

#include <PhosphorContext/ContextHandle.h>
#include <PhosphorContext/ContextResolver.h>
#include <PhosphorContext/DisabledReason.h>
#include <PhosphorContext/IContextInputs.h>
#include <PhosphorContext/IContextResolver.h>
