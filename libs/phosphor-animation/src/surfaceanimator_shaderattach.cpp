// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "surfaceanimator_p.h"

#include <PhosphorAnimation/AnimationShaderRegistry.h>
#include <PhosphorAnimation/AnimationUniformExtension.h>
#include <PhosphorRendering/ShaderEffect.h>
#include <PhosphorShaders/ShaderRegistry.h>

#include <QQuickItem>
#include <QQuickWindow>
#include <QStack>
#include <QUrl>
#include <QVector4D>
#include <private/qquickhoverhandler_p.h>
#include <private/qquickshadereffectsource_p.h>
#include <private/qquicksinglepointhandler_p.h>

#include <memory>

namespace PhosphorAnimationLayer {

namespace {
/// Pull the `AnimationUniformExtension` out of a shader item the
/// SurfaceAnimator owns, returning nullptr if the shader item somehow
/// has a different (or no) extension. Used by `syncShaderGeometryNow`
/// to push iSurfaceScreenPos / iAnchorSize into the right extension
/// instance — the per-leg attach installs the extension via
/// `ShaderEffect::setUniformExtension`, so a `dynamic_pointer_cast`
/// here pulls back the typed pointer for setter calls.
inline PhosphorAnimation::AnimationUniformExtension* animExtensionFor(PhosphorRendering::ShaderEffect* shaderItem)
{
    if (!shaderItem) {
        return nullptr;
    }
    return std::dynamic_pointer_cast<PhosphorAnimation::AnimationUniformExtension>(shaderItem->uniformExtension())
        .get();
}
} // namespace

namespace detail {

/// Apply the shader-item geometry derived from @p anchor.
/// Centralises the math behind the per-leg syncGeometry lambda so the
/// reuse path can re-fire it explicitly after a metadata hot-reload
/// that flips `fboExtentKind` between legs. Without it, the persistent
/// lambda only re-runs on the next anchor geometry signal, which for a
/// static popup (snap-assist, OSD) may never come. Idempotent on
/// identity (every setter no-ops when the value is unchanged), so the
/// common no-hot-reload reuse path costs nothing.
///
/// `extent` selects where the shader item lives in the QML scene:
///   - Anchor (default): item == anchor. iResolution = anchor pixel
///     size. vTexCoord 0..1 maps the captured anchor texture 1:1 over
///     the FBO. Existing fragment-only shaders use this.
///   - Surface: item fills the anchor's enclosing
///     QQuickWindow::contentItem (the surface scene root, which equals
///     the wl_surface logical size in the screen-sized OSD / popup
///     configurations). Falls back to `anchor->parentItem()` when no
///     QQuickWindow is reachable (headless tests, parentless anchors
///     mid-construction). Qt's QQuickItem::geometryChange auto-resets
///     iResolution to the shader item's bounds on every geometry
///     event, so iResolution naturally tracks the FBO size. Used by
///     vertex-shader effects (fly-in, slide) that need full-surface
///     clip-space travel for their MVP translation, and by fragment
///     shaders (broken-glass, morph) that use `anchorRemap` to convert
///     surface-UV back to anchor-space.
void syncShaderGeometryNow(QQuickItem* anchor, PhosphorRendering::ShaderEffect* shaderItem,
                           QQuickShaderEffectSource* shaderSource,
                           PhosphorAnimationShaders::AnimationShaderEffect::FboExtentKind extent)
{
    if (!anchor || !shaderItem) {
        return;
    }
    const qreal w = anchor->width();
    const qreal h = anchor->height();
    if (w <= 0.0 || h <= 0.0) {
        return;
    }
    // The shader anchor can be larger than the visible card. PopupFrame
    // wraps its frame + glow in a capture item and tags THAT as the
    // anchor, so the soft glow is inside the captured texture and
    // animates with the card through a transition. The anchor publishes
    // the card's rect (anchor-local coords) via the `shaderContentRect`
    // property; an absent / empty value means the anchor IS the card.
    // QRectF::isEmpty() is true for a null variant (toRectF default),
    // a zero rect, or a negative one — every "no usable value" case.
    QRectF contentRect = anchor->property("shaderContentRect").toRectF();
    if (contentRect.isEmpty()) {
        contentRect = QRectF(0.0, 0.0, w, h);
    }
    const qreal cardW = contentRect.width();
    const qreal cardH = contentRect.height();
    // Card's UV sub-rect within the captured anchor texture. animation.vert
    // divides texCoord by it (so anchor-extent shaders get card-space
    // vTexCoord) and `surfaceColor` folds it back in when sampling. The
    // (0,0,1,1) identity falls out when the anchor is the card. `w`/`h`
    // are > 0 here (guarded above), so the divisions are safe.
    const QVector4D anchorRectInTexture(static_cast<float>(contentRect.x() / w),
                                        static_cast<float>(contentRect.y() / h), static_cast<float>(cardW / w),
                                        static_cast<float>(cardH / h));
    // Two-mode extent model — the `fboExtent` JSON key (see
    // AnimationShaderEffect.h) picks the shader item's geometry:
    //
    //   • Surface (opt-in, "fboExtent": "surface"): shader item fills
    //     the QQuickWindow's contentItem (= wl_surface scene root).
    //     Used by shaders that need to render past the captured
    //     anchor — fly-in translates the card across the surface,
    //     broken-glass fires shards into the surrounding screen.
    //     `iAnchorPosInFbo` / `iAnchorSize` / `iResolution` tell the
    //     shader where the anchor sits inside the surface.
    //
    //   • Anchor (default): shader item == anchor, no padding.
    //     Animation shaders work in `vTexCoord ∈ [0, 1]` over the
    //     captured window content; off-window sampling is the
    //     shader's responsibility via `boundaryMask` or
    //     `getClippedInputColor`.
    if (extent == PhosphorAnimationShaders::AnimationShaderEffect::FboExtentKind::Surface) {
        // Position the shader item so its (0, 0) corner aligns with
        // the scene root's origin in parentItem-local coords. For
        // anchors.fill ancestor chains this resolves to (0, 0)
        // directly; for offset popup containers the conversion picks
        // up the parent's local offset so the shader item still maps
        // 1:1 onto the contentItem.
        QQuickItem* sceneRoot = nullptr;
        if (QQuickWindow* win = anchor->window()) {
            sceneRoot = win->contentItem();
        }
        QQuickItem* parent = anchor->parentItem();
        if (sceneRoot && parent) {
            const QPointF rootOriginInParent = parent->mapFromItem(sceneRoot, QPointF(0.0, 0.0));
            shaderItem->setX(rootOriginInParent.x());
            shaderItem->setY(rootOriginInParent.y());
            shaderItem->setWidth(sceneRoot->width());
            shaderItem->setHeight(sceneRoot->height());
        } else if (parent) {
            // Headless / mid-construction fallback: fill the immediate
            // parent. Qt's geometryChange will auto-resize iResolution.
            shaderItem->setX(0.0);
            shaderItem->setY(0.0);
            shaderItem->setWidth(parent->width());
            shaderItem->setHeight(parent->height());
        } else {
            // Parentless anchor — drop back to anchor-relative sizing as
            // a last-resort floor (test-fixture fallback).
            shaderItem->setX(anchor->x());
            shaderItem->setY(anchor->y());
            shaderItem->setWidth(w);
            shaderItem->setHeight(h);
            shaderItem->setIResolution(QSizeF(w, h));
        }
    } else {
        // Anchor mode (default) — the shader item covers the captured
        // anchor 1:1, so a PopupFrame's glow margin (folded into the
        // anchor) is rasterised too and the glow animates with the
        // card. iResolution reports the CARD size, not the anchor
        // size: animation.vert remaps vTexCoord into card space, so a
        // shader pairing [0,1] vTexCoord with iResolution for pixel /
        // aspect math must see the card. setIResolution runs AFTER
        // setWidth/setHeight so it wins over ShaderEffect::geometryChange's
        // auto-reset of iResolution to the item bounds.
        shaderItem->setX(anchor->x());
        shaderItem->setY(anchor->y());
        shaderItem->setWidth(w);
        shaderItem->setHeight(h);
        shaderItem->setIResolution(QSizeF(cardW, cardH));
    }
    if (shaderSource) {
        shaderSource->setWidth(w);
        shaderSource->setHeight(h);
    }
    // iAnchorSize tracks the captured card's pixel size on every sync —
    // independent of iResolution which Qt auto-resets to the shader
    // item's bounds. Vertex shaders read this for the visible card's
    // size in pixels regardless of fboExtentKind.
    //
    // Units: LOGICAL pixels (no DPR multiplication). iResolution lands
    // in the UBO multiplied by DPR (`shadereffect.cpp::syncCustomNode`)
    // so its units differ from the extension fields here. Shaders that
    // need to compute ratios involving FBO size MUST source the FBO
    // size from `iSurfaceScreenPos.zw` (also logical) — see the fly-in
    // vertex shader for the canonical pattern. Shaders that consume
    // `iAnchorSize` as a standalone pixel count (broken-glass's
    // `uSize`, tv-glitch's row offsets) want the logical value because
    // the magic-constant tuning was done against logical dimensions.
    if (auto* ext = animExtensionFor(shaderItem)) {
        ext->setIAnchorSize(QSizeF(cardW, cardH));

        // iAnchorPosInFbo: the CARD's top-left inside the shader item's
        // FBO, in logical pixels. shaderItem is parented to
        // `anchor->parentItem()` (see attachShaderToAnchor), so
        // anchor->x()/y() and shaderItem->x()/y() share the parent's
        // coord system; their difference is the anchor's top-left in
        // the FBO, and `contentRect` offsets that to the card.
        //
        //   Anchor extent: the shaderItem covers the anchor, so the
        //     anchor diff is (0, 0). animation.vert has already remapped
        //     vTexCoord into card space, so the card sits at the origin
        //     of that space — push (0, 0) and `anchorRemap` collapses
        //     to identity.
        //   Surface extent: the diff is the anchor's position within
        //     the surface FBO; adding contentRect's offset yields the
        //     card's position. `anchorRemap` consumes this to fold
        //     surface-UV into card space.
        if (extent == PhosphorAnimationShaders::AnimationShaderEffect::FboExtentKind::Surface) {
            ext->setIAnchorPosInFbo(QPointF(anchor->x() - shaderItem->x() + contentRect.x(),
                                            anchor->y() - shaderItem->y() + contentRect.y()));
        } else {
            ext->setIAnchorPosInFbo(QPointF(0.0, 0.0));
        }

        // iAnchorRectInTexture: the card's UV sub-rect within the
        // captured anchor texture. animation.vert and `surfaceColor`
        // fold it so anchor-extent shaders see card-space [0, 1] even
        // though the FBO captured the card plus its glow margin.
        ext->setIAnchorRectInTexture(anchorRectInTexture);

        // iSurfaceScreenPos describes where the card sits within its
        // *playing field* and how big that field is. The "playing field"
        // on the daemon path is the wl_surface (= the QQuickWindow's
        // contentItem, which is sized to the wl_surface 1:1). For a
        // virtual-screen popup the wl_surface IS the VS rect, so a
        // fly-in from "the nearest edge" naturally reads as flying in
        // from the VS's edge, which is what the user sees.
        //
        // Source zw from `QQuickWindow::contentItem` (not
        // `anchor->parentItem`): for Surface-extent shaders the FBO is
        // already sized to the contentItem (see syncShaderGeometryNow's
        // Surface branch), so reporting parent's width/height here when
        // the anchor sits inside a small inner container would leave
        // `iSurfaceScreenPos.zw` describing the container while the
        // shader's clip-space maps to the wl_surface. Vertex shaders
        // doing closest-edge math would then fire on the wrong edges.
        // Sourcing zw from contentItem keeps the "playing field" the
        // shader sees aligned with the FBO it is rendering into.
        //
        // CRITICAL: do NOT add `window->position()` to the card's
        // surface-local position. For a popup on a VS at screen offset
        // (X, Y), `window->position()` reports the VS offset but the
        // shader's FBO is the wl_surface (sized to the VS, NOT to the
        // physical screen). Adding the offset would produce absolute-
        // screen coords that overflow the FBO clip-space mapping;
        // VS-1's centred card would land at clip-x = 2.0 and never
        // render. xy MUST stay surface-local.
        QQuickItem* sceneRoot = nullptr;
        if (QQuickWindow* window = anchor->window()) {
            sceneRoot = window->contentItem();
        }
        const QPointF anchorScene = anchor->mapToScene(QPointF(0.0, 0.0));
        qreal fieldW = 0.0;
        qreal fieldH = 0.0;
        if (sceneRoot) {
            fieldW = sceneRoot->width();
            fieldH = sceneRoot->height();
        } else if (QQuickItem* parent = anchor->parentItem()) {
            // No window reachable (headless tests, parentless anchors
            // mid-construction); fall back to the immediate parent so
            // the field is at least non-zero and matches what
            // syncShaderGeometryNow's Surface-branch fallback used to
            // size the FBO.
            fieldW = parent->width();
            fieldH = parent->height();
        }
        if (fieldW > 0.0 && fieldH > 0.0) {
            ext->setISurfaceScreenPos(QVector4D(static_cast<float>(anchorScene.x()),
                                                static_cast<float>(anchorScene.y()), static_cast<float>(fieldW),
                                                static_cast<float>(fieldH)));
        }
    }
}

/// Iterative visual-tree search for `shaderAnchor: true` inside an
/// animator target. Walks `childItems()` because Window-rooted QML
/// QObject-parents its content to the Window (not contentItem), so
/// findChild from contentItem walks an empty subtree. QStack-iterative
/// to bound stack usage on deep trees. Returns the first match (prior
/// behaviour) but warns on duplicates.
QQuickItem* findShaderAnchorRecursive(QQuickItem* root)
{
    if (!root) {
        return nullptr;
    }
    QStack<QQuickItem*> stack;
    stack.push(root);
    QQuickItem* firstMatch = nullptr;
    QQuickItem* firstOverride = nullptr;
    bool warnedDuplicate = false;
    while (!stack.isEmpty()) {
        QQuickItem* item = stack.pop();
        if (!item) {
            continue;
        }
        // A decorated-output anchor (`shaderAnchorOverride: true`, set by the
        // SurfaceDecoration host on its composed shader item) ALWAYS wins over
        // a plain `shaderAnchor` tag. The host demotes the raw card's plain
        // tag when its decoration activates, but that demote/promote pair is
        // two independent property writes racing this walk — resolving the
        // raw card here draws the decoration statically at final geometry
        // while the shader animates the bare card. The override tag makes the
        // preference structural instead of ordering-dependent.
        //
        // Do NOT early-return: keep scanning so a SECOND override elsewhere in
        // the tree is caught and warned (a descendant of the first override is
        // NOT — this branch `continue`s without pushing the first override's
        // subtree, so only siblings/cousins outside it are seen). Two live
        // overrides mean a host bug (e.g. a released-but-not-yet-deleted
        // delegate re-tagging itself against the successor chain) — anchoring
        // the corpse animates a frozen capture while the real surface draws
        // statically.
        if (item->property("shaderAnchorOverride").toBool()) {
            if (!firstOverride) {
                firstOverride = item;
            } else {
                // Latch on a root property, like the shaderAnchor dupe warning
                // below, so this fires at most once per scene over the animator's
                // lifetime — runLeg runs on every show/hide and would otherwise
                // spam an identical message into the journal at each leg.
                static const char* kOverrideDupeWarnedProperty = "_phosphorShaderOverrideDupeWarned";
                if (!root->property(kOverrideDupeWarnedProperty).toBool()) {
                    qCWarning(lcSurfaceAnimator).nospace()
                        << "multiple shaderAnchorOverride tags found under " << root << " — using first match "
                        << firstOverride << " ignoring " << item
                        << " (a released delegate may have re-tagged itself; check the host's detag-on-release path)";
                    root->setProperty(kOverrideDupeWarnedProperty, true);
                }
            }
            continue;
        }
        if (item->property("shaderAnchor").toBool()) {
            if (!firstMatch) {
                firstMatch = item;
            } else if (!warnedDuplicate) {
                // Latch a flag on the root so this warning fires at most once
                // per scene over the lifetime of the animator — runLeg is
                // invoked on every show/hide and would otherwise spam an
                // identical message into the journal at each leg.
                static const char* kDupeWarnedProperty = "_phosphorShaderAnchorDupeWarned";
                if (!root->property(kDupeWarnedProperty).toBool()) {
                    qCWarning(lcSurfaceAnimator).nospace()
                        << "multiple shaderAnchor tags found under " << root << " — using first match " << firstMatch
                        << " (objectName='" << firstMatch->objectName() << "') ignoring " << item << " (objectName='"
                        << item->objectName() << "')";
                    root->setProperty(kDupeWarnedProperty, true);
                }
                warnedDuplicate = true;
            }
        }
        const auto kids = item->childItems();
        for (QQuickItem* child : kids) {
            stack.push(child);
        }
    }
    return firstOverride ? firstOverride : firstMatch;
}

/// Hide every visible sibling of the shader anchor for the duration
/// of a leg, returning QPointers so the caller can restore them on
/// teardown. We would prefer to redirect decorator siblings (e.g.
/// `MultiEffect { source: container }`) at the shader's output so the
/// decoration keeps tracking, but Qt 6 MultiEffect crashes inside its
/// private `QQuickShaderEffect` when given a non-stock `QQuickItem` as
/// source. Hiding gives a clean shader transition at the cost of the
/// decoration popping back in at teardown. Skipped for fallback
/// anchors (no `shaderAnchor: true` tag found) — the fallback case
/// hands back the consumer's QML root, whose siblings are usually
/// unrelated layout siblings rather than per-anchor decorators.
///
/// Used by both `attachShaderToAnchor` (fresh attach) and `runLeg`'s
/// reuse branch — between legs `teardownShaderLeg` restores siblings
/// to visible while the parked shader item idles, so the next leg has
/// to re-hide them.
QList<QPointer<QQuickItem>> hideAnchorSiblings(QQuickItem* shaderAnchor, QQuickItem* shaderItem,
                                               QQuickItem* shaderSource, bool foundExplicitAnchor)
{
    QList<QPointer<QQuickItem>> hidden;
    if (!foundExplicitAnchor || !shaderAnchor || !shaderAnchor->parentItem()) {
        return hidden;
    }
    const auto siblings = shaderAnchor->parentItem()->childItems();
    for (QQuickItem* sibling : siblings) {
        if (sibling == shaderAnchor || sibling == shaderItem || sibling == shaderSource) {
            continue;
        }
        // Never hide capture plumbing. A QQuickShaderEffectSource sibling
        // (e.g. the decoration host's card snapshot feeding the anchor's
        // uTexture0) is parked off-screen and draws nothing user-visible,
        // but setVisible(false) suppresses its updatePaintNode and therefore
        // its FBO render — the anchor would then sample a frozen (or, on a
        // fresh show, empty) texture for the whole leg. Hiding it also buys
        // nothing: it is not a decorator drawing a second visible copy.
        if (qobject_cast<QQuickShaderEffectSource*>(sibling)) {
            continue;
        }
        if (sibling->isVisible()) {
            hidden.append(QPointer<QQuickItem>(sibling));
            sibling->setVisible(false);
        }
    }
    return hidden;
}

/// Apply per-effect static configuration (fragment / vertex source,
/// include paths, multipass, wallpaper, depth) to an existing shader
/// item. Idempotent — every `ShaderEffect` setter no-ops on identity, so
/// a repeat call with the same `effect` only incurs the cost of the
/// wallpaper cache lookup (mtime-keyed, mutex-guarded; typically a
/// cache hit).
///
/// Always-set semantics: every effect-level toggle is written
/// unconditionally (the `else` arms explicitly clear), so a metadata.json
/// hot-reload that DISABLES a feature (`isMultipass` true→false,
/// `useWallpaper` true→false, `useDepthBuffer` true→false) propagates
/// onto a reused shader item rather than leaving stale state from the
/// prior leg. The `if (cond) … else …` shape matters here: a gated-set
/// helper would silently keep the old buffers/wallpaper alive across
/// the disable.
///
/// Called from both `attachShaderToAnchor` (fresh attach) and
/// `runLeg`'s reuse branch (between legs of the same effect id, where
/// a metadata.json hot-reload may have changed multipass / wallpaper /
/// depth fields even while the id stayed stable). Centralising here
/// keeps the two paths in lockstep so a future wired-through field
/// can't silently apply only on fresh attach. Per-leg state — `iTime`,
/// translated parameters, geometry, structural padding — is NOT this
/// helper's concern; the caller threads those through separately.
void applyEffectStaticConfig(PhosphorRendering::ShaderEffect* shaderItem,
                             const PhosphorAnimationShaders::AnimationShaderEffect& effect,
                             const QStringList& shaderIncludePaths)
{
    if (!shaderItem) {
        return;
    }
    if (!shaderIncludePaths.isEmpty()) {
        shaderItem->setShaderIncludePaths(shaderIncludePaths);
    }
    shaderItem->setShaderSource(QUrl::fromLocalFile(effect.fragmentShaderPath));
    // T1.1: generate the named-param preamble (`#define p_<id> ...`) from the
    // effect's declared parameters and hand it to the shader item, which splices
    // it after `#version` at bake time. The slot allocation mirrors
    // translateAnimationParams exactly, so `p_<id>` resolves to the same UBO
    // lane the per-leg setShaderParams uploads to. Empty when the effect declares
    // no parameters — a no-op splice, so legacy packs that hand-write their own
    // `#define`s are unaffected (distinct `p_`-prefixed names, no collision).
    shaderItem->setParamPreamble(PhosphorAnimationShaders::AnimationShaderRegistry::paramPreamble(effect));
    // T1.5: install the animation entry-point scaffold so a pack authored as
    // pTransition (symmetric) or pIn/pOut (asymmetric) — no main(), no
    // direction code — is assembled before expansion. A traditional main() pack
    // is left untouched. Must match the kwin-effect + warm-bake scaffold so the
    // bake-cache key agrees across paths.
    shaderItem->setEntryScaffold(PhosphorAnimationShaders::AnimationShaderRegistry::animationEntryPrologue(),
                                 PhosphorAnimationShaders::AnimationShaderRegistry::animationEntryCandidates());
    if (!effect.vertexShaderPath.isEmpty()) {
        shaderItem->setVertexShaderUrl(QUrl::fromLocalFile(effect.vertexShaderPath));
    }
    if (effect.isMultipass && !effect.bufferShaderPaths.isEmpty()) {
        shaderItem->setBufferShaderPaths(effect.bufferShaderPaths);
        shaderItem->setBufferFeedback(effect.bufferFeedback);
        shaderItem->setBufferScale(effect.bufferScale);
        // Per-buffer overrides: pass through unconditionally — empty
        // string normalizes to the ShaderEffect defaults ("clamp"
        // wrap, "linear" filter) per
        // `ShaderNodeRhi::normalizeWrapMode`/`normalizeFilterMode`,
        // and empty list clears any previous-leg overrides. Gating on
        // `!isEmpty()` would let stale per-buffer overrides survive a
        // hot-reload that removed them from `metadata.json`.
        shaderItem->setBufferWrap(effect.bufferWrap);
        shaderItem->setBufferWraps(effect.bufferWraps);
        shaderItem->setBufferFilter(effect.bufferFilter);
        shaderItem->setBufferFilters(effect.bufferFilters);
    } else {
        // Hot-reload disable path: clear whatever the previous leg
        // configured so the reused shader item doesn't keep running
        // stale buffer passes. Empty list / false / 1.0 are the
        // ShaderEffect default-state values; setters no-op when the
        // item is already at default.
        shaderItem->setBufferShaderPaths({});
        shaderItem->setBufferFeedback(false);
        shaderItem->setBufferScale(1.0);
        shaderItem->setBufferWrap(QString());
        shaderItem->setBufferWraps({});
        shaderItem->setBufferFilter(QString());
        shaderItem->setBufferFilters({});
    }
    if (effect.useWallpaper) {
        shaderItem->setUseWallpaper(true);
        shaderItem->setWallpaperTexture(PhosphorShaders::ShaderRegistry::loadWallpaperImage());
    } else {
        shaderItem->setUseWallpaper(false);
    }
    shaderItem->setUseDepthBuffer(effect.useDepthBuffer);

    // User textures are NOT pushed here — they flow through the per-leg
    // setShaderParams call alongside customParams/customColors. The
    // registry's `translateAnimationParams` enriches the params map with
    // pack-default `uTexture<N>` paths (resolved against
    // `effect.sourceDir`) and any per-leg overrides from
    // `friendlyParams`; ShaderEffect::setShaderParams handles the actual
    // file load + node push. Centralising loading there means SVG
    // rasterising, path-change detection, and slot-zero (surface)
    // protection all live in one place — same code path overlay zones
    // already use.
}

/// Build the per-leg ShaderEffect: anchor resolution + layer-enable +
/// setSourceItem (gated on explicit anchors — layer-enabling the
/// fallback QQuickRootItem breaks scene-graph rendering) + parameter
/// translation + live geometry sync.
ShaderAttachResult attachShaderToAnchor(QQuickItem* target,
                                        const PhosphorAnimationShaders::AnimationShaderEffect& effect,
                                        const QString& shaderEffectId, const QVariantMap& shaderParameters,
                                        bool isShowLeg, const QStringList& shaderIncludePaths)
{
    ShaderAttachResult out;
    QQuickItem* shaderAnchor = target;
    bool foundExplicitAnchor = false;
    if (QQuickItem* anchored = findShaderAnchorRecursive(target)) {
        shaderAnchor = anchored;
        foundExplicitAnchor = true;
    }

    qCDebug(lcSurfaceAnimator).nospace() << "shader leg: effect=" << shaderEffectId
                                         << " path=" << effect.fragmentShaderPath << " anchor=" << shaderAnchor
                                         << " explicit=" << foundExplicitAnchor << " size=" << shaderAnchor->width()
                                         << "x" << shaderAnchor->height();

    // Render the anchor into a separate FBO via QQuickShaderEffectSource;
    // the ShaderEffect samples that FBO as uTexture0. The separate FBO
    // is what sidesteps the "Texture used with different accesses within
    // the same pass" Vulkan validation: the source FBO render and the
    // shader effect's read are different render passes.
    //
    // **Gated on `foundExplicitAnchor`**: only created when a descendant
    // tagged `shaderAnchor: true` was found. The fallback path (no tag,
    // anchor==target==QML root) leaves `shaderSource` null and skips the
    // `setSourceItem` call below, so uTexture0 is unbound for the whole
    // leg — the shader runs without a content texture and any
    // `texture(uTexture0, …)` call returns whatever the GL spec does for
    // an unbound sampler (typically transparent black). This is
    // intentional: layer-enabling a QQuickRootItem-rooted target breaks
    // scene-graph rendering for the consumer's whole window. Animation
    // shaders that need an uTexture0 sample MUST be paired with a
    // `shaderAnchor: true` descendant in the consumer's QML; the
    // explicit=false case in the qCDebug above is the operator's signal
    // that a shader is running source-less.
    //
    // live=true: open animations have no prior frame to snapshot. A
    // one-shot grab (live=false) races the source's first layout/paint
    // and captures an empty FBO, leaving uTexture0 transparent for the
    // whole animation. Re-rendering each frame also lets the shader
    // pick up content that moves during the leg.
    //
    // hideSource=true keeps the anchor out of the regular scene render
    // path while still feeding its content to the FBO.
    //
    // Width/Height MUST be non-zero — Qt's QQuickShaderEffectSource
    // skips updatePaintNode (and the FBO render along with it) at 0×0,
    // leaving uTexture0 unpopulated even with hideSource set.
    //
    // Position is parked far off-screen. QQuickShaderEffectSource is
    // itself a renderable item: its updatePaintNode returns a node that
    // composites the FBO at the source's geometry. If we placed it at
    // the anchor's coordinates, that composite would render the FBO
    // contents on top of the anchor's spot — a second full-size copy
    // alongside the shaderItem's effected output. visible=false is not
    // an option (it suppresses updatePaintNode and therefore the FBO
    // render); opacity=0 has the same culling effect. Off-screen
    // positioning leaves Qt's processing intact while the composite
    // node draws somewhere the user can't see.
    constexpr qreal kOffscreenCoord = -1.0e6;
    QQuickShaderEffectSource* shaderSource = nullptr;
    if (foundExplicitAnchor) {
        if (!shaderAnchor->parentItem()) {
            qCWarning(lcSurfaceAnimator) << "Explicit shader anchor" << shaderAnchor
                                         << "has no parent item; shader effect will sample its own ancestor";
        }
        shaderSource =
            new QQuickShaderEffectSource(shaderAnchor->parentItem() ? shaderAnchor->parentItem() : shaderAnchor);
        shaderSource->setSourceItem(shaderAnchor);
        shaderSource->setLive(true);
        shaderSource->setHideSource(true);
        shaderSource->setWidth(shaderAnchor->width());
        shaderSource->setHeight(shaderAnchor->height());
        shaderSource->setX(kOffscreenCoord);
        shaderSource->setY(kOffscreenCoord);
    }

    // Parent the ShaderEffect to the anchor's parent (sibling) so it
    // renders independently. It reads the snapshot texture, not the
    // live layer — no same-pass conflict.
    auto* shaderItem =
        new PhosphorRendering::ShaderEffect(shaderAnchor->parentItem() ? shaderAnchor->parentItem() : shaderAnchor);
    // Install the animation-only UBO extension BEFORE applyEffectStaticConfig.
    // applyEffectStaticConfig may end up calling node-side load paths whose
    // SPIR-V expects the extension to land at offset sizeof(BaseUniforms);
    // installing later would cause the first prepare() to allocate the UBO
    // without the extension's 48 trailing bytes and the shader would read
    // garbage at offsets 672-720 until the next allocation cycle.
    //
    // Lifetime: the shared_ptr is captured by `ShaderEffect::m_uniformExtension`
    // and lives for the shaderItem's lifetime. `syncShaderGeometryNow` pulls
    // the typed pointer back via `animExtensionFor()`'s dynamic_pointer_cast
    // and writes through the extension's setters; the render-thread `write()`
    // serializes via the extension's own QMutex.
    shaderItem->setUniformExtension(std::make_shared<PhosphorAnimation::AnimationUniformExtension>());
    applyEffectStaticConfig(shaderItem, effect, shaderIncludePaths);

    // iMouse wiring — mirrors the overlay-shader pattern (RenderNodeOverlay's
    // `MouseArea { hoverEnabled }` and ShaderSettingsDialog's `HoverHandler`
    // both write cursor position through to `ZoneShaderItem::iMouse`,
    // with a `(-1, -1)` sentinel when the cursor leaves the shader's
    // bounding box). Self-contained: parented to shaderItem so it
    // auto-destroys on teardown; no consumer-side bus required. The
    // handler reports point coordinates in shader-item-local pixels,
    // which equals the shader's `iResolution`, so authors can use
    // `iMouse / iResolution` for normalised-UV math without per-runtime
    // coordinate-system surprises.
    auto* hoverHandler = new QQuickHoverHandler(shaderItem);
    QPointer<PhosphorRendering::ShaderEffect> shaderItemForHover{shaderItem};
    QObject::connect(hoverHandler, &QQuickSinglePointHandler::pointChanged, shaderItem,
                     [shaderItemForHover, hoverHandler]() {
                         if (!shaderItemForHover || !hoverHandler->isHovered()) {
                             return;
                         }
                         shaderItemForHover->setIMouse(hoverHandler->point().position());
                     });
    QObject::connect(hoverHandler, &QQuickHoverHandler::hoveredChanged, shaderItem,
                     [shaderItemForHover, hoverHandler]() {
                         if (!shaderItemForHover || hoverHandler->isHovered()) {
                             return;
                         }
                         // Cursor left the shader's bounding box — match
                         // overlay's `(-1, -1)` off-region sentinel so
                         // shader authors get one consistent "no cursor
                         // here" signal across both runtimes.
                         shaderItemForHover->setIMouse(QPointF(-1.0, -1.0));
                     });
    // Initialise iTime to the leg's start value BEFORE the QQuickShaderEffect
    // can paint a frame. Show legs run iTime 0→1 (start at 0); hide legs run
    // 1→0 (start at 1). If we always initialised to 0.0 here, a hide leg's
    // first paint between attach and the AnimatedValue's start-tick at
    // shaderTime->start(...) would briefly render at iTime=0 — i.e. the
    // "transition start" state (popin scaleFrom, slide slid-out, dissolve
    // fully dissolved) on top of an already-visible surface, producing a
    // visible flash before the hide animation reverses out.
    shaderItem->setITime(isShowLeg ? qreal(0.0) : qreal(1.0));
    // Direction signal for asymmetric shaders. Symmetric shaders ignore
    // it and rely on the iTime flip alone to auto-mirror; matrix and
    // future asymmetric shaders branch on this.
    shaderItem->setIsReversed(!isShowLeg);

    const QVariantMap translated =
        PhosphorAnimationShaders::AnimationShaderRegistry::translateAnimationParams(effect, shaderParameters);
    if (!translated.isEmpty()) {
        shaderItem->setShaderParams(translated);
    }

    // Size + position the shader.
    //
    // iResolution stays in LOGICAL units to match the project-wide shader
    // convention (libs/phosphor-rendering/src/shadereffect.cpp:763-765,
    // common.glsl pxScale()). Animation shaders derive UV from the vertex-
    // stage `vTexCoord` varying, not from `gl_FragCoord.xy / iResolution`
    // — gl_FragCoord is post-DPR pixel coords and would overshoot [0,1]
    // by a factor of DPR.
    //
    // Initial-attach geometry mirrors syncShaderGeometryNow's two-mode
    // logic (Anchor default vs Surface opt-in). Pre-seeding here keeps
    // the first paint between attach and the next syncGeometry() call
    // correctly sized; syncGeometry() refreshes on subsequent geometry
    // events.
    if (effect.fboExtentKind == PhosphorAnimationShaders::AnimationShaderEffect::FboExtentKind::Surface) {
        QQuickItem* sceneRoot = nullptr;
        if (QQuickWindow* win = shaderAnchor->window()) {
            sceneRoot = win->contentItem();
        }
        QQuickItem* parent = shaderAnchor->parentItem();
        if (sceneRoot && parent) {
            const QPointF rootOriginInParent = parent->mapFromItem(sceneRoot, QPointF(0.0, 0.0));
            shaderItem->setX(rootOriginInParent.x());
            shaderItem->setY(rootOriginInParent.y());
            shaderItem->setWidth(sceneRoot->width());
            shaderItem->setHeight(sceneRoot->height());
            shaderItem->setIResolution(QSizeF(sceneRoot->width(), sceneRoot->height()));
        } else if (parent) {
            shaderItem->setX(0.0);
            shaderItem->setY(0.0);
            shaderItem->setWidth(parent->width());
            shaderItem->setHeight(parent->height());
            shaderItem->setIResolution(QSizeF(parent->width(), parent->height()));
        } else {
            shaderItem->setX(shaderAnchor->x());
            shaderItem->setY(shaderAnchor->y());
            shaderItem->setWidth(shaderAnchor->width());
            shaderItem->setHeight(shaderAnchor->height());
            shaderItem->setIResolution(QSizeF(shaderAnchor->width(), shaderAnchor->height()));
        }
    } else {
        // Anchor mode (default) — shader item exactly covers the anchor.
        shaderItem->setX(shaderAnchor->x());
        shaderItem->setY(shaderAnchor->y());
        shaderItem->setWidth(shaderAnchor->width());
        shaderItem->setHeight(shaderAnchor->height());
        shaderItem->setIResolution(QSizeF(shaderAnchor->width(), shaderAnchor->height()));
    }

    // No more customParams[7].x structural write: morph and broken-glass
    // (the only consumers) were ported to read the pad implicitly via
    // `iAnchorPosInFbo / iResolution`. customParams[7] is now a regular
    // user-parameter slot like the other seven, available for any future
    // shader pack that declares >28 float params (customParams[7].x is
    // flat slot 28, reached only once the first 28 sub-slots are filled).

    // Build the geometry-sync lambda + its dependencies.
    //
    // The lambda re-runs the same parent-margin clamp the initial-attach
    // block above does. Anchor x/y/w/h can change mid-leg (parent-layout
    // reflow on sibling-hide; host's anchored layout re-evaluating on
    // parent resize), and the clamp depends on the LIVE values. A
    // fixed `pad` captured at attach time would let the shader item drift
    // past the parent's bounds again whenever the anchor moves toward an
    // edge, re-introducing the viewport-clipping vTexCoord shift the
    // initial clamp avoids. The recomputed pad propagates into
    // `iAnchorPosInFbo` via `syncShaderGeometryNow`, keeping the
    // shader's UV remap consistent with the updated geometry.
    //
    // shaderSource is captured as QPointer so that any teardown path that
    // destroys it independently of shaderItem (scene-graph rebuild,
    // explicit deleteLater outside teardownShaderLeg) doesn't leave the
    // lambda dereferencing freed memory. Auto-disconnect on shaderItem
    // destruction normally protects this — QPointer is belt-and-braces.
    // fboExtentKind is captured by `shared_ptr` so the reuse path can
    // rewrite it under metadata hot-reload (a settings edit that flips
    // the effect's `fboExtent` between Anchor and Surface) and the
    // already-connected syncGeometry lambda picks up the new value on
    // the next anchor geometry signal. Capturing by value would freeze
    // the lambda on the original metadata.
    auto fboExtentKindPtr =
        std::make_shared<PhosphorAnimationShaders::AnimationShaderEffect::FboExtentKind>(effect.fboExtentKind);
    QPointer<PhosphorRendering::ShaderEffect> shaderItemPtr{shaderItem};
    auto syncGeometry = [shaderItemPtr, shaderSourcePtr = QPointer<QQuickShaderEffectSource>(shaderSource),
                         fboExtentKindPtr, anchorPtr = QPointer<QQuickItem>(shaderAnchor)]() {
        syncShaderGeometryNow(anchorPtr.data(), shaderItemPtr.data(), shaderSourcePtr.data(), *fboExtentKindPtr);
    };

    // Seed the fboExtentKind-dependent shader item geometry AND the
    // animation-extension uniforms (iSurfaceScreenPos, iAnchorSize)
    // BEFORE `setSourceItem` wires up the first paint. The render
    // thread's first prepare() copies the extension's m_data verbatim
    // into the UBO; without this seed the first frame samples zeros at
    // offsets 672/688 and a fly-in / slide vert reads
    // `screenW = max(0, 1) = 1`, collapsing `clearancePx` to a single
    // pixel, so the card snaps to its rest position with no slide-in.
    // Pre-fix this only manifested on show legs (hide legs ran against
    // iSurfaceScreenPos values populated by the show leg's later
    // anchor-geometry signals, so the slide-out played correctly).
    //
    // For the persistent-anchor case (zone selector, snap-assist,
    // layout picker pre-warmed PopupFrames), this also seeds the
    // shader item's size: width/height stays default-zero otherwise
    // and the rendered surface collapses to a point for the leg.
    syncGeometry();

    // setSourceItem wires up FBO sampling and triggers the shader
    // effect's first sync into the scene graph. Setting it LAST,
    // after all initial uniforms, custom params, geometry, AND the
    // post-syncGeometry extension push, guarantees that the shader's
    // first painted frame sees a fully-initialised state. Mirrors the
    // setITime ordering rationale above: any state mutation after
    // setSourceItem could race the first paint and produce a visible
    // flash with default-valued uniforms (un-translated shaderParameters
    // leaves user uniforms at -1 sentinel; un-seeded iSurfaceScreenPos
    // collapses fly-in's slide to a snap).
    if (shaderSource) {
        shaderItem->setSourceItem(shaderSource);
    }

    // Connect to all four geometry signals — anchor x/y can shift mid-leg
    // when sibling visibility flips trigger parent layout reflow (we hide
    // visible siblings below; QML Row/ColumnLayout re-pack on visible
    // change), or when the host's anchored layout (centerIn/anchors.fill)
    // re-evaluates on parent resize. Without xChanged/yChanged the shader
    // effect renders at the stale anchor coordinates while the FBO source
    // also captures from there — visible mid-animation drift.
    //
    // CRUCIALLY connected BEFORE the sibling-hide loop. Hiding siblings
    // mutates parent-layout-managed children synchronously, which can
    // emit xChanged/yChanged on the anchor as Row/ColumnLayout re-packs.
    // If the connects ran after the loop, the shader item would be stuck
    // at the pre-reflow coordinates captured at lines 273-297 above —
    // visible offset for the entire show leg until the next geometry
    // signal happens to fire.
    QObject::connect(shaderAnchor, &QQuickItem::widthChanged, shaderItem, syncGeometry);
    QObject::connect(shaderAnchor, &QQuickItem::heightChanged, shaderItem, syncGeometry);
    QObject::connect(shaderAnchor, &QQuickItem::xChanged, shaderItem, syncGeometry);
    QObject::connect(shaderAnchor, &QQuickItem::yChanged, shaderItem, syncGeometry);

    // Hide visible decorator siblings — see `hideAnchorSiblings` for the
    // rationale (Qt 6 MultiEffect can't sample our shader item as source).
    QList<QPointer<QQuickItem>> hiddenSiblings =
        hideAnchorSiblings(shaderAnchor, shaderItem, shaderSource, foundExplicitAnchor);

    // Snap residual scales along the chain shaderAnchor → … → target
    // back to 1.0. The shader-exclusive leg suppresses the C++ scale
    // leg, so a previous non-shader hide that left an intermediate
    // QML item (Loader, host wrapper, the anchor itself) at
    // hideScaleTo would render the entire shader subtree at the
    // residual scale. target.setScale(1.0) below covers the surface
    // root; this loop covers everything between it and the anchor.
    {
        QQuickItem* it = shaderAnchor;
        while (it && it != target) {
            if (!qFuzzyCompare(it->scale(), qreal(1.0))) {
                it->setScale(1.0);
            }
            it = it->parentItem();
        }
    }

    out.shaderItem = shaderItem;
    out.shaderSource = shaderSource;
    out.shaderAnchor = shaderAnchor;
    out.foundExplicitAnchor = foundExplicitAnchor;
    out.hiddenSiblings = std::move(hiddenSiblings);
    out.fboExtentKindPtr = std::move(fboExtentKindPtr);
    return out;
}

} // namespace detail

} // namespace PhosphorAnimationLayer
