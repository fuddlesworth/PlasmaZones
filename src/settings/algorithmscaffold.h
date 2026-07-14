// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

namespace PlasmaZones {

/// Pure string helpers behind AlgorithmService::createNewAlgorithm.
///
/// Free functions (no Qt object state) so the scaffold and splice logic is
/// unit-testable without standing up the registry / loader / settings stack.
namespace AlgorithmScaffold {

/// Capability flags the New Algorithm wizard can request for a blank scaffold.
/// Template-based creation ignores these — a template's own metadata already
/// declares the capabilities its code depends on.
struct Capabilities
{
    bool masterCount = false;
    bool splitRatio = false;
    bool overlappingZones = false;
    bool memory = false;
    bool scriptState = false;
    bool singleWindow = false;
    bool retileOnFocus = false;
};

/// Strip characters that would let a metadata display string break out of its
/// Luau double-quoted literal, or inject a brace that desyncs the brace-depth
/// scan in spliceTemplate().
QString sanitizeMetadataString(QString value);

/// Build a complete blank-scaffold module: SPDX @p header, a metadata table
/// from @p displayName / @p id / @p caps, and a minimal tile function. The
/// three new capability flags (scriptState, singleWindow, retileOnFocus) are
/// emitted only when set; scriptState additionally emits an onWindowResized
/// stub showing the return contract.
QString buildBlankScaffold(const QString& header, const QString& displayName, const QString& id,
                           const Capabilities& caps);

/// Personalize a bundled `.luau` template: replace its leading SPDX comment
/// block with @p newHeader and rewrite only the top-level `name` / `id`
/// fields of its `metadata = { ... }` table, keeping every other metadata
/// field (capability flags, defaults, customParams) that the template's code
/// depends on. Returns empty when the metadata table can't be located
/// (caller falls back to a blank scaffold). Brace-depth scan; matches the
/// bundled templates' formatting (no `{`/`}` inside metadata strings).
QString spliceTemplate(const QString& templateContent, const QString& newHeader, const QString& displayName,
                       const QString& id);

} // namespace AlgorithmScaffold
} // namespace PlasmaZones
