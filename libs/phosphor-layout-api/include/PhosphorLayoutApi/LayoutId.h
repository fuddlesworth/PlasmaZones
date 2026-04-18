// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <QLatin1String>
#include <QString>
#include <QtGlobal>

namespace PhosphorLayout {

/// LayoutPreview::id namespace utilities.
///
/// Manual zone layouts use UUID strings; autotile-algorithm previews use the
/// prefixed form `"autotile:<algorithmId>"` so manual + autotile IDs share a
/// single namespace at the consumer level. Everyone who needs to build / parse
/// / classify a LayoutPreview id goes through the helpers here — no inline
/// `"autotile:"` literals outside this namespace.
///
/// Lives in phosphor-layout-api because both phosphor-zones and phosphor-tiles
/// produce LayoutPreview values with these IDs, and every consumer of
/// ILayoutSource must be able to classify an id without pulling in either
/// library-specific header.
namespace LayoutId {

inline constexpr QLatin1String AutotilePrefix{"autotile:"};

inline bool isAutotile(const QString& id)
{
    return id.startsWith(AutotilePrefix);
}

/// Extract the algorithm id portion from an autotile preview id.
/// Callers are expected to check @c isAutotile first — passing a non-autotile
/// id here is a contract violation. We warn and return an empty string so
/// the misuse is loud rather than silent, but remain graceful in release
/// (no assert — the empty return gives callers a testable signal).
inline QString extractAlgorithmId(const QString& id)
{
    if (!isAutotile(id)) {
        qWarning("PhosphorLayout::LayoutId::extractAlgorithmId called with non-autotile id: %s", qUtf8Printable(id));
        return QString();
    }
    if (id.size() <= AutotilePrefix.size()) {
        return {};
    }
    return id.mid(AutotilePrefix.size());
}

inline QString makeAutotileId(const QString& algorithmId)
{
    if (algorithmId.isEmpty()) {
        // Producing the bare prefix would round-trip through isAutotile(true)
        // → extractAlgorithmId("") which downstream code can't act on. Surface
        // the misuse loudly while staying graceful (empty return is testable).
        qWarning("PhosphorLayout::LayoutId::makeAutotileId called with empty algorithmId");
        return QString();
    }
    return AutotilePrefix + algorithmId;
}

} // namespace LayoutId
} // namespace PhosphorLayout
