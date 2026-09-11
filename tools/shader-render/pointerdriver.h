// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorPointer/PointerHistory.h>
#include <PhosphorPointer/PointerShaderEffect.h>
#include <PhosphorPointer/PointerUniformExtension.h>

#include <QImage>
#include <QPointF>
#include <QSize>
#include <QString>

namespace PhosphorRendering {
class ShaderEffect;
}

namespace PlasmaZones::ShaderRender {

/**
 * @brief Parse a pointer pack and install it on @p effect the way the settings
 *        preview does.
 *
 * A pointer pack is NOT assembled like an overlay pack: its entry point is
 * `pPointer(vec2)`, its helpers come from its own `shared/` tree, and its
 * parameter slots are allocated by `PointerShaderRegistry`, whose allocation the
 * matching `p_<id>` preamble mirrors. Installing the zone scaffold instead
 * produces a fragment with no `main()` that calls the pack, which links and then
 * paints nothing — an empty render that looks exactly like a pack correctly
 * drawing nothing on a frame with no click.
 *
 * Returns false when the pack cannot be parsed, in which case @p effect is left
 * untouched. On success @p parsedOut carries the parsed pack, which is what the
 * caller hands to PointerDriver::applyPackContract so the two agree about the
 * same parse rather than reading metadata.json twice.
 */
bool installPointerPack(PhosphorRendering::ShaderEffect& effect, const QString& metadataPath,
                        PhosphorPointerShaders::PointerShaderEffect& parsedOut);

/**
 * @brief How the synthetic pointer behaves for a `--pointer` render.
 *
 * Every length is LOGICAL px and every time is seconds from the first frame,
 * which is what the CLI takes, so the numbers a caller passes are the numbers
 * the pack's own parameters are expressed in.
 */
struct PointerDriveOptions
{
    bool enabled = false;

    /// Direction of travel, degrees clockwise from screen right (canvas y runs
    /// down, so 90 is downward). The path is a straight line through the canvas
    /// centre along this heading, so comparing a pack at two headings is a
    /// one-flag change. That comparison is how a pack that claims to key off the
    /// direction of travel is checked: render it at 0 and at 135, and the lit
    /// pixels must differ materially rather than merely rotating a symmetric
    /// shape.
    double headingDegrees = 0.0;

    /// Travel speed along that line, logical px per second.
    double speedPxPerSec = 900.0;

    /// Hold the pointer still at the canvas centre instead of sweeping, which is
    /// how a pack is checked with no recent motion for it to read. A pack that
    /// derives anything from the stroke takes its fallback path here, and a
    /// hold-driven pack can be exercised without the pointer drifting.
    bool still = false;

    /// Seconds at which the button goes down, and comes back up. A negative
    /// `releaseAt` leaves the button DOWN for the rest of the render, which is
    /// how a hold-reading pack's window behaviour is exercised. A negative
    /// `pressAt` means no click at all.
    double pressAt = 0.4;
    double releaseAt = 0.55;

    /// 1 left, 2 right, 3 middle, matching the contract's button codes.
    int button = 1;

    /// Bind a synthetic arrow as `uCursorSprite` and set `uPointerFlags.x`.
    /// A `needsCursor` pack samples the sprite, so without this it can only
    /// show its no-sprite fallback.
    bool cursorSprite = true;

    /// Drawn size of that arrow, logical px.
    double cursorSize = 24.0;
};

/**
 * @brief Drives `PointerHistory` + `PointerUniformExtension` for a headless
 *        pointer-pack render.
 *
 * The same pair the compositor and the settings preview use, fed a synthetic
 * event stream instead of a real one. Events are pushed at a mouse-like rate
 * rather than one per frame, for the reason PointerPreviewController documents:
 * the sampler decides where a slot lands from the gap since the last event, so
 * one event per frame would round the ring's spacing up to the frame interval
 * and the trail would not match what ships.
 *
 * The pack's own `reach`, trail window and `needsCursor` come from the parsed
 * pack through `applyPackContract`, so a render cannot silently disagree with
 * the pack about the damage radius the shader reads back through
 * `pointerReach()`.
 */
class PointerDriver
{
public:
    /// @p canvasDevicePx is the render target size and @p scale the
    /// logical-to-device ratio the pack will see through `pointerScale()`.
    PointerDriver(const PointerDriveOptions& options, const QSize& canvasDevicePx, double scale);

    /// Take the pack's reach, trail window and `needsCursor` from @p effect.
    ///
    /// Resolution goes through the pack's own `resolvedReach` /
    /// `resolvedTrailWindow`, which is what the compositor and the settings
    /// preview call, so the render cannot disagree with either about the damage
    /// radius the shader reads back through `pointerReach()` or about how far
    /// back the ring is spaced. Re-deriving those here from the raw JSON is how
    /// the two drift: the library caps the reach and clamps the window to the
    /// pack's liveness, and a hand-rolled copy of that arithmetic silently does
    /// not. An empty parameter map means every parameter takes its declared
    /// default, which is exactly what installPointerPack seeds the shader with.
    void applyPackContract(const PhosphorPointerShaders::PointerShaderEffect& effect);

    /// True when the pack asked for the cursor sprite. A caller binds the
    /// sprite image only then, to keep `uPointerFlags.x` honest.
    [[nodiscard]] bool needsCursor() const
    {
        return m_needsCursor;
    }

    /// The synthetic cursor sprite, premultiplied like the real one arrives.
    /// Null when `cursorSprite` is off.
    [[nodiscard]] QImage cursorSprite() const;

    /// Advance the synthetic pointer to frame @p frame and push the resulting
    /// frame state into @p ext. @p iMouseLogical receives the pointer position
    /// in LOGICAL px, which is what `setIMouse` takes.
    void applyFrame(int frame, double fps, PhosphorPointerShaders::PointerUniformExtension& ext,
                    QPointF& iMouseLogical);

private:
    /// Pointer position in DEVICE px at @p seconds, in the contract's TOP-DOWN
    /// space, which is the space every uniform this driver pushes is expressed
    /// in. The render target's y runs the other way, and renderer.cpp corrects
    /// that by flipping the finished frame rather than the uniforms going in;
    /// the note there says why.
    [[nodiscard]] QPointF positionAt(double seconds) const;

    PointerDriveOptions m_opts;
    QSize m_canvas;
    double m_scale = 1.0;
    double m_reachLogicalPx = 64.0;
    bool m_needsCursor = false;

    PhosphorPointerShaders::PointerHistory m_history;
    double m_lastSeconds = 0.0;
    bool m_pressed = false;
    bool m_seeded = false;
};

} // namespace PlasmaZones::ShaderRender
