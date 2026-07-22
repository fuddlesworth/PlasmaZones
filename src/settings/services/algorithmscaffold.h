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
/// A top-level name/id field is rewritten when it is the whole line. The value
/// may use either quote style and may contain escaped quotes (`"Bob\"s Grid"`),
/// and the line may close with either of Luau's field separators (`,` or `;`)
/// and carry a trailing `--` comment; the separator and comment are preserved,
/// and only the value changes.
///
/// Returns empty on an unrecognized shape: no metadata table, an opening line
/// that does not end at the `{`, an unterminated table, a brace depth that goes
/// negative, a bracketed `name` / `id` key (`["name"] = ...` is the same Luau
/// key as the bare one, and cannot be rewritten in place), a bracketed key this
/// cannot read back as a plain string literal and so cannot prove is not one
/// (`["na" .. "me"]`, `[someVar]`), or a top-level name/id field
/// that is not a whole line of that form. That last one covers a field sharing
/// its line with another (in either
/// order), one trailing a long bracket's closer, one whose key and `=` fall on
/// different lines, one whose value is a long-bracket string (`name = [[x]]`,
/// which may span lines), and one whose value spans lines or is followed by a
/// long-bracket comment (`--[[`, `--[=[`, ...), which can close mid-line and
/// leave a second field somewhere a line-anchored read will not look. None can
/// be rewritten in place, and leaving one would let Luau's last-wins keep the
/// template's value.
///
/// A bracketed key that IS provably something else (`["description"] = ...`) is
/// left alone and does not refuse the file.
///
/// The table's closer ends the read, wherever on its line it falls: a line that
/// closes it and opens a sibling (`}, extra = {`) stops here rather than
/// carrying on into the sibling's fields.
///
/// A `metadata = {` inside a long comment is that comment's text, not the
/// table, and the search reads it as such rather than rewriting it and leaving
/// the real table below.
///
/// Only the table's own top-level fields are read, so a nested `customParams`
/// entry keeps its own `name` key whether it is written inline or across lines.
///
/// The brace-depth scan counts a brace only where it is code. All three of
/// Luau's string forms are text, as are `--` line comments and long brackets at
/// any level (`[[`, `[=[`, ..., as long strings or, spelled `--[[`, long
/// comments). Comments and long brackets may span lines. A short or
/// interpolated string may not: one still open at the end of a line runs past
/// where this reads, so the file is refused rather than read on as if the
/// literal had closed. That covers a backslash-newline or `\z` continuation and
/// a backtick literal whose interpolation spans lines.
///
/// @p displayName must already be sanitizeMetadataString()'d and
/// @p id must be a bare `[A-Za-z0-9_-]` basename — both are embedded in Luau
/// string literals without further escaping.
QString rewriteMetadataNameId(const QString& content, const QString& displayName, const QString& id);

/// Build a complete blank-scaffold module: SPDX @p header (surrounding blank
/// lines are normalized, so a trailing newline is optional), a metadata table
/// from @p displayName / @p id / @p caps, and a minimal tile function. The
/// three new capability flags (scriptState, singleWindow, retileOnFocus) are
/// emitted only when set. Either scriptState or splitRatio additionally emits
/// an onWindowResized stub documenting the half of the return contract that
/// capability reaches: splitRatio for the reserved control key, scriptState
/// for the persistent bag. @p displayName must already be
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
/// Returns empty when @p newCopyrightLine is multi-line or is not a
/// `-- SPDX-FileCopyrightText:` line, and on every shape
/// rewriteMetadataNameId() rejects.
QString spliceTemplate(const QString& templateContent, const QString& newCopyrightLine, const QString& displayName,
                       const QString& id);

} // namespace AlgorithmScaffold
} // namespace PlasmaZones
