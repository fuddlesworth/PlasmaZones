// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <PhosphorTheme/phosphortheme_export.h>

#include <QString>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

namespace PhosphorTheme {

// Minimal mustache-style template engine for the matugen fan-out pipeline.
//
// Templates contain `{{token}}` placeholders that resolve to color values
// from a `token name -> QColor` map. A dot-suffix selects a representation:
//
//   {{primary}}            -> "#RRGGBB"        (default = hex form)
//   {{primary.hex}}        -> "#RRGGBB"
//   {{primary.hexa}}       -> "#RRGGBBAA"
//   {{primary.r}}          -> integer 0-255    (also: .red)
//   {{primary.g}}          -> integer 0-255    (also: .green)
//   {{primary.b}}          -> integer 0-255    (also: .blue)
//   {{primary.alpha}}      -> "1.0" / "0.50" / etc (3 decimal places)
//   {{primary.rgb}}        -> "R, G, B"
//   {{primary.rgba}}       -> "R, G, B, A.AAA"
//
// Unknown tokens are left untouched as `{{token}}` in the output and emit
// a warning on stderr, silent omission would hide rename mistakes.
//
// Unknown FIELDS behave differently. A typo'd suffix on a known token,
// say `{{primary.hax}}`, degrades to the default hex form and logs a
// warning. The asymmetry is intentional. A missing color is a structural
// failure that needs to surface visibly in the rendered file. A typo'd
// field on a valid color is recoverable with a sensible fallback. The
// fallback also avoids leaking a literal `{{primary.hax}}` into config
// files that downstream parsers like CSS or GTK treat as a hard error.
//
// This is NOT full mustache: no sections, partials, or escape handling.
// Phosphor fan-out templates do simple substitution; sections / loops
// would complicate the renderer and the templates without buying clarity.
// If a downstream template needs real mustache, render it with the
// external `mustache` CLI from a different stage of the pipeline.
class PHOSPHORTHEME_EXPORT TemplateEngine
{
    Q_GADGET
    QML_VALUE_TYPE(templateEngine)

public:
    // Render `templateSource` against `tokens`. Returns the substituted
    // string. Missing tokens log a warning and leave the placeholder
    // intact.
    [[nodiscard]] static QString render(const QString& templateSource, const QVariantMap& tokens);

    // Convenience: read template from disk, render, write to outPath.
    // Returns true on success, false on any IO failure (rendering never
    // fails). Logs a warning on either failure path.
    static bool renderFile(const QString& templatePath, const QString& outPath, const QVariantMap& tokens);
};

} // namespace PhosphorTheme
