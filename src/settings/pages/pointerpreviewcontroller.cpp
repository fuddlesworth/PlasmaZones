// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "pointerpreviewcontroller.h"

#include "pointer_controller_detail.h"

#include <PhosphorPointer/PointerFrameState.h>
#include <PhosphorPointer/PointerShaderEffect.h>
#include <PhosphorPointer/PointerShaderRegistry.h>
#include <PhosphorPointer/PointerUniformExtension.h>
#include <PhosphorRendering/ShaderEffect.h>
#include <PhosphorShaders/ShaderRegistry.h>

#include <QPointF>
#include <QRectF>
#include <QQuickItem>
#include <QQuickWindow>
#include <QUrl>
#include <QVariantList>

#include <memory>

namespace PlasmaZones {

PointerPreviewController::PointerPreviewController(PhosphorPointerShaders::PointerShaderRegistry* registry,
                                                   QObject* parent)
    : QObject(parent)
    , m_registry(registry)
{
    // A pack installed or edited on disk while the browser is open moves what
    // packInfo and configurePreviewItem resolve, and neither is a property QML
    // could bind to on its own.
    if (m_registry) {
        connect(m_registry, &PhosphorPointerShaders::PointerShaderRegistry::effectsChanged, this,
                &PointerPreviewController::bumpPreviewRevision);
    }
}

PointerPreviewController::~PointerPreviewController() = default;

void PointerPreviewController::bumpPreviewRevision()
{
    ++m_previewRevision;
    Q_EMIT previewRevisionChanged();
}

QVariantMap PointerPreviewController::packInfo(const QString& packId) const
{
    QVariantMap info;
    if (!m_registry || packId.isEmpty() || !m_registry->hasEffect(packId)) {
        return info;
    }
    const PhosphorPointerShaders::PointerShaderEffect effect = m_registry->effect(packId);
    info.insert(QStringLiteral("valid"), effect.isValid());
    if (!effect.isValid()) {
        return info;
    }
    info.insert(QStringLiteral("id"), effect.id);
    info.insert(QStringLiteral("name"), effect.name);
    // Surfaced by the pane: an "above" pack replaces the cursor sprite rather
    // than painting under it, and trailSeconds is how long the pane must keep
    // driving frames after the simulated pointer stops.
    info.insert(QStringLiteral("layer"), PhosphorPointerShaders::PointerShaderEffect::layerToken(effect.layer));
    info.insert(QStringLiteral("trailSeconds"), effect.trailSeconds);
    info.insert(QStringLiteral("needsCursor"), effect.needsCursor);

    // The same row shape the page controller's effectToMap emits, so the
    // shared parameter editor reads a pack-info map and a browser row alike.
    QVariantList params;
    params.reserve(effect.parameters.size());
    for (const auto& p : effect.parameters) {
        params.append(decoration_controller_detail::parameterInfoToMap(p));
    }
    info.insert(QStringLiteral("parameters"), params);
    return info;
}

bool PointerPreviewController::configurePreviewItem(QQuickItem* item, const QString& packId,
                                                    const QVariantMap& friendlyParams)
{
    using Registry = PhosphorPointerShaders::PointerShaderRegistry;
    auto* shaderItem = qobject_cast<PhosphorRendering::ShaderEffect*>(item);
    if (!shaderItem || !m_registry || packId.isEmpty() || !m_registry->hasEffect(packId)) {
        return false;
    }
    const PhosphorPointerShaders::PointerShaderEffect effect = m_registry->effect(packId);
    if (!effect.isValid()) {
        return false;
    }

    // Extension BEFORE the sources — see the header's ordering note.
    auto ext = std::make_shared<PhosphorPointerShaders::PointerUniformExtension>();
    // The reach the pack may bound itself to, in logical px; the extension
    // scales it per frame. Re-set by updatePreviewParams because a reachParam
    // makes it follow a slider.
    ext->setReachLogicalPx(effect.resolvedReach(friendlyParams));
    shaderItem->setUniformExtension(ext);

    // `{<packRoot>/shared, <packRoot>}`, so `#include <pointer_lib.glsl>`
    // resolves to the same shared helpers the compositor and the validator
    // expand against.
    // Always set, for the same reason the buffer block below is: a reconfigure
    // of the same item must replace what the previous pack installed, not
    // leave it standing when the new answer happens to be empty.
    shaderItem->setShaderIncludePaths(Registry::includePathsFor(effect.sourceDir));
    shaderItem->setShaderSource(QUrl::fromLocalFile(effect.fragmentShaderPath));
    // The `#define p_<id> customParamsN_x` block, spliced after `#version` at
    // bake time. Its slot allocation mirrors translatePointerParams exactly, so
    // `p_<id>` resolves to the UBO lane setShaderParams uploads to.
    shaderItem->setParamPreamble(Registry::paramPreamble(effect));
    // The `pPointer(vec2 uv)` scaffold. Must match the compositor's and the
    // validator's so all three bake the same source for the same pack.
    shaderItem->setEntryScaffold(Registry::pointerEntryPrologue(), Registry::pointerEntryCandidates());
    if (!effect.vertexShaderPath.isEmpty()) {
        shaderItem->setVertexShaderUrl(QUrl::fromLocalFile(effect.vertexShaderPath));
    }
    // Always-set rather than gated, for the same reason applyEffectStaticConfig
    // is: a metadata edit that turns multipass OFF must clear the buffers a
    // previous configure installed, not leave the item running stale passes.
    if (effect.isMultipass && !effect.bufferShaderPaths.isEmpty()) {
        shaderItem->setBufferShaderPaths(effect.bufferShaderPaths);
        shaderItem->setBufferFeedback(effect.bufferFeedback);
        shaderItem->setBufferScale(effect.bufferScale);
    } else {
        shaderItem->setBufferShaderPaths({});
        shaderItem->setBufferFeedback(false);
        shaderItem->setBufferScale(1.0);
    }
    // A pointer pack ticks continuously while the pointer is live, so iTime is
    // seconds since the pass engaged rather than a progress sweep. The item
    // free-runs it; the pane gates that through `playing`.
    shaderItem->setITime(0.0);
    // The same window the compositor gives its sampler for this pack, so the
    // preview's trail is spaced (and so is as long as) the one on screen.
    // The state is created here rather than on the first drivePointer so the
    // window is in place before the first sample lands.
    stateFor(shaderItem).history.setTrailSeconds(effect.trailSeconds);
    updatePreviewParams(item, packId, friendlyParams);
    return true;
}

PointerPreviewController::PointerState& PointerPreviewController::stateFor(QObject* item)
{
    // One sampler per canvas. The first call for an item default-constructs
    // its state, which is what gives a newly opened pack a fresh ring rather
    // than a trail streaking in from wherever another preview's pointer
    // happened to be. Dropped again when the item goes away, and the
    // destroyed hookup is made exactly once per entry.
    auto stateIt = m_states.find(item);
    if (stateIt == m_states.end()) {
        stateIt = m_states.insert(item, PointerState{});
        connect(item, &QObject::destroyed, this, [this](QObject* gone) {
            m_states.remove(gone);
        });
    }
    return *stateIt;
}

void PointerPreviewController::updatePreviewParams(QQuickItem* item, const QString& packId,
                                                   const QVariantMap& friendlyParams) const
{
    auto* shaderItem = qobject_cast<PhosphorRendering::ShaderEffect*>(item);
    if (!shaderItem || !m_registry || packId.isEmpty() || !m_registry->hasEffect(packId)) {
        return;
    }
    const PhosphorPointerShaders::PointerShaderEffect effect = m_registry->effect(packId);
    if (!effect.isValid()) {
        return;
    }
    const QVariantMap translated =
        PhosphorPointerShaders::PointerShaderRegistry::translatePointerParams(effect, friendlyParams);
    // Unconditional, like the buffer block in configurePreviewItem: a map
    // that translates to nothing must clear the lanes a previous upload left,
    // not leave stale values standing on the item.
    shaderItem->setShaderParams(translated);
    if (const auto ext = std::dynamic_pointer_cast<PhosphorPointerShaders::PointerUniformExtension>(
            shaderItem->uniformExtension())) {
        ext->setReachLogicalPx(effect.resolvedReach(friendlyParams));
    }
}

void PointerPreviewController::drivePointer(QQuickItem* item, qreal x, qreal y, qreal cursorW, qreal cursorH,
                                            qreal dtMs, bool pressed)
{
    auto* shaderItem = qobject_cast<PhosphorRendering::ShaderEffect*>(item);
    if (!shaderItem) {
        return;
    }
    const auto ext =
        std::dynamic_pointer_cast<PhosphorPointerShaders::PointerUniformExtension>(shaderItem->uniformExtension());
    if (!ext) {
        return;
    }

    PointerState& st = stateFor(shaderItem);
    // Clamped so a paused-then-resumed pane (or a first frame with no previous
    // timestamp) cannot jump the clock far enough to age the whole ring out in
    // one step, and never runs backwards.
    st.nowMs += static_cast<qint64>(qBound(0.0, static_cast<double>(dtMs), 250.0));

    // The canvas is the item, in DEVICE px: iResolution is DPR-scaled on the
    // way to the GPU (the extension keeps requiresPhysicalResolution), and
    // every tail position is canvas px, so the history has to be fed device px
    // for the two to agree. iMouse stays logical because the node scales it by
    // the same DPR as iResolution.
    const qreal dpr = shaderItem->window() ? shaderItem->window()->effectiveDevicePixelRatio() : 1.0;
    const QPointF devicePos(x * dpr, y * dpr);
    st.history.notePointer(devicePos, st.nowMs);

    // A synthetic left button, so the press and release edges reach the history
    // through the same noteButtons the compositor calls on a real click.
    if (pressed != st.pressed) {
        const Qt::MouseButtons before = st.pressed ? Qt::MouseButtons(Qt::LeftButton) : Qt::MouseButtons(Qt::NoButton);
        const Qt::MouseButtons now = pressed ? Qt::MouseButtons(Qt::LeftButton) : Qt::MouseButtons(Qt::NoButton);
        st.history.noteButtons(now, before, devicePos, st.nowMs);
        st.pressed = pressed;
    }

    // frameState leaves the sprite rect to the host, because only the host
    // knows where its cursor is drawn. Here that is the canvas's stand-in
    // arrow, hotspot at the tip, so the rect starts at the pointer position
    // and spans the arrow's drawn size, in device px like every other canvas
    // position. A needsCursor pack sizes its work from this rect the way it
    // does from KWin's cursor rect on screen. hasSprite stays FALSE: the
    // contract says uPointerFlags.x is 1 only when uCursorSprite is bound,
    // and the preview binds nothing there (the stand-in arrow is a QML item,
    // not a texture on the sampler), so a pack that samples the sprite must
    // read the rect and skip the sample here, as the contract tells it to.
    PhosphorPointerShaders::PointerFrameState state = st.history.frameState(st.nowMs, dpr);
    state.cursorRect = QRectF(x * dpr, y * dpr, cursorW * dpr, cursorH * dpr);
    state.hasSprite = false;
    ext->apply(state);
    shaderItem->setIMouse(QPointF(x, y));
}

void PointerPreviewController::resetPointer(QQuickItem* item)
{
    // Only this canvas. Another preview may be mid-trail on the same
    // controller, and dropping its ring here would be the very cross-canvas
    // clobbering the per-item keying exists to prevent.
    // Reset in place rather than erasing the entry. The pane calls this on
    // every clock restart, and erasing would make the next drivePointer
    // re-insert and connect a SECOND destroyed handler on the same live item —
    // one more per restart, for as long as the canvas exists.
    const auto it = m_states.find(item);
    // The history's reset keeps its trail window, which configurePreviewItem
    // set for this pack; a fresh PointerState would drop it to the floor.
    if (it != m_states.end()) {
        it->history.reset();
        it->nowMs = 0;
        it->pressed = false;
    }
}

QString PointerPreviewController::wallpaperPath() const
{
    // Same resolver the other three previews use, so all four agree on what
    // "the desktop" is.
    return PhosphorShaders::ShaderRegistry::wallpaperPath();
}

} // namespace PlasmaZones
