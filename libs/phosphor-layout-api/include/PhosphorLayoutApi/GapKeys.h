// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QLatin1String>

namespace PhosphorLayout {

/// Canonical JSON key strings for per-side outer-gap fields.
///
/// The wire format is intentionally shared between manual zone layouts
/// (phosphor-zones) and autotile algorithm configs (phosphor-tiles) so a
/// downstream overlay / animation / KCM layer can read either kind without
/// branching. Both libraries depend on phosphor-layout-api — this header is
/// the single source of truth for these strings.
namespace GapKeys {

inline constexpr QLatin1String UsePerSideOuterGap{"usePerSideOuterGap"};
inline constexpr QLatin1String OuterGapTop{"outerGapTop"};
inline constexpr QLatin1String OuterGapBottom{"outerGapBottom"};
inline constexpr QLatin1String OuterGapLeft{"outerGapLeft"};
inline constexpr QLatin1String OuterGapRight{"outerGapRight"};

} // namespace GapKeys
} // namespace PhosphorLayout
