// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorShaders/phosphorshaders_export.h>

#include <QList>
#include <QString>

namespace PhosphorShaders {

/// One candidate entry function the author may define, paired with the
/// `void main()` the harness generates when that function is present and the
/// source defines no `main()` of its own (T1.4).
///
/// The candidate list is ordered: `composeEntryPoint` appends the
/// `generatedMain` of the FIRST candidate whose `functionName` the source
/// defines. The wrapper text is shader-system-specific (zone packs dispatch a
/// per-zone loop calling `pzZone`; animation packs call `pzTransition` /
/// `pzIn` / `pzOut`), so each system supplies its own candidates while the
/// detection + composition mechanism here stays runtime-agnostic.
struct EntryCandidate
{
    /// Name of the entry function the author defines, e.g. `"pzZone"`.
    QString functionName;

    /// The complete `void main() { ... }` the harness appends when
    /// `functionName` is defined. References to the entry function, plus any
    /// uniforms / helpers it uses (`zoneRects`, `blendOver`, `clampFragColor`,
    /// …), must resolve against what the shader already includes — the wrapper
    /// is appended verbatim after the author's body, so the entry function and
    /// all referenced symbols are already in scope.
    QString generatedMain;

    bool operator==(const EntryCandidate&) const = default;
};

/// True if @p expandedSource defines `void main()` at top level.
///
/// Comments are stripped first so a `main` inside a `//` or block comment never
/// false-matches, and the match requires the `(`…`)` signature followed by `{`
/// so a stray `main` identifier or a forward reference can't trip it. Run on
/// the **include-expanded** source: a `main()` can legitimately arrive from an
/// include, and the harness must treat that exactly like an inline one.
PHOSPHORSHADERS_EXPORT bool definesMain(const QString& expandedSource);

/// True if @p expandedSource defines a function named @p functionName.
///
/// Matches a definition — `<name> ( <params> ) {` — not a call: the trailing
/// `{` distinguishes `vec4 pzZone(ZoneCtx z) { … }` from `return pzZone(z);`.
/// Comments are stripped first. @p functionName is matched as a whole word.
/// Parameter lists spanning multiple lines are handled.
PHOSPHORSHADERS_EXPORT bool definesFunction(const QString& expandedSource, const QString& functionName);

/// Compose the fragment entry point (T1.4).
///
///   • If @p expandedSource already defines `void main()`, it is returned
///     unchanged — the author keeps full control (the escape hatch every
///     existing pack uses today).
///   • Otherwise the first @p candidate whose `functionName` is defined has its
///     `generatedMain` appended (separated by a newline) and the result
///     returned.
///   • If no `main()` and no candidate matches, @p expandedSource is returned
///     unchanged; the compiler then surfaces the missing-`main()` error, which
///     the offline validator (T1.2) maps back to the author's file via the
///     resolver's `#line` legend.
///
/// Splice the T1.1 param preamble (`spliceAfterVersion`) AFTER this, or before
/// — they don't overlap (the preamble sits just after `#version`; the wrapper
/// is appended at the end), but compose on the **include-expanded** source so
/// `definesMain` / `definesFunction` see any entry point arriving from an
/// include.
PHOSPHORSHADERS_EXPORT QString composeEntryPoint(const QString& expandedSource,
                                                 const QList<EntryCandidate>& candidates);

/// Strip GLSL line (`//…`) and block (`/* … */`) comments from @p source,
/// preserving newlines inside block comments so line numbers are unchanged.
/// Exposed for the detection helpers and for tests; GLSL has no string literals
/// so there is no quoting context to preserve.
PHOSPHORSHADERS_EXPORT QString stripGlslComments(const QString& source);

} // namespace PhosphorShaders
