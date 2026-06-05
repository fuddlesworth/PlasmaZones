// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "zoneentryscaffold.h"

namespace PlasmaZones {

QString zoneEntryPrologue()
{
    // `#version` first (GLSL requires it); the shared include declares ZoneCtx,
    // the zone UBO, and the helpers (blendOver / clampFragColor) the generated
    // main() relies on; then the fragment in/out the per-pack main() used to
    // declare by hand. vFragCoord is fed by zone.vert (location 1).
    return QStringLiteral(
        "#version 450\n"
        "#include <common.glsl>\n"
        "layout(location = 1) in vec2 vFragCoord;\n"
        "layout(location = 0) out vec4 fragColor;\n");
}

QList<PhosphorShaders::EntryCandidate> zoneEntryCandidates()
{
    // pzZone: the per-zone dispatch the 26 packs write by hand today — the
    // zoneCount guard, the bounded loop, the degenerate-rect skip, the
    // blendOver accumulate, and the final clamp — all generated once here.
    // `pz_`-prefixed locals avoid colliding with author identifiers.
    static const QString zoneMain = QStringLiteral(
        "void main() {\n"
        "    if (zoneCount == 0) { fragColor = vec4(0.0); return; }\n"
        "    vec4 pz_accum = vec4(0.0);\n"
        "    for (int pz_i = 0; pz_i < zoneCount && pz_i < 64; pz_i++) {\n"
        "        vec4 pz_rect = zoneRects[pz_i];\n"
        "        if (pz_rect.z <= 0.0 || pz_rect.w <= 0.0) continue;\n"
        "        ZoneCtx pz_z;\n"
        "        pz_z.index = pz_i;\n"
        "        pz_z.fragCoord = vFragCoord;\n"
        "        pz_z.rect = pz_rect;\n"
        "        pz_z.fillColor = zoneFillColors[pz_i];\n"
        "        pz_z.borderColor = zoneBorderColors[pz_i];\n"
        "        pz_z.params = zoneParams[pz_i];\n"
        "        pz_z.isHighlighted = zoneParams[pz_i].z > 0.5;\n"
        "        pz_accum = blendOver(pz_accum, pzZone(pz_z));\n"
        "    }\n"
        "    fragColor = clampFragColor(pz_accum);\n"
        "}\n");

    // pzImage: full-frame entry — the trivial main() a multipass buffer pass or
    // a wallpaper/full-screen effect needs, with the central clamp applied.
    static const QString imageMain = QStringLiteral(
        "void main() {\n"
        "    fragColor = clampFragColor(pzImage(vFragCoord));\n"
        "}\n");

    return {PhosphorShaders::EntryCandidate{QStringLiteral("pzZone"), zoneMain},
            PhosphorShaders::EntryCandidate{QStringLiteral("pzImage"), imageMain}};
}

QString assembleZoneEntrySource(const QString& rawFragmentSource)
{
    // A pack that ships its own main() is authored the traditional way (it
    // already carries #version, includes, and in/out) — leave it byte-for-byte.
    if (PhosphorShaders::definesMain(rawFragmentSource)) {
        return rawFragmentSource;
    }
    // Entry-only: supply the scaffold, then let composeEntryPoint append the
    // main() for whichever entry function the body defines (or pass through to
    // a clean missing-main() error if none).
    return PhosphorShaders::composeEntryPoint(zoneEntryPrologue() + rawFragmentSource, zoneEntryCandidates());
}

} // namespace PlasmaZones
