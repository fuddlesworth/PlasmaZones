// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShaders/PixelUnits.h>

#include <QLatin1String>
#include <QtGlobal>

#include <cmath>

namespace PhosphorShaders {

void scalePixelParams(const QVariantList& parameterInfos, QVariantMap& values, double factor)
{
    // A factor of exactly 1 is the runtime case (the surface IS the real
    // thing), and every unusable value is treated as that rather than as an
    // error: a host mid-layout can hand over a zero width, and flattening
    // every px parameter to zero would blank a whole preview page for a frame.
    if (!std::isfinite(factor) || factor <= 0.0 || qFuzzyCompare(factor, 1.0)) {
        return;
    }

    // Read every source value out of the map as the caller handed it over. The
    // loop writes into `values` as it goes, so a pack that declares the same id
    // twice would otherwise re-read its own scaled output and multiply the
    // factor in a second time. A snapshot makes a repeat write the same result
    // instead of a compounded one, and needs no extra bookkeeping set.
    const QVariantMap sourceValues = values;

    for (const QVariant& infoVar : parameterInfos) {
        const QVariantMap info = infoVar.toMap();
        if (info.value(QStringLiteral("unit")).toString() != pixelUnit()) {
            continue;
        }
        // An image parameter's value is a path, never a length. Beyond being
        // meaningless to scale, the default-insertion below would fabricate the
        // key-presence that ShaderRegistry::translateParamsToUniforms reads as
        // "the user chose this" (`fromUser = storedParams.contains(param.id)`,
        // shaderregistry.cpp:539) to select Trust over Reject for an absolute
        // path. A value transformer must not write a security provenance flag.
        if (info.value(QStringLiteral("type")).toString() == QLatin1String("image")) {
            continue;
        }
        const QString id = info.value(QStringLiteral("id")).toString();
        if (id.isEmpty()) {
            continue;
        }

        // The caller's value when there is one, the declared default when
        // there is not — see the header on why an absent px parameter cannot
        // simply be skipped.
        const QVariant source =
            sourceValues.contains(id) ? sourceValues.value(id) : info.value(QStringLiteral("default"));
        bool numeric = false;
        const double raw = source.toDouble(&numeric);
        if (!numeric) {
            // A px parameter whose value will not read as a number is a pack
            // authoring fault, not something to guess at. Leave whatever the
            // caller had; the parameter pipeline downstream reports its own
            // type faults.
            continue;
        }

        // Scaled values stay real-valued even for an `int` parameter. The
        // uniform is a float on the GPU either way, and rounding a 14px cell
        // scaled by 0.24 up to 3px (or down to 0) would reintroduce, in the
        // small, exactly the mismatch this scaling removes.
        //
        // The result also DELIBERATELY leaves the parameter's declared
        // [min,max]: a preview of a 24px minimum blur at quarter scale has to
        // be 6px to look right. So a scaled map must never be routed through
        // ShaderRegistry's range validator, which substitutes the declared
        // default for an out-of-range value rather than clamping it, and would
        // hand back the full-size number this call exists to replace.
        values.insert(id, raw * factor);
    }
}

} // namespace PhosphorShaders
