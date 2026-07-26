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
/// Scrolling has no layout entity, so its whole id is the bare sentinel —
/// it exists so a mode-only Scrolling assignment carries a NON-EMPTY
/// activeLayoutId() and survives the assignment cascade's non-empty-id
/// visitors (exactly like the bare "autotile:" shape the KCM writes).
inline constexpr QLatin1String ScrollingId{"scrolling:"};

inline bool isAutotile(const QString& id)
{
    return id.startsWith(AutotilePrefix);
}

inline bool isScrolling(const QString& id)
{
    // Exact compare, not startsWith: unlike AutotilePrefix, this sentinel
    // carries no payload — "scrolling:foo" is NOT a valid id, and prefix
    // matching would classify it as Scrolling while silently dropping the
    // "foo" in every consumer.
    return id == ScrollingId;
}

/// Extract the algorithm id portion from an autotile preview id.
/// (The misuse warning below is a bare qWarning by design: PhosphorLayoutApi
/// declares no logging category of its own — it is a contract library, and the
/// few .cpp files it does build carry only interface glue, no logging. The
/// message is a programmer-error flag, not runtime noise worth filtering. These
/// helpers are `inline` and header-resident so a consumer can classify an id
/// without linking anything.)
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
    // Empty algorithmId is a legitimate caller intent: "autotile mode, let
    // the engine pick the default algorithm". The KCM writes exactly this
    // shape via LayoutAdaptor::setAssignmentEntry (mode=Autotile,
    // tilingAlgorithm="") — see AssignmentEntry::activeLayoutId() and
    // LayoutRegistry::setAssignmentEntryDirect's comment. Returning the
    // bare prefix round-trips cleanly: isAutotile("autotile:") is true,
    // extractAlgorithmId("autotile:") returns empty (LayoutId.h above
    // handles id.size() <= prefix.size()), and the assignment cascade
    // treats the entry as non-empty so modeForScreen correctly reports
    // Autotile.
    return AutotilePrefix + algorithmId;
}

} // namespace LayoutId
} // namespace PhosphorLayout
