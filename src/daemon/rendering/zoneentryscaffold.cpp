// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "zoneentryscaffold.h"

namespace PlasmaZones {

QString zoneEntryPrologue()
{
    // `#version` first (GLSL requires it); the shared include declares ZoneCtx,
    // the zone UBO, and the helpers (blendOver / clampFragColor) the generated
    // main() relies on; then the fragment ins/out the per-pack main() used to
    // declare by hand. zone.vert outputs BOTH vTexCoord (location 0, 0..1) and
    // vFragCoord (location 1, screen pixels); declare both so a pack that reads
    // either still compiles (an unused in matched to a vertex output is fine).
    return QStringLiteral(
        "#version 450\n"
        "#include <common.glsl>\n"
        "layout(location = 0) in vec2 vTexCoord;\n"
        "layout(location = 1) in vec2 vFragCoord;\n"
        "layout(location = 0) out vec4 fragColor;\n");
}

QList<PhosphorShaders::EntryCandidate> zoneEntryCandidates()
{
    // pZone: the per-zone dispatch the 26 packs write by hand today — the
    // zoneCount guard, the bounded loop, the degenerate-rect skip, the
    // blendOver accumulate, and the final clamp — all generated once here.
    // `p_`-prefixed locals avoid colliding with author identifiers.
    static const QString zoneMain = QStringLiteral(
        "void main() {\n"
        "    if (zoneCount == 0) { fragColor = vec4(0.0); return; }\n"
        "    vec4 p_accum = vec4(0.0);\n"
        "    for (int p_i = 0; p_i < zoneCount && p_i < 64; p_i++) {\n"
        "        vec4 p_rect = zoneRects[p_i];\n"
        "        if (p_rect.z <= 0.0 || p_rect.w <= 0.0) continue;\n"
        "        ZoneCtx p_z;\n"
        "        p_z.index = p_i;\n"
        "        p_z.fragCoord = vFragCoord;\n"
        "        p_z.rect = p_rect;\n"
        "        p_z.fillColor = zoneFillColors[p_i];\n"
        "        p_z.borderColor = zoneBorderColors[p_i];\n"
        "        p_z.params = zoneParams[p_i];\n"
        "        p_z.isHighlighted = zoneParams[p_i].z > 0.5;\n"
        "        p_accum = blendOver(p_accum, pZone(p_z));\n"
        "    }\n"
        "    fragColor = clampFragColor(p_accum);\n"
        "}\n");

    // pImage: full-frame entry — the trivial main() a multipass buffer pass or
    // a wallpaper/full-screen effect needs, with the central clamp applied.
    static const QString imageMain = QStringLiteral(
        "void main() {\n"
        "    fragColor = clampFragColor(pImage(vFragCoord));\n"
        "}\n");

    return {PhosphorShaders::EntryCandidate{QStringLiteral("pZone"), zoneMain},
            PhosphorShaders::EntryCandidate{QStringLiteral("pImage"), imageMain}};
}

QString assembleZoneEntrySource(const QString& rawFragmentSource)
{
    // Delegate to the shared library assembler so the zone path can't drift from
    // the rendering/kwin-effect paths: a pack that ships its own main() passes
    // through byte-for-byte; an entry-only pack gets the prologue + the generated
    // main() for whichever candidate (pZone/pImage) its body defines. The zone
    // candidate list is never empty, so this is exactly the old open-coded form.
    return PhosphorShaders::assembleEntryPoint(rawFragmentSource, zoneEntryPrologue(), zoneEntryCandidates());
}

} // namespace PlasmaZones
