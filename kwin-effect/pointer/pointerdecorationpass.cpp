// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// PointerDecorationPass — lifecycle, settings, pointer sampling, damage and
// cursor arbitration. The pack COMPILATION lives in pointerdecorationshader
// .cpp and the DRAW in pointerdecorationpaint.cpp; the class contract and the
// reasoning behind the cost rule, the coordinate space and the cursor
// arbitration live on the header.

#include "pointerdecorationpass.h"

#include "plasmazoneseffect/shader_internal.h"
#include "compositor/effectlogging.h"

#include <core/output.h>
#include <core/rect.h>
#include <effect/effecthandler.h>
#include <effect/globals.h>
#include <opengl/glframebuffer.h>
#include <opengl/glshader.h>
#include <opengl/gltexture.h>

#include <QImage>
#include <QLoggingCategory>
#include <QPoint>
#include <QSizeF>

#include <algorithm>

namespace PlasmaZones {

namespace PPS = PhosphorPointerShaders;

PointerDecorationPass::PointerDecorationPass() = default;

PointerDecorationPass::~PointerDecorationPass()
{
    // The compiled packs own GLShaders, GLTextures and GLFramebuffers whose
    // destruction issues glDelete*; the destructor can run at compositor
    // teardown where no context is current. Same discipline as every other GL
    // owner in the effect.
    releaseGl();
}

// ── Settings ────────────────────────────────────────────────────────────────

void PointerDecorationPass::setProfile(const PhosphorSurfaceShaders::DecorationProfile& profile)
{
    if (m_profile == profile) {
        return;
    }
    m_profile = profile;
    // Parameter VALUES are baked into the compiled pack at first compile and
    // the cache is keyed on pack id alone, so a chain whose ids are unchanged
    // but whose parameters were edited would keep rendering the old values.
    // Drop the compiled cache on every real profile change, exactly as the
    // surface path does for its own pack cache when the same D-Bus reply
    // lands. releaseGl() makes the context current itself and warns when it
    // cannot, which is the right discipline for a call arriving between
    // frames.
    releaseGl();
    // Taken before the rebuild, while the rect still describes what is on
    // screen: damageLogicalRect answers from the CURRENT chain, so once
    // rebuildChain has narrowed the reach or dropped the `above` layer there
    // is no longer anything that describes the band the last frame painted.
    const QRectF stale = staleTrailRect();
    rebuildChain();
    // Unconditional, not just on an emptied chain. A rebuild that keeps the
    // chain engaged can still take the `above` layer away, and the sprite blit
    // is gated on m_anyAboveLayer: leaving the hide in place then means the
    // compositor's cursor stays hidden while nothing here draws a replacement.
    // Every other release path waits for the history to go quiet, so a user who
    // keeps moving the mouse would have no pointer at all until they stopped.
    // The call costs nothing when no hide is held and keeps one that is still
    // wanted, so it is safe on every path.
    updateCursorHiding();
    if (!m_engaged) {
        // Emptying the chain mid-trail must not leave the history to be picked
        // up by a later chain as a stale burst.
        resetHistory();
        m_lastSpriteCanvasRect = QRectF();
        m_hasTimeOrigin = false;
    }
    repaintStale(stale);
}

void PointerDecorationPass::setSuppressedOutputs(const QSet<KWin::LogicalOutput*>& outputs)
{
    if (m_suppressedOutputs == outputs) {
        return;
    }
    m_suppressedOutputs = outputs;
    if (!suppressedOn(m_output)) {
        // Either nothing changed for the pointer's own output, or the gate just
        // LIFTED there. Nothing to tear down: the next pointer event starts a
        // fresh burst, and resurrecting the trail the pointer left behind a
        // fullscreen window would replay motion the user has moved on from.
        return;
    }
    // The gate just closed over the pointer's output. Same tidy-up an emptied
    // chain does in setProfile: hand the cursor back before an `above` layer's
    // hide outlives the last frame that would have drawn the sprite, and drop
    // the history so a later un-suppress does not pick it up as a stale burst.
    //
    // The rect has to be taken before m_suppressedOutputs is consulted again:
    // damageDeviceRect answers empty for a suppressed screen, so after the
    // assignment above there is nothing left that describes the band the last
    // frame painted. Without the repaint the trail stays frozen over a
    // fullscreen window that may never repaint on its own — a paused game is
    // the case that bites.
    const QRectF stale = m_output && KWin::effects
        ? damageLogicalRect(m_output, ShaderInternal::shaderClockNowMs(), /*ignoreSuppression=*/true)
        : QRectF();
    updateCursorHiding();
    resetHistory();
    m_lastSpriteCanvasRect = QRectF();
    m_hasTimeOrigin = false;
    repaintStale(stale);
}

void PointerDecorationPass::rebuildChain()
{
    m_engagedLayers.clear();
    m_anyAboveLayer = false;
    m_maxReachLogical = 0.0;
    m_maxTrailSeconds = 0.0;

    // enabledChain() is effectiveChain() minus the per-layer disable toggles,
    // which is exactly the set of layers the renderer should paint. An empty
    // one is the "off" state: there is no separate master switch.
    const QStringList chain = m_profile.enabledChain();
    if (chain.isEmpty()) {
        m_engaged = false;
        // Kept in step with m_maxTrailSeconds on this path too, so the
        // sampler never carries a window from a chain that no longer exists.
        m_history.setTrailSeconds(m_maxTrailSeconds);
        return;
    }
    // Populating the search paths is what makes the registry scan the pack
    // dirs at all, so it has to happen before the first resolve — but only
    // once the user has actually enabled a chain, so a disabled feature never
    // pays for the scan or the file watcher.
    ensureRegistryPaths();

    const QVariantMap allParameters = m_profile.effectiveParameters();
    m_engagedLayers.reserve(static_cast<size_t>(chain.size()));
    for (const QString& effectId : chain) {
        if (effectId.isEmpty()) {
            continue;
        }
        const PPS::PointerShaderEffect eff = m_registry.effect(effectId);
        if (!eff.isValid()) {
            // Not a per-frame warning: the resolve happens only when the
            // profile or the registry changes.
            qCWarning(lcEffect) << "Pointer pack" << effectId << "is not in the registry — layer skipped";
            continue;
        }
        // The decoration tree keys per-pack overrides by pack id, so a chain
        // that names the same pack twice shares one parameter set — the same
        // contract every other decoration surface runs under.
        const QVariantMap parameters = allParameters.value(effectId).toMap();
        // reach is LOGICAL px, possibly overridden by the pack's reachParam
        // against this layer's parameter overrides. It sets the damage rect,
        // so a pack painting past it is clipped rather than smeared.
        const double reach = eff.resolvedReach(parameters);
        m_maxReachLogical = std::max(m_maxReachLogical, reach);
        m_maxTrailSeconds = std::max(m_maxTrailSeconds, eff.trailSeconds);
        if (eff.layer == PPS::PointerShaderEffect::Layer::Above) {
            m_anyAboveLayer = true;
        }
        m_engagedLayers.push_back(EngagedLayer{effectId, eff, parameters, reach});
    }
    // A chain whose every layer resolved away is NOT engaged: the cost rule
    // is about live layers, not about a non-empty profile.
    m_engaged = !m_engagedLayers.empty() && m_maxTrailSeconds > 0.0;
    // The ring spreads its samples over the longest window in the chain, so a
    // pack's tail can actually be as long as its trailSeconds says. Without
    // this the sampler kept every event and a fast mouse filled all 32 slots
    // in a few tens of ms, whatever the pack's length parameter said.
    m_history.setTrailSeconds(m_maxTrailSeconds);
}

// ── Pointer sampling ────────────────────────────────────────────────────────

void PointerDecorationPass::notePointer(const QPointF& pos, const QPointF& oldPos, Qt::MouseButtons buttons,
                                        Qt::MouseButtons oldButtons)
{
    if (!m_engaged || !KWin::effects) {
        return;
    }
    const bool moved = pos != oldPos;
    const bool buttonsChanged = buttons != oldButtons;
    if (!moved && !buttonsChanged) {
        return;
    }
    KWin::LogicalOutput* const screen = KWin::effects->screenAt(pos.toPoint());
    if (!screen) {
        return;
    }
    const qint64 nowMs = ShaderInternal::shaderClockNowMs();
    if (suppressedOn(screen)) {
        // The fullscreen gate covers this output. Record the canvas so a later
        // un-suppress (or a move to another output) still sees the crossing and
        // resets, but write NO history and request NO repaint for THIS output:
        // not sampling is what makes this cost nothing, where sampling and then
        // drawing nothing would keep the pass in the frame loop. The output
        // the pointer LEFT still gets its last trail damaged (see
        // repaintStaleTrail), and a hide held there is released now rather
        // than on the next scheduleRepaints.
        repaintStaleTrail(screen, nowMs);
        m_output = screen;
        resetHistory();
        m_lastSpriteCanvasRect = QRectF();
        m_hasTimeOrigin = false;
        updateCursorHiding();
        return;
    }
    if (screen != m_output) {
        // A new canvas. The samples in the ring are positions against the old
        // output's origin and scale, so carrying them over would draw the
        // trail at an arbitrary offset on the new output for the length of
        // one trailSeconds window. Start clean, after asking the old output
        // to repaint the trail it still shows.
        repaintStaleTrail(screen, nowMs);
        resetHistory();
        m_lastSpriteCanvasRect = QRectF();
        m_hasTimeOrigin = false;
        m_output = screen;
    }
    const qreal scale = screen->scale();
    const QPointF devicePx = (pos - screen->geometryF().topLeft()) * scale;
    // A buttons-only event (the early return above means !moved implies
    // buttonsChanged) writes no motion sample, so after a reset (an output
    // crossing, an un-suppress) the first live frame would hand packs
    // iMouse == (0,0) while uPointerPress carries the real position. Seed the
    // ring from the event when it is empty, as a position and NOT as motion
    // (a click is not a move, and the preview never records one as such); a
    // ring with samples keeps them.
    if (moved) {
        m_history.notePointer(devicePx, nowMs);
    } else {
        m_history.seedPosition(devicePx, nowMs);
    }
    if (buttonsChanged) {
        m_history.noteButtons(buttons, oldButtons, devicePx, nowMs);
    }
    // Ask for the frame ourselves. A pointer moving over a hardware cursor
    // plane damages nothing, so without this the chain would only tick when
    // something unrelated happened to repaint the screen — the same reason
    // repaintHoverDecorations exists on this signal.
    const QRectF logical = damageLogicalRect(screen, nowMs);
    if (!logical.isEmpty()) {
        KWin::effects->addRepaint(KWin::RectF(logical));
    }
}

void PointerDecorationPass::repaintStaleTrail(KWin::LogicalOutput* next, qint64 nowMs)
{
    // The trail a reset is about to drop was painted on the pointer's
    // previous output, and nothing else damages it there: paintOutput only
    // draws for m_output, scheduleRepaints only asks for m_output, and a
    // hardware-plane cursor leaving the output damages nothing. Without this
    // the last frame's trail (and, for an `above` chain, its sprite copy)
    // stayed frozen at the edge of the previous monitor on every crossing,
    // until something else happened to repaint that region.
    if (!m_output || m_output == next || !KWin::effects) {
        return;
    }
    const QRectF stale = damageLogicalRect(m_output, nowMs);
    if (!stale.isEmpty()) {
        KWin::effects->addRepaint(KWin::RectF(stale));
    }
}

bool PointerDecorationPass::isLive() const
{
    // Suppressed counts as not live, so the effect is not held in the paint
    // chain on our account while the pointer sits over a fullscreen window.
    if (!m_engaged || suppressedOn(m_output)) {
        return false;
    }
    return m_history.isLive(ShaderInternal::shaderClockNowMs(), m_maxTrailSeconds);
}

// ── Damage ──────────────────────────────────────────────────────────────────

QRectF PointerDecorationPass::damageDeviceRect(KWin::LogicalOutput* screen, qint64 nowMs, bool ignoreSuppression)
{
    // Suppressed here too, not only in the callers: this rect is both the
    // repaint REQUEST and the draw quad, so an empty one is what actually
    // guarantees a covered output is neither asked for a frame nor painted,
    // however a future caller reaches it.
    //
    // ignoreSuppression is for the one caller that needs the rect of what is
    // ALREADY on screen at the moment suppression closes over the output: the
    // gate has been applied by then, so the honest answer is empty, and that
    // is precisely the band still needing a repaint.
    if (!m_engaged || !screen || (!ignoreSuppression && suppressedOn(screen))) {
        return {};
    }
    const qreal scale = screen->scale();
    // reach is declared in logical px (the unit a user types in the settings
    // app); the history and the canvas are device px.
    QRectF rect = m_history.damageRect(m_maxReachLogical * scale, nowMs, m_maxTrailSeconds);
    if (rect.isEmpty()) {
        return {};
    }
    if (m_anyAboveLayer) {
        // An `above` chain hides KWin's cursor and re-draws the sprite
        // itself, so the sprite's own rect has to be inside the damage or the
        // pointer would leave a hole wherever it drifts past the reach band.
        // The PREVIOUS rect is unioned in too: a cursor shape change arrives
        // with no pointer event, so a sprite that shrank (an arrow after a
        // resize cursor) would otherwise leave its old band unrepainted for
        // a frame, still showing the stale sprite this pass drew there.
        const QRectF sprite = cursorCanvasRect(screen);
        if (!sprite.isEmpty()) {
            rect = rect.united(sprite);
        }
        if (!m_lastSpriteCanvasRect.isEmpty()) {
            rect = rect.united(m_lastSpriteCanvasRect);
        }
        m_lastSpriteCanvasRect = sprite;
    }
    const QSizeF deviceSize = screen->geometryF().size() * scale;
    return rect.intersected(QRectF(QPointF(0.0, 0.0), deviceSize));
}

QRectF PointerDecorationPass::damageLogicalRect(KWin::LogicalOutput* screen, qint64 nowMs, bool ignoreSuppression)
{
    const QRectF device = damageDeviceRect(screen, nowMs, ignoreSuppression);
    if (device.isEmpty()) {
        return {};
    }
    const qreal scale = screen->scale();
    const QRectF outputGeo = screen->geometryF();
    const QRectF local(device.x() / scale, device.y() / scale, device.width() / scale, device.height() / scale);
    // Grown to whole logical pixels AFTER the translate: a fractional-scale
    // device rect maps to a fractional logical one, and a repaint region that
    // stops mid-pixel leaves the pack's outermost row unrefreshed. Aligning
    // before the translate would round the output origin too, which on a
    // fractionally-positioned output shifts the whole rect.
    return QRectF(local.translated(outputGeo.topLeft()).toAlignedRect());
}

QRectF PointerDecorationPass::cursorCanvasRect(KWin::LogicalOutput* screen) const
{
    if (!screen || !KWin::effects) {
        return {};
    }
    const KWin::PlatformCursorImage cursor = KWin::effects->cursorImage();
    if (cursor.isNull()) {
        return {};
    }
    const QImage img = cursor.image();
    const qreal dpr = img.devicePixelRatio() > 0.0 ? img.devicePixelRatio() : 1.0;
    // The hotspot is expressed in the sprite image's own device pixels, so it
    // divides by the image's dpr to reach logical px — not by the OUTPUT's
    // scale, which need not match the cursor theme's size.
    const QPointF logicalTopLeft = KWin::effects->cursorPos() - cursor.hotSpot() / dpr;
    const qreal scale = screen->scale();
    const QPointF canvasTopLeft = (logicalTopLeft - screen->geometryF().topLeft()) * scale;
    const QSizeF canvasSize(img.width() / dpr * scale, img.height() / dpr * scale);
    return QRectF(canvasTopLeft, canvasSize);
}

void PointerDecorationPass::scheduleRepaints()
{
    if (!m_engaged || !m_output || suppressedOn(m_output) || !KWin::effects) {
        // Nothing engaged, or the fullscreen gate covers the pointer's output:
        // no repaint, no clock read, no allocation. Requesting a frame and
        // drawing nothing is the one thing suppression must NOT do, since the
        // per-frame wake-up is the cost the setting exists to remove. The
        // cursor hide cannot be outstanding either — every path that
        // disengages releases it — but a disengage that raced a frame is
        // covered by the release below.
        if (m_cursorHidden) {
            updateCursorHiding();
        }
        return;
    }
    const qint64 nowMs = ShaderInternal::shaderClockNowMs();
    if (!m_history.isLive(nowMs, m_maxTrailSeconds)) {
        // Gone quiet. Drop the iTime origin so the next burst starts at zero,
        // and hand the cursor back — this is the path that covers a pointer
        // output which stopped painting entirely, where paintOutput's own
        // release never runs.
        m_hasTimeOrigin = false;
        updateCursorHiding();
        return;
    }
    const QRectF logical = damageLogicalRect(m_output, nowMs);
    if (!logical.isEmpty()) {
        KWin::effects->addRepaint(KWin::RectF(logical));
    }
}

// ── Cursor arbitration ──────────────────────────────────────────────────────

bool PointerDecorationPass::cursorOnOutput(KWin::LogicalOutput* screen) const
{
    // The same exclusive rule notePointer keys the canvas on. QRectF::contains
    // includes the right and bottom edges, so a pointer on a shared boundary
    // would read as on BOTH outputs and the hide could be taken for one the
    // history is not on. The strip pass resolves the same way.
    return screen && KWin::effects && KWin::effects->screenAt(KWin::effects->cursorPos().toPoint()) == screen;
}

bool PointerDecorationPass::hideCursorForPass(KWin::LogicalOutput* screen)
{
    if (m_cursorHidden || !m_anyAboveLayer || suppressedOn(screen) || !KWin::effects || !cursorOnOutput(screen)) {
        return false;
    }
    // Another owner (the strip pass, KWin's zoom, a screen-edge peek) already
    // holds the hidden state and draws its own copy; taking a second hide
    // would leave the show/hide pair unbalanced and drawing the cursor twice.
    if (KWin::effects->isCursorHidden()) {
        return false;
    }
    KWin::effects->hideCursor();
    m_cursorHidden = true;
    return true;
}

void PointerDecorationPass::updateCursorHiding()
{
    if (!m_cursorHidden) {
        return;
    }
    const bool stillLive = m_engaged && m_anyAboveLayer && m_output && !suppressedOn(m_output)
        && cursorOnOutput(m_output) && m_history.isLive(ShaderInternal::shaderClockNowMs(), m_maxTrailSeconds);
    if (stillLive) {
        return;
    }
    if (KWin::effects) {
        KWin::effects->showCursor();
    }
    m_cursorHidden = false;
}

void PointerDecorationPass::releaseCursorHideForForeignPaint(KWin::LogicalOutput* screen)
{
    // Unconditional for THIS output, unlike updateCursorHiding: a live chain
    // on it does not keep the hide, because the caller is about to paint the
    // output through another pass and nothing else would draw the cursor.
    if (!m_cursorHidden || !cursorOnOutput(screen)) {
        return;
    }
    if (KWin::effects) {
        KWin::effects->showCursor();
    }
    m_cursorHidden = false;
}

// ── Teardown ────────────────────────────────────────────────────────────────

bool PointerDecorationPass::ensureGlContextCurrent()
{
    return KWin::effects && KWin::effects->makeOpenGLContextCurrent();
}

void PointerDecorationPass::releaseGl()
{
    if (m_packCache.empty() && !m_cursorSprite) {
        return;
    }
    // The result is CAPTURED rather than discarded: the only false case is
    // compositor teardown, where GL is going away and the driver reclaims the
    // objects whatever we do, so the clear is safe either way — but a guard
    // whose answer is thrown away is not a guard.
    if (!ensureGlContextCurrent()) {
        qCWarning(lcEffect) << "Pointer pack cache released without a current GL context (compositor teardown?)";
    }
    m_packCache.clear();
    m_cursorSprite.reset();
    m_cursorSpriteKey = 0;
}

void PointerDecorationPass::resetHistory()
{
    m_history.reset();
    m_bufferFeedbackStale = true;
}

QRectF PointerDecorationPass::staleTrailRect()
{
    return m_output && KWin::effects ? damageLogicalRect(m_output, ShaderInternal::shaderClockNowMs()) : QRectF();
}

void PointerDecorationPass::repaintStale(const QRectF& stale) const
{
    if (stale.isEmpty() || !KWin::effects) {
        return;
    }
    KWin::effects->addRepaint(KWin::RectF(stale));
}

void PointerDecorationPass::invalidateShaderCache()
{
    releaseGl();
    // The trail on screen right now, taken BEFORE the rebuild: damageLogicalRect
    // answers from the CURRENT chain, and the caller deliberately requests no
    // repaint of its own.
    const QRectF stale = staleTrailRect();
    // A reload can add the pack a chain names, or remove one it resolved to,
    // and it can change reach / trailSeconds / layer — all of which feed the
    // engaged-chain cache, not just the compiled shaders.
    rebuildChain();
    // Unconditional, for the reason setProfile gives: a rebuild can drop the
    // `above` layer without disengaging the chain.
    updateCursorHiding();
    if (!m_engaged) {
        // The same tidy-up setProfile does when a chain empties: a reload that
        // removes the chain's pack and a later one that restores it must not
        // replay the trail the pointer left in between.
        resetHistory();
        m_lastSpriteCanvasRect = QRectF();
        m_hasTimeOrigin = false;
    }
    // Also unconditional. A rebuild that keeps the chain engaged but SHRINKS
    // the reach, or drops the only `above` layer, leaves the band between the
    // old extent and the new one undamaged: every later frame asks only for
    // the new, smaller rect, so the outer ring of the last frame painted at
    // the old reach stays on screen. This is one addRepaint on a settings or
    // reload event, not per frame.
    repaintStale(stale);
}

void PointerDecorationPass::outputGeometryChanged()
{
    if (!m_output) {
        return;
    }
    // Only the samples. The output is still ours and a live chain stays live —
    // what is stale is the canvas the ring was measured in, so the trail
    // restarts from the pointer's next position instead of streaking across
    // the rescaled screen. The sprite rect is in the old scale's device px,
    // so it goes too.
    //
    // Taken first, for the same reason as the other reset paths: from the next
    // frame on the rect answers from an empty ring, so the band the last frame
    // painted has nothing left to describe it. A resolution change usually
    // forces a full output repaint, but a virtual-layout change does not
    // necessarily re-render the output's contents.
    const QRectF stale = staleTrailRect();
    resetHistory();
    m_lastSpriteCanvasRect = QRectF();
    m_hasTimeOrigin = false;
    repaintStale(stale);
}

void PointerDecorationPass::outputRemoved(KWin::LogicalOutput* screen)
{
    if (m_output != screen) {
        return;
    }
    // A dangling LogicalOutput* here would be dereferenced by scheduleRepaints
    // and by the damage math; the history is keyed to this output's canvas
    // and means nothing without it.
    m_output = nullptr;
    resetHistory();
    m_lastSpriteCanvasRect = QRectF();
    m_hasTimeOrigin = false;
    // The pointer is about to land somewhere else (or nowhere); a hide taken
    // for the dying output has no pass left to draw the cursor for it.
    if (m_cursorHidden) {
        if (KWin::effects) {
            KWin::effects->showCursor();
        }
        m_cursorHidden = false;
    }
    // The buffer targets are sized to the removed output's device size; the
    // next live frame reallocates them against whatever output the pointer
    // lands on. Freed here rather than carried, so a hotplug cycle does not
    // hold a full-size FBO pair per pack for a pointer that never returns.
    // screenRemoved arrives from KWin's signal, not from the paint thread, so
    // the glDelete* the resets below issue need the context made current —
    // same discipline as releaseGl().
    if (!m_packCache.empty() && !ensureGlContextCurrent()) {
        qCWarning(lcEffect) << "Pointer buffer targets freed without a current GL context (compositor teardown?)";
    }
    for (auto& entry : m_packCache) {
        entry.second.bufferTex.clear();
        entry.second.bufferFbo.clear();
        entry.second.bufferSize = QSize();
    }
}

void PointerDecorationPass::reset()
{
    if (m_cursorHidden) {
        if (KWin::effects) {
            KWin::effects->showCursor();
        }
        m_cursorHidden = false;
    }
    releaseGl();
    resetHistory();
    m_lastSpriteCanvasRect = QRectF();
    m_output = nullptr;
    m_hasTimeOrigin = false;
}

} // namespace PlasmaZones
