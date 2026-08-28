// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShaders/PixelUnits.h>

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

    for (const QVariant& infoVar : parameterInfos) {
        const QVariantMap info = infoVar.toMap();
        if (info.value(QStringLiteral("unit")).toString() != pixelUnit()) {
            continue;
        }
        const QString id = info.value(QStringLiteral("id")).toString();
        if (id.isEmpty()) {
            continue;
        }

        // The caller's value when there is one, the declared default when
        // there is not — see the header on why an absent px parameter cannot
        // simply be skipped.
        const QVariant source = values.contains(id) ? values.value(id) : info.value(QStringLiteral("default"));
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
        values.insert(id, raw * factor);
    }
}

} // namespace PhosphorShaders
