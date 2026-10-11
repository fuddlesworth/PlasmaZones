// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorIdentity/VirtualScreenId.h>

#include <QRect>
#include <QString>

/// Which scrolling strip a screen id names, and the rect its edges are
/// measured against, header-only so the rules are unit-testable without a
/// compositor (same pattern as pretiledecisions.h). A virtual screen running
/// scrolling is a strip of its own beside its siblings (F368).
namespace PlasmaZones::StripKeys {

/// The key a scrolling strip's own state lives under. A virtual screen is a
/// strip of its own. A physical spelling is keyed by the effect's id for the
/// output it resolves to (@p outputPhysicalId), so two spellings of one monitor
/// share one strip; an unresolved one keeps its own id.
inline QString stripKey(const QString& screenId, const QString& outputPhysicalId)
{
    if (PhosphorIdentity::VirtualScreenId::isVirtual(screenId)) {
        return screenId;
    }
    return outputPhysicalId.isEmpty() ? screenId : outputPhysicalId;
}

/// The rect a strip's edges are measured against: a virtual screen's own
/// region when @p defs carries it, else @p outputGeometry (an output not yet
/// subdivided effect-side, or a definition still in flight). @p defs is any
/// range of entries with an `id` and a `geometry`.
template<typename Defs>
QRect screenRect(const QString& screenId, const QRect& outputGeometry, const Defs& defs)
{
    if (PhosphorIdentity::VirtualScreenId::isVirtual(screenId)) {
        for (const auto& def : defs) {
            if (def.id == screenId) {
                return def.geometry;
            }
        }
    }
    return outputGeometry;
}

} // namespace PlasmaZones::StripKeys
