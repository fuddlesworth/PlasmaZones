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
/// Luau double-quoted literal (newlines and quotes are replaced, backslashes
/// become slashes). Braces are fine: rewriteMetadataNameId()'s depth scan
/// ignores braces inside quoted strings.
QString sanitizeMetadataString(QString value);

/// Rewrite the top-level `name` / `id` fields of a `.luau` algorithm's
/// `metadata = { ... }` table, leaving the rest of the file — header,
/// comments, other metadata fields, code — exactly as it was. Line endings
/// are normalized to LF.
///
/// Returns empty on an unrecognized shape: no metadata table, an opening line
/// that does not end at the `{`, an unterminated table, a brace depth that
/// goes negative, or a name/id field that is not a whole `field = "value",`
/// line. Only depth-1 fields are touched, so nested `customParams` entries
/// keep their own `name` keys. The brace-depth scan ignores braces inside
/// quoted strings and after `--` comments; Luau long strings/comments
/// (`[[...]]`) and multi-line short strings inside metadata are not
/// supported. @p displayName must already be sanitizeMetadataString()'d — it
/// is embedded in a Luau string literal without further escaping.
QString rewriteMetadataNameId(const QString& content, const QString& displayName, const QString& id);

/// Build a complete blank-scaffold module: SPDX @p header (which must end
/// with a newline), a metadata table
/// from @p displayName / @p id / @p caps, and a minimal tile function. The
/// three new capability flags (scriptState, singleWindow, retileOnFocus) are
/// emitted only when set; scriptState additionally emits an onWindowResized
/// stub showing the return contract. @p displayName must already be
/// sanitizeMetadataString()'d — it is embedded in a Luau string literal
/// without further escaping.
QString buildBlankScaffold(const QString& header, const QString& displayName, const QString& id,
                           const Capabilities& caps);

/// Personalize a bundled `.luau` template: rewriteMetadataNameId() plus a
/// fresh header. Every other metadata field (capability flags, defaults,
/// customParams) the template's code depends on is kept.
///
/// The copy is a derivative work of the template, so its header is @p
/// newCopyrightLine (exactly one `-- SPDX-FileCopyrightText:` line, with or
/// without a trailing newline) followed by the template's own SPDX lines: its
/// copyright, then its license, which the copy inherits rather than being
/// restamped. A template already carrying @p newCopyrightLine verbatim (a
/// copy of a copy) does not repeat it, and a template with no license line
/// yields a copy with none — this function never invents a license for
/// someone else's code. Other leading comments, e.g. a template's doc block,
/// carry over verbatim.
///
/// Returns empty when @p newCopyrightLine is multi-line, and on every shape
/// rewriteMetadataNameId() rejects.
QString spliceTemplate(const QString& templateContent, const QString& newCopyrightLine, const QString& displayName,
                       const QString& id);

} // namespace AlgorithmScaffold
} // namespace PlasmaZones
