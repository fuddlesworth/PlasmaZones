// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

namespace PhosphorContext {

/**
 * @brief Why a context is considered disabled, in priority order.
 *
 * The disable cascade walks Monitor → Desktop → Activity and the first
 * non-Disabled bucket wins. The enum is ordered so callers can reason
 * about "highest-priority reason" — if a screen is monitor-disabled the
 * desktop / activity gates are short-circuited.
 *
 * Mirrors the GPL daemon's `PlasmaZones::DisabledReason` so consumers can
 * map at the boundary, but lives in PhosphorContext so the LGPL library
 * does not pull in any GPL header.
 */
enum class DisabledReason {
    NotDisabled,
    MonitorDisabled,
    DesktopDisabled,
    ActivityDisabled,
};

} // namespace PhosphorContext
