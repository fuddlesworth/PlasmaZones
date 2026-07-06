// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/SurfaceAnimator.h>

#include <PhosphorAnimation/AnimatedValue.h>
#include <PhosphorAnimation/AnimationLimits.h>
#include <PhosphorAnimation/IMotionClock.h>
#include <PhosphorAnimation/MotionSpec.h>
#include <PhosphorAnimation/PhosphorProfileRegistry.h>
#include <PhosphorAnimation/Profile.h>

#include <PhosphorAnimation/AnimationShaderContract.h>
#include <PhosphorAnimation/AnimationShaderRegistry.h>
#include <PhosphorAnimation/AnimationUniformExtension.h>
#include <PhosphorRendering/ShaderEffect.h>
#include <PhosphorShaders/ShaderRegistry.h>

#include <PhosphorLayer/Role.h>
#include <PhosphorLayer/Surface.h>
#include <PhosphorLayer/SurfaceConfig.h>

#include <QDir>
#include <QFile>
#include <QHash>
#include <QLoggingCategory>
#include <QObject>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScreen>
#include <QVector4D>
#include <private/qquickhoverhandler_p.h>
#include <private/qquickshadereffectsource_p.h>
#include <private/qquicksinglepointhandler_p.h>
#include <QStack>
#include <QTimer>
#include <QUrl>

#include <chrono>
#include <memory>
#include <unordered_map>

namespace PhosphorAnimationLayer {

namespace {
Q_LOGGING_CATEGORY(lcSurfaceAnimator, "phosphoranimationlayer.surfaceanimator")

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

/// Driver tick interval. SteadyClock::refreshRate MUST agree so
/// velocity-based curves (Spring) sample at the timer's cadence.
constexpr int kTickIntervalMs = 16;
constexpr qreal kRefreshRateHz = 1000.0 / kTickIntervalMs;

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
inline void syncShaderGeometryNow(QQuickItem* anchor, PhosphorRendering::ShaderEffect* shaderItem,
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
} // namespace

namespace internal {
/// Steady-clock IMotionClock. Independent of QQuickWindow rendering
/// so offscreen QPA (headless CI) ticks too. `internal` (not
/// anonymous) for external linkage — avoids -Wsubobject-linkage on
/// the SurfaceAnimator::Private member type.
class SteadyClock : public PhosphorAnimation::IMotionClock
{
public:
    std::chrono::nanoseconds now() const override
    {
        return std::chrono::steady_clock::now().time_since_epoch();
    }

    qreal refreshRate() const override
    {
        return kRefreshRateHz;
    }

    /// requestFrame is a hint — the SurfaceAnimator's QTimer already
    /// ticks unconditionally while any track is in flight. No-op.
    void requestFrame() override
    {
    }
};
} // namespace internal

namespace {

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

/// Resolve a Profile path through the registry, falling back to a
/// library-default Profile. Empty path is the documented "use
/// defaults" sentinel; a non-empty unresolved path warns on the
/// journal — typo backstop for C++ profile registrations the QML-
/// side lint can't see.
PhosphorAnimation::Profile resolveProfile(PhosphorAnimation::PhosphorProfileRegistry& registry, const QString& path)
{
    if (path.isEmpty()) {
        return PhosphorAnimation::Profile{}.withDefaults();
    }
    // Inheritance-aware resolve so a parent-node card edit at e.g.
    // `popup` propagates to every `popup.*.*` leg even
    // when the bundled per-leaf JSONs (popup.layoutPicker.show
    // .json etc.) hardcode their own duration. The previous direct
    // `registry.resolve(path)` call only matched the exact leaf —
    // bundled leaf entries silently shadowed the user's parent
    // override and the slider had no visible effect on legs whose
    // bundled JSON happened to ship.
    return registry.resolveWithInheritance(path);
}

/// Build a MotionSpec<qreal> — centralises construction so beginShow
/// / beginHide read identically.
PhosphorAnimation::MotionSpec<qreal> buildSpec(const PhosphorAnimation::Profile& profile,
                                               PhosphorAnimation::IMotionClock* clock,
                                               std::function<void(const qreal&)> onValueChanged,
                                               std::function<void()> onComplete)
{
    PhosphorAnimation::MotionSpec<qreal> spec;
    spec.profile = profile;
    spec.clock = clock;
    spec.onValueChanged = std::move(onValueChanged);
    spec.onComplete = std::move(onComplete);
    return spec;
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

/// Pieces produced by `attachShaderToAnchor`. The caller stashes
/// these on its in-flight Track; the helper stays free of the inner
/// Track type so a future caller can reuse it.
struct ShaderAttachResult
{
    PhosphorRendering::ShaderEffect* shaderItem = nullptr;
    QQuickShaderEffectSource* shaderSource = nullptr;
    QQuickItem* shaderAnchor = nullptr;
    bool foundExplicitAnchor = false;
    /// Anchor siblings whose `visible` we flipped to false for the leg.
    /// Restored on teardown. QPointer guards against the consumer's
    /// QML scene tearing down a sibling mid-flight.
    ///
    /// Decorator siblings (e.g. `MultiEffect { source: container }`)
    /// sample the anchor's layer FBO and render `source + shadow` in
    /// parallel with the shader leg — without hiding them the user
    /// sees a full-size copy of the anchor stacked behind the shader.
    /// We tried redirecting MultiEffect.source→shaderItem (so the
    /// decorator follows the shader's output and keeps drawing the
    /// shadow), but Qt 6's MultiEffect crashes inside its private
    /// QQuickShaderEffect's event handler when the source is a
    /// non-stock QQuickItem subclass. Hide for now; the
    /// shadow-pop-in at teardown is the lesser evil.
    QList<QPointer<QQuickItem>> hiddenSiblings;
    /// Live fboExtentKind. Captured by the syncGeometry lambda by
    /// `shared_ptr` so a metadata edit that flips an effect from
    /// Anchor to Surface (or vice versa) takes effect on the next
    /// geometry signal without reattaching the shader.
    std::shared_ptr<PhosphorAnimationShaders::AnimationShaderEffect::FboExtentKind> fboExtentKindPtr;
};

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
    // the ShaderEffect samples that FBO as iChannel0. The separate FBO
    // is what sidesteps the "Texture used with different accesses within
    // the same pass" Vulkan validation: the source FBO render and the
    // shader effect's read are different render passes.
    //
    // **Gated on `foundExplicitAnchor`**: only created when a descendant
    // tagged `shaderAnchor: true` was found. The fallback path (no tag,
    // anchor==target==QML root) leaves `shaderSource` null and skips the
    // `setSourceItem` call below, so iChannel0 is unbound for the whole
    // leg — the shader runs without a content texture and any
    // `texture(iChannel0, …)` call returns whatever the GL spec does for
    // an unbound sampler (typically transparent black). This is
    // intentional: layer-enabling a QQuickRootItem-rooted target breaks
    // scene-graph rendering for the consumer's whole window. Animation
    // shaders that need an iChannel0 sample MUST be paired with a
    // `shaderAnchor: true` descendant in the consumer's QML; the
    // explicit=false case in the qCDebug above is the operator's signal
    // that a shader is running source-less.
    //
    // live=true: open animations have no prior frame to snapshot. A
    // one-shot grab (live=false) races the source's first layout/paint
    // and captures an empty FBO, leaving iChannel0 transparent for the
    // whole animation. Re-rendering each frame also lets the shader
    // pick up content that moves during the leg.
    //
    // hideSource=true keeps the anchor out of the regular scene render
    // path while still feeding its content to the FBO.
    //
    // Width/Height MUST be non-zero — Qt's QQuickShaderEffectSource
    // skips updatePaintNode (and the FBO render along with it) at 0×0,
    // leaving iChannel0 unpopulated even with hideSource set.
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
    // shader pack that declares >24 float params.

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

} // namespace

// ══════════════════════════════════════════════════════════════════════════
// SurfaceAnimator::Private
// ══════════════════════════════════════════════════════════════════════════

class SurfaceAnimator::Private
{
public:
    /// Composite key for in-flight tracks. A single Surface can host
    /// multiple animated targets simultaneously (the unified overlay
    /// shell pattern: one per-screen wl_surface containing OSD,
    /// zone-selector, and modal items as siblings, each animated
    /// independently). Pre-shell, m_tracks was keyed on Surface*
    /// alone — adding a second target on the same surface would
    /// supersede the first via cancelTracking. Now tracks are keyed
    /// on the pair so concurrent legs on different items in the
    /// same surface coexist.
    struct TrackKey
    {
        PhosphorLayer::Surface* surface;
        QQuickItem* target;
        bool operator==(const TrackKey&) const = default;
    };
    struct TrackKeyHash
    {
        std::size_t operator()(const TrackKey& k) const noexcept
        {
            // Mix the two pointers — XOR alone collides on
            // (a, b) and (b, a). Shift the second half to break
            // that symmetry; std::hash<void*> on a sane stdlib is
            // identity for pointers, so the shift matters.
            return std::hash<const void*>()(k.surface) ^ (std::hash<const void*>()(k.target) << 1);
        }
    };

    /// Per-(surface, target) in-flight bookkeeping. unique_ptr<AnimatedValue>
    /// so slot.opacity.reset() can null the AV before installing a
    /// fresh one — AnimatedValue itself has no moved-from state.
    struct Track
    {
        QPointer<QQuickItem> target; ///< Auto-nulls if QML scene tears down mid-flight
        /// Where the shader effect is parented + sized. Equal to
        /// `target` unless an explicit `property bool shaderAnchor:
        /// true` was found by attachShaderToAnchor.
        QPointer<QQuickItem> shaderAnchor;
        bool foundExplicitAnchor = false;
        std::unique_ptr<PhosphorAnimation::AnimatedValue<qreal>> opacity;
        std::unique_ptr<PhosphorAnimation::AnimatedValue<qreal>> scale;
        std::unique_ptr<PhosphorAnimation::AnimatedValue<qreal>> shaderTime;
        QPointer<PhosphorRendering::ShaderEffect> shaderItem;
        QPointer<QQuickShaderEffectSource> shaderSource;
        /// Anchor siblings flipped to invisible for the leg. Restored on
        /// teardown. QPointer auto-nulls if the QML scene tears down mid-
        /// flight.
        QList<QPointer<QQuickItem>> hiddenSiblings;
        /// Effect id of the currently-attached shader item (empty when no
        /// shader leg is active). Used by runLeg to detect "same shader,
        /// can reuse the existing ShaderEffect/Source pair across legs"
        /// and skip the create-then-destroy-then-recreate churn that
        /// otherwise leaks render-thread RHI resources under rapid
        /// show/hide toggling (zone selector during a drag).
        QString shaderEffectId;
        /// Shared with the syncGeometry lambda so a metadata hot-reload
        /// that flips fboExtentKind (Anchor ↔ Surface) takes effect on
        /// the next geometry signal without reattaching.
        std::shared_ptr<PhosphorAnimationShaders::AnimationShaderEffect::FboExtentKind> fboExtentKindPtr;
        int pendingLegs = 0;
        bool shaderExclusive = false; ///< Shader replaces motion legs
        qreal targetOpacity = 1.0; ///< Snapped on completion when shaderExclusive
        ISurfaceAnimator::CompletionCallback onComplete;
        /// Per-leg `iFrame` counter pushed to the shader item by
        /// `pushDynamicShaderUniforms`. Resets to 0 on each fresh attach
        /// (matches overlay convention: `iFrame` starts at 0 when the
        /// shader first runs and increments per rendered frame).
        /// Post-incremented at push time so the first frame sees 0,
        /// the second sees 1, etc. Reset on reuse-attach as well —
        /// `iFrame` is per-leg, not per-shader-item-lifetime, so a
        /// reused shader item starts fresh at 0 on the next leg.
        int shaderFrameCount = 0;
    };

    /// Stash for shader pieces between legs. Populated by
    /// teardownShaderLeg, drained by runLeg's reuse path or by
    /// destroyPendingReuseFor. See teardownShaderLeg's comment for
    /// the rationale (avoids RHI resource batch pool exhaustion under
    /// rapid show/hide toggling).
    struct PendingReuseShader
    {
        QPointer<PhosphorRendering::ShaderEffect> shaderItem;
        QPointer<QQuickShaderEffectSource> shaderSource;
        QPointer<QQuickItem> shaderAnchor;
        bool foundExplicitAnchor = false;
        QString shaderEffectId;
        QPointer<QQuickItem> target;
        /// See Track::fboExtentKindPtr.
        std::shared_ptr<PhosphorAnimationShaders::AnimationShaderEffect::FboExtentKind> fboExtentKindPtr;
    };

    Private(PhosphorAnimation::PhosphorProfileRegistry& registry,
            PhosphorAnimationShaders::AnimationShaderRegistry* shaderRegistry, Config defaults)
        : m_registry(registry)
        , m_shaderRegistry(shaderRegistry)
        , m_defaultConfig(std::move(defaults))
    {
        // Pin the member-destruction-order invariant connectSurfaceCleanup
        // depends on. m_driverTimer is the receiver context for every
        // per-surface destroyed-signal lambda we install; those lambdas
        // touch m_pendingReuse and m_tracks. Member dtor order is reverse
        // of declaration order, so m_driverTimer MUST be declared after
        // m_pendingReuse and m_tracks (= destroyed FIRST, severing
        // connections before the maps it accesses tear down). Without
        // this assertion a future maintainer reordering members for
        // readability could silently introduce a UAF on shutdown.
        //
        // `Private` is non-standard-layout (QObject-derived members,
        // std::function callbacks, mixed access specifiers) which makes
        // `offsetof` "conditionally-supported" per [class.mem]/27, so
        // GCC raises -Winvalid-offsetof. Every mainstream compiler
        // (GCC, Clang, MSVC) actually computes the right value here —
        // the warning is a portability hint, not a correctness bug.
        // Suppress locally; the static_assert is what we want, and any
        // compiler that DIDN'T support offsetof on this type would
        // simply fail to compile rather than miscompare.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
        static_assert(offsetof(Private, m_driverTimer) > offsetof(Private, m_pendingReuse),
                      "m_driverTimer must be declared AFTER m_pendingReuse so connectSurfaceCleanup's "
                      "destroyed-lambda is auto-disconnected before the map it touches dies.");
        static_assert(offsetof(Private, m_driverTimer) > offsetof(Private, m_tracks),
                      "m_driverTimer must be declared AFTER m_tracks so connectSurfaceCleanup's "
                      "destroyed-lambda is auto-disconnected before the map it touches dies.");
#pragma GCC diagnostic pop

        // Driver fires kTickIntervalMs cadence while any track is in
        // flight; tickAll stops the timer when m_tracks empties.
        m_driverTimer.setInterval(std::chrono::milliseconds(kTickIntervalMs));
        m_driverTimer.setTimerType(Qt::PreciseTimer);
        QObject::connect(&m_driverTimer, &QTimer::timeout, [this]() {
            tickAll();
        });
    }

    /// Wind down the per-leg shader pieces, parking the
    /// ShaderEffect/Source pair into m_pendingReuse[surface] so a
    /// follow-up runLeg can reclaim them under the same effect id.
    /// The shaderTime AV is leg-specific (its `from`/`to` are tied to
    /// the leg's iTime direction) and goes to the graveyard regardless.
    /// Sibling visibility is RESTORED on park — between legs the
    /// surface is visually idle, and a follow-up reuse-attach hides
    /// them again before its own first paint.
    ///
    /// CRUCIALLY the parked items are made DORMANT — `setVisible(false)`
    /// on the shaderItem stops its prepare()/render() each frame, and
    /// `setLive(false)` on the QQuickShaderEffectSource stops its FBO
    /// refresh. Without this the parked items keep consuming a Qt RHI
    /// resource update batch per frame each (UBO upload + FBO render)
    /// for the rest of the surface's lifetime — across rapid toggling
    /// the 64-batch global pool exhausts within seconds even though
    /// reuse "fixed" the per-leg ShaderEffect creation. The dormant
    /// state contributes zero per-frame cost.
    ///
    /// `setHideSource(false)` is also restored so the original anchor
    /// renders normally during the idle phase between legs — without
    /// it, the anchor stays suppressed from the scene render path
    /// (because hideSource installs a QQuickItemLayer on the source)
    /// and decorator siblings (drop shadows, badges) sample empty
    /// content, making the post-leg static-Shown state look broken.
    ///
    /// Without parking, every leg's shader pieces went to deleteLater
    /// and the render thread couldn't drain them fast enough under
    /// rapid show/hide toggling — same pool exhaustion via a different
    /// mechanism. Parking + dormant state lets one ShaderEffect /
    /// Source / ShaderNodeRhi tower live for the surface's lifetime
    /// while contributing no per-frame cost between legs.
    ///
    /// Surface destruction is handled by the destroyed-signal
    /// connection installed in connectSurfaceCleanup — when surface
    /// dies, the QPointers in m_pendingReuse auto-null and the entry
    /// is dropped.
    void teardownShaderLeg(PhosphorLayer::Surface* surface, Track& track)
    {
        if (track.shaderTime) {
            track.shaderTime->cancel();
            m_pendingDestroy.push_back(std::move(track.shaderTime));
        }
        // Restore previously-hidden sibling decorators FIRST. Reused
        // shader runs hide them again on next attach, so this just
        // covers the between-legs and post-final-leg cases.
        for (const QPointer<QQuickItem>& sibling : track.hiddenSiblings) {
            if (sibling) {
                sibling->setVisible(true);
            }
        }
        track.hiddenSiblings.clear();
        if (!track.shaderItem || track.shaderEffectId.isEmpty()) {
            // Nothing to park (non-shader track, or shader pieces
            // already moved out by an earlier reuse-claim).
            track.shaderItem = nullptr;
            track.shaderSource = nullptr;
            track.shaderAnchor.clear();
            track.foundExplicitAnchor = false;
            track.shaderEffectId.clear();
            return;
        }

        // If track.target's QPointer auto-nulled because the
        // QQuickItem was destroyed mid-flight, parking under
        // {surface, nullptr} keys the entry under a sentinel that
        // runLeg never queries (it always passes a non-null target).
        // Skip the park step: nothing useful to reuse, and burning
        // an unreclaimable slot in m_pendingReuse just leaks until
        // surface destruction. Tear the pieces down inline.
        if (!track.target) {
            PendingReuseShader transient;
            transient.shaderItem = std::move(track.shaderItem);
            transient.shaderSource = std::move(track.shaderSource);
            transient.shaderAnchor = track.shaderAnchor;
            destroyPendingReuseEntry(transient);
            track.shaderItem = nullptr;
            track.shaderSource = nullptr;
            track.shaderAnchor.clear();
            track.foundExplicitAnchor = false;
            track.shaderEffectId.clear();
            track.fboExtentKindPtr.reset();
            return;
        }
        // Park to m_pendingReuse[{surface, target}]. If a previous
        // pending entry exists for this (surface, target) pair (rare —
        // should only happen if a shader was parked but never
        // reclaimed), destroy it first to avoid leaking a stale
        // ShaderEffect.
        connectSurfaceCleanup(surface);
        PendingReuseShader& pending = m_pendingReuse[TrackKey{surface, track.target.data()}];
        if (pending.shaderItem) {
            destroyPendingReuseEntry(pending);
        }
        pending.shaderItem = std::move(track.shaderItem);
        pending.shaderSource = std::move(track.shaderSource);
        pending.shaderAnchor = track.shaderAnchor;
        pending.foundExplicitAnchor = track.foundExplicitAnchor;
        pending.shaderEffectId = std::move(track.shaderEffectId);
        pending.target = track.target;
        pending.fboExtentKindPtr = std::move(track.fboExtentKindPtr);

        // Make parked pieces dormant — see method docstring.
        if (pending.shaderItem) {
            pending.shaderItem->setVisible(false);
        }
        if (pending.shaderSource) {
            // setLive(false) freezes FBO refresh. setHideSource(false)
            // re-enables the anchor's normal scene render so the user
            // sees the anchor + decorators during the idle phase
            // exactly as they would without any shader.
            pending.shaderSource->setLive(false);
            pending.shaderSource->setHideSource(false);
        }

        track.shaderItem = nullptr;
        track.shaderSource = nullptr;
        track.shaderAnchor.clear();
        track.foundExplicitAnchor = false;
        track.shaderEffectId.clear();
        track.fboExtentKindPtr.reset();
    }

    /// Destroy a single pending-reuse entry's render-side pieces.
    /// Disconnects the anchor signal hookups before deleteLater so
    /// the scene-graph reflow window between deleteLater and dispatch
    /// can't fire xChanged/yChanged/widthChanged/heightChanged on the
    /// anchor into a queued-for-delete receiver.
    void destroyPendingReuseEntry(PendingReuseShader& pending)
    {
        if (pending.shaderItem) {
            if (pending.shaderAnchor) {
                QObject::disconnect(pending.shaderAnchor.data(), nullptr, pending.shaderItem.data(), nullptr);
            }
            pending.shaderItem->deleteLater();
            pending.shaderItem = nullptr;
        }
        if (pending.shaderSource) {
            pending.shaderSource->deleteLater();
            pending.shaderSource = nullptr;
        }
        pending.shaderAnchor.clear();
        pending.foundExplicitAnchor = false;
        pending.shaderEffectId.clear();
        pending.target.clear();
        pending.fboExtentKindPtr.reset();
    }

    /// Drop the reuse stash for one surface — used when the surface is
    /// destroyed, when the next leg uses a different effect / target,
    /// or when a non-shader leg supersedes a shader leg.
    /// Walks every (surface, *) entry — a surface may have multiple
    /// targets parked simultaneously now that the unified shell hosts
    /// independent items as siblings on one Surface.
    void destroyPendingReuseFor(PhosphorLayer::Surface* surface)
    {
        for (auto it = m_pendingReuse.begin(); it != m_pendingReuse.end();) {
            if (it->first.surface == surface) {
                destroyPendingReuseEntry(it->second);
                it = m_pendingReuse.erase(it);
            } else {
                ++it;
            }
        }
    }

    /// Single-(surface, target) variant. Used by runLeg's null-target
    /// path: target is null, so iterate every (surface, *) anyway.
    /// Used by teardownShaderLeg-driven reuse claim: caller has
    /// already moved fields out, so we just erase.
    void destroyPendingReuseForKey(const TrackKey& key)
    {
        const auto it = m_pendingReuse.find(key);
        if (it == m_pendingReuse.end()) {
            return;
        }
        destroyPendingReuseEntry(it->second);
        m_pendingReuse.erase(it);
    }

    /// Wire surface->destroyed once per surface (per animator instance)
    /// so we drop its reuse stash when it dies, and (defensively) any
    /// still-tracked entry. Tracked in m_destroyedConnections — the
    /// per-animator map ensures that a surface re-encountered by a
    /// FRESH animator instance (e.g. test fixtures, daemon hot-restart
    /// reusing a Surface*) gets its OWN connection rather than seeing a
    /// dynamic-property flag set by a long-dead prior animator.
    /// `contains()` is the gate; `insert()` ties the Connection lifetime
    /// to the map entry so a future `disconnect` can be issued via
    /// `QObject::disconnect(connection)` if we ever need explicit
    /// teardown without surface destruction.
    void connectSurfaceCleanup(PhosphorLayer::Surface* surface)
    {
        if (m_destroyedConnections.contains(surface)) {
            return;
        }
        // Cast is address-only-safe — destroyed fires from ~QObject
        // after ~Surface ran, so the value MUST NOT be dereferenced.
        // Using the QObject* form keeps the contract obvious.
        // Use m_driverTimer as the receiver context — it's a QObject
        // member of Private, so when this animator is destroyed
        // m_driverTimer dies first (member dtor order = reverse of
        // declaration; m_driverTimer is declared LAST) and the
        // connection auto-disconnects before m_pendingReuse tears
        // down. SurfaceAnimator itself isn't a QObject so we can't
        // use `this` as the receiver.
        QMetaObject::Connection conn =
            QObject::connect(surface, &QObject::destroyed, &m_driverTimer, [this](QObject* dying) {
                // Address-only cast — dying object is partially destroyed;
                // we only use the pointer value as a map key.
                auto* surf = static_cast<PhosphorLayer::Surface*>(dying);
                // Mutating m_destroyedConnections at the end of this
                // slot is safe because destroyPendingReuseFor's only
                // synchronous side-effect is deleteLater() on shader
                // pieces — Qt's deferred-delete mechanism queues the
                // destruction for the next event-loop iteration and
                // never re-enters connectSurfaceCleanup. If a future
                // edit ever makes destroyPendingReuseEntry synchronous
                // (e.g. a hard delete instead of deleteLater) and
                // re-enters this animator, this remove() must happen
                // BEFORE that path or be re-checked for invalidation.
                destroyPendingReuseFor(surf);
                // Also drop every still-tracked (surf, *) entry;
                // consumer's SurfaceAnimator::cancel may not have run
                // for surfaces that bypass the standard ~Surface cancel
                // path. Walk the map and erase all matching entries.
                for (auto trackIt = m_tracks.begin(); trackIt != m_tracks.end();) {
                    if (trackIt->first.surface == surf) {
                        trackIt = m_tracks.erase(trackIt);
                    } else {
                        ++trackIt;
                    }
                }
                m_destroyedConnections.remove(surf);
            });
        m_destroyedConnections.insert(surface, conn);
    }

    /// Cancel a single (surface, target) leg. Called by runLeg's pre-
    /// cancel (supersession of the SAME target) and by cancelAllForSurface.
    /// AnimatedValue::cancel clears m_isAnimating without firing
    /// spec.onComplete (cancel = non-completion termination per
    /// ISurfaceAnimator). AVs are parked in m_pendingDestroy because a
    /// re-entrant cancel from inside spec.onValueChanged must not
    /// destroy *this mid-advance (AnimatedValue.h:547); graveyard
    /// drains on next tickAll. Shader pieces are parked in
    /// m_pendingReuse (see teardownShaderLeg) so they survive the
    /// external Surface::show()/hide() pre-cancel and can be reclaimed
    /// by the immediately-following beginShow/Hide.
    void cancelTrackingFor(PhosphorLayer::Surface* surface, QQuickItem* target)
    {
        const auto it = m_tracks.find(TrackKey{surface, target});
        if (it == m_tracks.end()) {
            return;
        }
        // Cancel BEFORE moving so m_isAnimating=false propagates to
        // any advance() frame still on the stack — post-callback work
        // there then bails on the m_isAnimating guard rather than
        // mutating cancelled state.
        if (it->second.opacity) {
            it->second.opacity->cancel();
            m_pendingDestroy.push_back(std::move(it->second.opacity));
        }
        if (it->second.scale) {
            it->second.scale->cancel();
            m_pendingDestroy.push_back(std::move(it->second.scale));
        }
        teardownShaderLeg(surface, it->second);
        m_tracks.erase(it);
    }

    /// Cancel every in-flight leg for the surface, regardless of target.
    /// Used by the public ISurfaceAnimator::cancel(Surface*) contract,
    /// which means "wipe all motion for this surface" — appropriate for
    /// teardown paths and consumer-driven dismissals where every item
    /// hosted by the surface should stop animating.
    void cancelAllForSurface(PhosphorLayer::Surface* surface)
    {
        // Snapshot keys first — cancelTrackingFor erases from m_tracks,
        // and erase + iterate on the same unordered_map invalidates
        // iterators of the erased bucket.
        std::vector<QQuickItem*> targets;
        targets.reserve(m_tracks.size());
        for (const auto& [key, _] : m_tracks) {
            if (key.surface == surface) {
                targets.push_back(key.target);
            }
        }
        for (QQuickItem* target : targets) {
            cancelTrackingFor(surface, target);
        }
    }

    /// Look up the per-role config (longest-prefix-match on
    /// scopePrefix, fall back to default). Consumers derive per-
    /// surface roles via withScopePrefix("base-{screenId}") and
    /// register configs against the unsuffixed base, so a bare find()
    /// would always miss. Match boundary is '-' or end-of-string so
    /// "phosphor-layout" doesn't collide with "phosphor-layout-
    /// picker-..." when both are registered.
    Config configFor(const PhosphorLayer::Role& role) const
    {
        const QString& target = role.scopePrefix;
        int bestLen = -1;
        Config bestCfg = m_defaultConfig;
        for (auto it = m_configByRole.constBegin(); it != m_configByRole.constEnd(); ++it) {
            const QString& key = it.key();
            if (key.isEmpty() || !target.startsWith(key)) {
                continue;
            }
            if (target.size() != key.size() && target.at(key.size()) != QLatin1Char('-')) {
                continue;
            }
            if (key.size() > bestLen) {
                bestLen = key.size();
                bestCfg = it.value();
            }
        }
        return bestCfg;
    }

    /// Run a show or hide leg. Used by beginShow/beginHide. Threads
    /// the from→to opacity/scale pair, the profile pair, the optional
    /// shader leg, and the caller's onComplete; fires onComplete once
    /// every leg settles. Empty *ProfilePath = skip that leg.
    void runLeg(PhosphorLayer::Surface* surface, QQuickItem* target, qreal fromOpacity, qreal toOpacity,
                const QString& opacityProfilePath, qreal fromScale, qreal toScale, const QString& scaleProfilePath,
                const QString& shaderEffectId, const QString& shaderProfilePath, const QVariantMap& shaderParameters,
                ISurfaceAnimator::CompletionCallback onComplete)
    {
        // Resolve shader up front so legCount is accurate.
        PhosphorAnimationShaders::AnimationShaderEffect resolvedShaderEff;
        QStringList animIncludePaths;
        if (!shaderEffectId.isEmpty() && m_shaderRegistry) {
            resolvedShaderEff = m_shaderRegistry->effect(shaderEffectId);
            for (const QString& sp : m_shaderRegistry->searchPaths()) {
                const QString sharedDir = sp + QStringLiteral("/shared");
                if (QDir(sharedDir).exists()) {
                    animIncludePaths.append(sharedDir);
                    if (resolvedShaderEff.isValid() && resolvedShaderEff.vertexShaderPath.isEmpty()) {
                        const QString sharedVert = sharedDir + QStringLiteral("/animation.vert");
                        if (QFile::exists(sharedVert)) {
                            resolvedShaderEff.vertexShaderPath = sharedVert;
                        }
                    }
                }
            }
        }
        const bool hasShaderLeg = resolvedShaderEff.isValid();

        // Supersede BEFORE the null-target check — leaving a prior
        // entry behind on null-target strands its AV (custom
        // orchestrators bypass Surface::Impl::drive()'s pre-cancel).
        // cancelTrackingFor parks any in-flight shader pieces in
        // m_pendingReuse[{surface,target}] (see parkShaderForReuse) so
        // they survive the external Surface::show()/hide() pre-cancel
        // and can be reclaimed below if the new leg uses the same
        // effect. Null target means "supersede whatever was running on
        // any target of this surface" — fall back to surface-wide
        // cancel and drop every stash.
        if (!target) {
            cancelAllForSurface(surface);
            qCWarning(lcSurfaceAnimator) << "runLeg called with null target — onComplete fires synchronously";
            // Null-target dispatch is malformed and won't drive any
            // specific (surface, target) pair. The tickAll sweep will
            // GC stale pending entries that outlive their target's
            // QPointer; do NOT cascade-destroy every sibling slot's
            // parked shader on this surface. The unified-overlay-shell
            // pattern hosts multiple sibling slots per Surface, so
            // surface-wide destruction violates the per-(Surface, target)
            // keying contract.
            // Synchronous onComplete: same forbidden-ops contract as a
            // legCompleted-fired callback (SurfaceAnimator.h:79-90).
            if (onComplete) {
                onComplete();
            }
            return;
        }
        cancelTrackingFor(surface, target);

        // Reuse stashed shader pieces from the previous leg if the
        // effect id, target, anchor, and shader item all still match
        // and are alive. Under rapid show/hide toggling (zone selector
        // during a drag), creating a fresh QQuickShaderEffectSource
        // (with its own FBO) plus PhosphorRendering::ShaderEffect (with
        // its own QSGRenderNode + ShaderNodeRhi) on every leg outpaces
        // deleteLater + render-thread cleanup, accumulating render
        // nodes that each consume a Qt RHI resource update batch from
        // the 64-batch global pool — the journal eventually fills with
        // "Resource update batch pool exhausted (max is 64)" and the
        // compositor stalls. Reuse skips the create-then-destroy-then-
        // recreate churn entirely: one ShaderEffect lives for the
        // surface's lifetime (or until the effect id changes), and
        // successive legs just reset iTime and the shaderTime AV.
        QPointer<PhosphorRendering::ShaderEffect> reusedShaderItem;
        QPointer<QQuickShaderEffectSource> reusedShaderSource;
        QPointer<QQuickItem> reusedShaderAnchor;
        bool reusedFoundExplicit = false;
        std::shared_ptr<PhosphorAnimationShaders::AnimationShaderEffect::FboExtentKind> reusedFboExtentKindPtr;
        if (hasShaderLeg) {
            const auto pendIt = m_pendingReuse.find(TrackKey{surface, target});
            if (pendIt != m_pendingReuse.end()) {
                PendingReuseShader& pending = pendIt->second;
                // Re-validate anchor against the current QML scene.
                // The shell's per-mode Loader unloads + loads its content
                // on `mode` / `loaded` flips (e.g. layout-osd ↔ navigation-
                // osd, snap-assist re-instantiate), destroying the
                // previously-found shaderAnchor descendant. The cached
                // QPointer auto-nulls when the underlying QObject has
                // been destroyed — `pending.shaderAnchor.data() == nullptr`
                // in that case, so the equality compare against
                // `currentAnchor` (which is the live anchor or null)
                // correctly rejects reuse and forces a fresh attach. Even
                // when QPointer hasn't yet cleared (immediately after
                // deleteLater queues but before the event loop runs), the
                // pointer would refer to the previous Loader generation's
                // anchor, not the freshly-loaded one — also a mismatch
                // against `currentAnchor` (which is the *new* anchor) and
                // also correctly rejected. Reuse only when the cache
                // resolves to the same QQuickItem that the live tree's
                // shaderAnchor walk finds (or both fall back to target).
                QQuickItem* currentAnchor = findShaderAnchorRecursive(target);
                const bool anchorMatches = pending.foundExplicitAnchor
                    ? (currentAnchor != nullptr && currentAnchor == pending.shaderAnchor.data())
                    : (currentAnchor == nullptr && pending.shaderAnchor.data() == target);
                if (pending.shaderItem && pending.shaderAnchor && pending.shaderEffectId == shaderEffectId
                    && pending.target.data() == target && anchorMatches) {
                    reusedShaderItem = std::move(pending.shaderItem);
                    reusedShaderSource = std::move(pending.shaderSource);
                    reusedShaderAnchor = pending.shaderAnchor;
                    reusedFoundExplicit = pending.foundExplicitAnchor;
                    reusedFboExtentKindPtr = std::move(pending.fboExtentKindPtr);
                    // Refresh the live fboExtentKind so the persistent
                    // syncGeometry lambda (still connected from the
                    // original attach) picks up the new metadata value
                    // on the next anchor geometry signal. Without this
                    // refresh, an in-place metadata.json hot-reload that
                    // flips `fboExtent` between Anchor and Surface
                    // leaves the lambda using the OLD kind when the
                    // anchor next moves/resizes.
                    if (reusedFboExtentKindPtr) {
                        *reusedFboExtentKindPtr = resolvedShaderEff.fboExtentKind;
                    }
                    m_pendingReuse.erase(pendIt);
                } else {
                    // Mismatch (different effect, QML scene rebuild
                    // swapped the anchor, or stale QPointers from item
                    // destruction) — drop ONLY this {surface, target}
                    // entry. Sibling slots on the same shell surface
                    // (OSD, zone-selector, etc.) keep their parked
                    // shaders intact; per-(Surface, target) keying is
                    // the whole point of TrackKey.
                    destroyPendingReuseEntry(pending);
                    m_pendingReuse.erase(pendIt);
                }
            }
        } else {
            // Non-shader leg following a shader leg on THIS target —
            // drop ONLY this target's stash. Other slots on the same
            // surface may have valid parked shaders that should
            // survive (per-(Surface, target) keying contract).
            destroyPendingReuseForKey(TrackKey{surface, target});
        }

        PhosphorAnimation::IMotionClock* clock = &m_clock;

        // When a shader leg is active, it IS the transition — suppress
        // the scale leg so the built-in popin motion doesn't fight the
        // shader's own visual. Matches the Niri model where custom
        // shaders replace the default animation, not layer on top.
        const bool hasScaleLeg = !scaleProfilePath.isEmpty() && !hasShaderLeg;
        const int legCount = 1 + (hasScaleLeg ? 1 : 0) + (hasShaderLeg ? 1 : 0);

        // Install the bookkeeping slot BEFORE the synchronous pre-
        // state writes below. setOpacity / setScale fire QML changed
        // signals synchronously, and a consumer binding can re-enter
        // cancel(surface). With the slot installed first, that cancel
        // erases cleanly (no AVs to park yet) and the post-write
        // re-find below detects the erasure and bails.
        {
            Track& slot = m_tracks[TrackKey{surface, target}];
            slot.opacity.reset();
            slot.scale.reset();
            // Reset shader-leg state explicitly — a non-shader leg
            // following a shader leg (e.g. show w/ pixelate → hide w/o)
            // would otherwise leave stale anchor/shaderItem QPointers
            // hanging around since teardownShaderLeg only runs when the
            // prior leg had hasShaderLeg.
            slot.shaderAnchor.clear();
            slot.shaderItem = nullptr;
            slot.shaderSource = nullptr;
            slot.hiddenSiblings.clear();
            slot.foundExplicitAnchor = false;
            slot.shaderEffectId.clear();
            slot.fboExtentKindPtr.reset();
            slot.target = target;
            slot.onComplete = std::move(onComplete);
            slot.pendingLegs = legCount;
            slot.shaderExclusive = hasShaderLeg;
            slot.targetOpacity = toOpacity;
        }

        // When a shader leg is active, the shader IS the entire
        // visual transition (Niri model). The surface must be fully
        // visible while the shader runs — show snaps to 1.0 so the
        // shader's first frames aren't invisible; hide keeps the
        // current opacity (shader drives perceived fade-out), then
        // snaps to 0.0 on shader completion (see legCompleted below).
        //
        // Scale must ALSO snap to 1.0: the scale leg is suppressed
        // when a shader is active (`hasScaleLeg` gates on `!hasShaderLeg`),
        // and a previous non-shader hide that ran a scale-down leg
        // leaves `target->scale()` at hideScaleTo. Without snapping,
        // the QML root carries that residual scale into the shader
        // leg and the entire subtree — including our shaderItem and
        // any redirected MultiEffect — renders at the reduced scale.
        // The user sees a correctly-rendered shader at the wrong
        // visual size and a "pop to full size" at teardown when the
        // next non-shader cycle restores scale via its own scale leg.
        if (hasShaderLeg) {
            const bool isShow = (toOpacity > fromOpacity);
            target->setOpacity(isShow ? toOpacity : fromOpacity);
            target->setScale(1.0);
        } else {
            target->setOpacity(fromOpacity);
        }
        if (hasScaleLeg) {
            target->setScale(fromScale);
        }

        // Construct AVs atomically BEFORE start() on either — a
        // synchronous onValueChanged that re-enters cancel(surface)
        // would otherwise erase the slot between assignments. Re-find
        // here too in case the setOpacity/setScale chain above just
        // re-entered cancel() — bail per the cancellation contract.
        {
            auto it = m_tracks.find(TrackKey{surface, target});
            if (it == m_tracks.end()) {
                return;
            }
            if (!hasShaderLeg) {
                it->second.opacity = std::make_unique<PhosphorAnimation::AnimatedValue<qreal>>();
            }
            if (hasScaleLeg) {
                it->second.scale = std::make_unique<PhosphorAnimation::AnimatedValue<qreal>>();
            }
            if (hasShaderLeg) {
                const bool isShowLeg = (toOpacity > fromOpacity);
                if (reusedShaderItem) {
                    // Reuse path: keep the existing ShaderEffect /
                    // ShaderEffectSource / ShaderNodeRhi tower across
                    // the leg boundary. The pieces were made dormant
                    // by teardownShaderLeg's park step (setVisible
                    // (false), setLive(false), setHideSource(false))
                    // to avoid per-frame RHI batch consumption during
                    // the idle phase. Wake them up here BEFORE setting
                    // any leg-specific state so the SG sync that
                    // follows runLeg picks them up live.
                    if (reusedShaderSource) {
                        // Wake the source FBO + re-hide the anchor from
                        // direct scene render. The park step in
                        // `teardownShaderLeg` calls
                        // `setHideSource(false)` to restore the anchor's
                        // normal rendering during the idle phase, and
                        // that toggle tears down the source item's
                        // internal `QQuickItemLayer`. A bare
                        // `setHideSource(true)` here doesn't reliably
                        // rebuild that layer — the FBO comes back live
                        // but empty, so the shader samples a
                        // transparent uTexture0 throughout the leg and
                        // the user sees a "pop in" at completion when
                        // the post-leg `setOpacity(1.0)` restores the
                        // anchor's direct render.
                        //
                        // Force a clean layer reattach by clearing the
                        // source item first, then re-pointing it at
                        // the anchor. Qt rebuilds the layer + FBO from
                        // scratch, mirroring the fresh-attach setup at
                        // `attachShaderToAnchor`. The matching order
                        // (setLive, setHideSource, setSourceItem) keeps
                        // a single code-path's worth of state machine
                        // for the source.
                        reusedShaderSource->setSourceItem(nullptr);
                        reusedShaderSource->setLive(true);
                        reusedShaderSource->setHideSource(true);
                        reusedShaderSource->setSourceItem(reusedShaderAnchor.data());
                    }
                    reusedShaderItem->setVisible(true);
                    // Re-apply per-effect static config so a metadata.json
                    // hot-reload that changed multipass / wallpaper / depth
                    // / vertex shader between legs propagates onto the
                    // reused shader item. Idempotent on identity (the
                    // ShaderEffect setters all no-op when the value is
                    // unchanged), so the common no-hot-reload case costs
                    // only a wallpaper-cache lookup.
                    applyEffectStaticConfig(reusedShaderItem.data(), resolvedShaderEff, animIncludePaths);
                    // Re-fire the geometry sync so a fboExtentKind
                    // hot-reload between legs (refreshed into the
                    // shared cell at the reuse-claim site above) takes
                    // effect immediately on the new leg's first frame.
                    // The persistent syncGeometry lambda only runs on
                    // anchor geometry signals; for static popups
                    // (snap-assist, OSD) those may never fire. Manually
                    // invoking the same logic here closes that gap.
                    syncShaderGeometryNow(reusedShaderAnchor.data(), reusedShaderItem.data(), reusedShaderSource.data(),
                                          reusedFboExtentKindPtr
                                              ? *reusedFboExtentKindPtr
                                              : PhosphorAnimationShaders::AnimationShaderEffect::FboExtentKind::Anchor);
                    // Reset iTime to the new leg's start so the first
                    // painted frame after the shaderTime AV starts
                    // matches the AnimatedValue's intended `from` —
                    // same defence as attachShaderToAnchor's setITime
                    // block. Without this the parked iTime (terminal
                    // value of the previous leg) leaks into the first
                    // frame of the new leg.
                    reusedShaderItem->setITime(isShowLeg ? qreal(0.0) : qreal(1.0));
                    // Direction signal for asymmetric shaders. The reuse
                    // path may swap a show leg into a hide leg (or vice
                    // versa) on the same shader item, so re-push every
                    // leg start.
                    reusedShaderItem->setIsReversed(!isShowLeg);
                    // Re-translate parameter overrides — shaderParameters
                    // can differ between legs (per-event customisation).
                    const QVariantMap translated =
                        PhosphorAnimationShaders::AnimationShaderRegistry::translateAnimationParams(resolvedShaderEff,
                                                                                                    shaderParameters);
                    if (!translated.isEmpty()) {
                        reusedShaderItem->setShaderParams(translated);
                    }
                    // Re-hide decorator siblings of the anchor — they
                    // were restored by teardownShaderLeg between legs
                    // so the static-Shown decoration was visible
                    // during the idle phase, but the new leg's shader
                    // needs them out of the way again. Same helper as
                    // the fresh-attach path uses.
                    QList<QPointer<QQuickItem>> hidden =
                        hideAnchorSiblings(reusedShaderAnchor.data(), reusedShaderItem.data(),
                                           reusedShaderSource.data(), reusedFoundExplicit);
                    it->second.shaderItem = reusedShaderItem;
                    it->second.shaderSource = reusedShaderSource;
                    it->second.shaderAnchor = reusedShaderAnchor;
                    it->second.foundExplicitAnchor = reusedFoundExplicit;
                    it->second.hiddenSiblings = std::move(hidden);
                    it->second.shaderEffectId = shaderEffectId;
                    it->second.fboExtentKindPtr = std::move(reusedFboExtentKindPtr);
                    it->second.shaderTime = std::make_unique<PhosphorAnimation::AnimatedValue<qreal>>();
                    seedShaderUniformsAtAttach(it->second);
                } else {
                    // Fresh attach: no prior compatible shader pair, or
                    // the previous one was for a different effect id.
                    // resolvedShaderEff already validated by hasShaderLeg.
                    // isShowLeg lets attachShaderToAnchor seed iTime to
                    // the leg's start value (0 for show, 1 for hide) so
                    // the very first paint after the QQuickShaderEffect
                    // is added to the scene matches the AnimatedValue's
                    // intended start — see attachShaderToAnchor's
                    // setITime block.
                    ShaderAttachResult attached = attachShaderToAnchor(target, resolvedShaderEff, shaderEffectId,
                                                                       shaderParameters, isShowLeg, animIncludePaths);
                    it->second.shaderItem = attached.shaderItem;
                    it->second.shaderSource = attached.shaderSource;
                    it->second.shaderAnchor = attached.shaderAnchor;
                    it->second.foundExplicitAnchor = attached.foundExplicitAnchor;
                    it->second.hiddenSiblings = std::move(attached.hiddenSiblings);
                    it->second.shaderEffectId = shaderEffectId;
                    it->second.fboExtentKindPtr = std::move(attached.fboExtentKindPtr);
                    it->second.shaderTime = std::make_unique<PhosphorAnimation::AnimatedValue<qreal>>();
                    seedShaderUniformsAtAttach(it->second);
                }
            }
        }

        // Capture-by-value of `surface` (raw pointer key, never deref'd)
        // and `this` (SurfaceAnimator outlives every Surface — daemon
        // owns both, dtor ordering enforces it) are safe inside the
        // callbacks. Re-find on every access between start()s so a
        // re-entrant cancel mid-flight is honoured.
        if (hasShaderLeg) {
            // Shader-exclusive mode: opacity was snapped to toOpacity
            // above. The opacity "leg" completes immediately — the
            // shader's iTime 0→1 drives the entire visual transition.
            legCompleted(surface, target);
        } else {
            auto it = m_tracks.find(TrackKey{surface, target});
            if (it == m_tracks.end() || !it->second.opacity) {
                return;
            }
            it->second.opacity->start(fromOpacity, toOpacity,
                                      buildSpec(
                                          resolveProfile(m_registry, opacityProfilePath), clock,
                                          /*onValueChanged=*/
                                          [this, surface, target](const qreal& v) {
                                              auto sit = m_tracks.find(TrackKey{surface, target});
                                              if (sit == m_tracks.end() || !sit->second.target) {
                                                  return;
                                              }
                                              sit->second.target->setOpacity(v);
                                          },
                                          /*onComplete=*/
                                          [this, surface, target]() {
                                              legCompleted(surface, target);
                                          }));
        }

        if (hasScaleLeg) {
            auto it = m_tracks.find(TrackKey{surface, target});
            if (it == m_tracks.end() || !it->second.scale) {
                // The opacity leg's start() (or a synchronous callback)
                // cancelled the entry; let the cancellation stand.
                return;
            }
            it->second.scale->start(fromScale, toScale,
                                    buildSpec(
                                        resolveProfile(m_registry, scaleProfilePath), clock,
                                        /*onValueChanged=*/
                                        [this, surface, target](const qreal& v) {
                                            auto sit = m_tracks.find(TrackKey{surface, target});
                                            if (sit == m_tracks.end() || !sit->second.target) {
                                                return;
                                            }
                                            sit->second.target->setScale(v);
                                        },
                                        /*onComplete=*/
                                        [this, surface, target]() {
                                            legCompleted(surface, target);
                                        }));
        }

        if (hasShaderLeg) {
            auto it = m_tracks.find(TrackKey{surface, target});
            if (it == m_tracks.end() || !it->second.shaderTime || !it->second.shaderItem) {
                return;
            }
            const QString shaderLegProfilePath = shaderProfilePath.isEmpty() ? opacityProfilePath : shaderProfilePath;
            if (shaderLegProfilePath.isEmpty()) {
                qCDebug(lcSurfaceAnimator)
                    << "shader leg falling back to library defaults (150ms OutCubic) — both shader and opacity "
                       "profile paths are empty for surface"
                    << surface;
            }
            // Run iTime backward on hide so shaders whose visual depends on
            // direction (pixelate dissipating, popin scaling up, slide
            // revealing, dissolve filling in) play in reverse for the hide
            // leg without each shader having to know the leg sign. iTime
            // semantics in the GLSL stay the same — iTime=0 is the
            // "transition start" state (pixelated / small / slid-out /
            // dissolved), iTime=1 is the "fully resolved" state (clear /
            // full size / fully revealed / opaque). On show we tween 0→1;
            // on hide we tween 1→0 so the surface visibly transitions
            // OUT through the same intermediate states.
            //
            // Symmetric envelopes (morph's `sin(iTime*pi)`, glitch's
            // sin-shaped peak) are direction-invariant by construction —
            // the peak still lands mid-leg either way.
            const bool isShowLegInner = (toOpacity > fromOpacity);
            const qreal shaderFrom = isShowLegInner ? qreal(0.0) : qreal(1.0);
            const qreal shaderTo = isShowLegInner ? qreal(1.0) : qreal(0.0);
            // No setITime here — attachShaderToAnchor (called with the
            // same isShowLeg flag) already seeded iTime to shaderFrom
            // before the QQuickShaderEffect could paint a frame.
            const PhosphorAnimation::Profile shaderLegProfile = resolveProfile(m_registry, shaderLegProfilePath);
            qCDebug(lcSurfaceAnimator).nospace()
                << "shader leg start: path=" << shaderLegProfilePath
                << " duration=" << shaderLegProfile.effectiveDuration() << "ms surface=" << surface;
            it->second.shaderTime->start(shaderFrom, shaderTo,
                                         buildSpec(
                                             shaderLegProfile, clock,
                                             /*onValueChanged=*/
                                             [this, surface, target](const qreal& v) {
                                                 auto sit = m_tracks.find(TrackKey{surface, target});
                                                 // shaderItem QPointer auto-nulls if the
                                                 // ShaderEffect's parent (the anchor) was torn
                                                 // down between ticks — bail rather than UAF.
                                                 if (sit == m_tracks.end() || !sit->second.shaderItem) {
                                                     return;
                                                 }
                                                 // Clamp iTime to the [0, 1] contract before
                                                 // forwarding. Underdamped curves (Spring with
                                                 // zeta < 1, e.g. `bouncy`) overshoot the
                                                 // target — show legs push the lerp above 1.0
                                                 // and hide legs push it below 0.0 at each
                                                 // oscillation peak. Shaders treat iTime
                                                 // outside [0, 1] as undefined per
                                                 // AnimationShaderContract; without clamping,
                                                 // a hide leg flicks iTime negative which most
                                                 // shaders render as "more than fully gone"
                                                 // (transparent/invisible) before the spring
                                                 // oscillates back to a slightly positive
                                                 // value, producing a visible disappear→re-
                                                 // render→disappear flicker. Mirror what the
                                                 // kwin-effect path already does for window
                                                 // shader transitions (paint_pipeline.cpp's
                                                 // `qBound(0.0, anim->state().value, 1.0)`).
                                                 // Geometry sync runs off the anchor's
                                                 // widthChanged/heightChanged signals (see
                                                 // syncGeometry); the per-tick callback only
                                                 // threads the time value through.
                                                 sit->second.shaderItem->setITime(qBound(qreal(0.0), v, qreal(1.0)));
                                             },
                                             /*onComplete=*/
                                             [this, surface, target]() {
                                                 legCompleted(surface, target);
                                             }));
        }

        // Kick the driver — ensureDriving is idempotent. Skip if a
        // re-entrant cancel already drained the map.
        if (!m_tracks.empty()) {
            ensureDriving();
        }
    }

    /// Decrement pendingLegs; on the final settle, fire the consumer's
    /// onComplete and retire the entry. Idempotent against a missing
    /// entry. AVs are parked in m_pendingDestroy (deferred destroy)
    /// because legCompleted runs from inside the spec.onComplete of
    /// the very AV — AnimatedValue.h:547 forbids destroying *this from
    /// within a spec callback.
    void legCompleted(PhosphorLayer::Surface* surface, QQuickItem* target)
    {
        const auto it = m_tracks.find(TrackKey{surface, target});
        if (it == m_tracks.end()) {
            return;
        }
        --it->second.pendingLegs;
        if (it->second.pendingLegs > 0) {
            return;
        }
        // Shader-exclusive hide: the surface stayed at full opacity while
        // the shader ran. Now snap to the terminal opacity (0.0 for hide)
        // so the surface actually disappears.
        if (it->second.shaderExclusive && it->second.target) {
            it->second.target->setOpacity(it->second.targetOpacity);
        }

        // Move the callback + AnimatedValues out before erase so:
        //   1. m_tracks.erase doesn't drop the std::function we're
        //      about to invoke;
        //   2. m_tracks.erase doesn't destroy the AnimatedValue whose
        //      onComplete we're currently inside (deferred to graveyard).
        auto onComplete = std::move(it->second.onComplete);
        if (it->second.opacity) {
            m_pendingDestroy.push_back(std::move(it->second.opacity));
        }
        if (it->second.scale) {
            m_pendingDestroy.push_back(std::move(it->second.scale));
        }
        teardownShaderLeg(surface, it->second);
        m_tracks.erase(it);
        if (onComplete) {
            onComplete();
        }
    }

    /// Drive every active animation by one tick. By-key snapshot
    /// iteration is safe against re-entrant erase from legCompleted.
    void tickAll()
    {
        // Drain the graveyard — AVs parked here last tick are now safe
        // to destroy (the spec.onComplete advance() frame has unwound).
        m_pendingDestroy.clear();

        // Sweep the reuse cache for stale entries — surfaces whose
        // QPointers all auto-nulled because the QML scene tore the
        // shader pieces down independently of our cancel paths
        // (consumer-driven anchor reparent, Loader unload outside the
        // animator's notice). The destroyed-signal cleanup catches
        // most cases, but it fires on Surface destruction, not on
        // shaderItem/Source/Anchor destruction in isolation. Without
        // this sweep, a surface that survives but loses its anchor
        // sits with a dead reuse entry indefinitely; the next leg's
        // reuse-mismatch path destroys it then, but the entry stays
        // wasting a hash slot until then.
        for (auto it = m_pendingReuse.begin(); it != m_pendingReuse.end();) {
            const PendingReuseShader& p = it->second;
            if (!p.shaderItem && !p.shaderSource && !p.shaderAnchor) {
                it = m_pendingReuse.erase(it);
            } else {
                ++it;
            }
        }

        // Snapshot — legCompleted may erase from m_tracks while iterating.
        std::vector<TrackKey> keys;
        keys.reserve(m_tracks.size());
        for (auto& [k, _] : m_tracks) {
            keys.push_back(k);
        }
        // Real-time delta for shader iTimeDelta — measured between
        // SurfaceAnimator ticks (which fire at kTickIntervalMs cadence
        // while any track is in flight). First tick after a quiescent
        // period reports 0 instead of a stale wall-clock gap, matching
        // OverlayService::updateShaderUniforms's clamp-on-resume idiom
        // for the overlay path. Capped at the shared
        // PhosphorAnimation::Limits::MaxShaderTimeDeltaSeconds (100 ms)
        // so a sleep/resume hiccup doesn't push a multi-second jump
        // into the shader. Both runtimes reference the same constant —
        // bumping one without the other was the prior drift risk.
        const qreal kMaxShaderDeltaSecs = static_cast<qreal>(PhosphorAnimation::Limits::MaxShaderTimeDeltaSeconds);
        const qint64 nowNs = m_clock.now().count();
        qreal shaderDeltaSecs = 0.0;
        if (m_lastShaderTickNs > 0) {
            shaderDeltaSecs = qBound(qreal(0.0), qreal(nowNs - m_lastShaderTickNs) / 1.0e9, kMaxShaderDeltaSecs);
        }
        m_lastShaderTickNs = nowNs;

        for (const auto& k : keys) {
            auto it = m_tracks.find(k);
            if (it == m_tracks.end()) {
                continue;
            }
            if (it->second.opacity) {
                it->second.opacity->advance();
            }
            // Re-find: advance may have completed + erased via legCompleted.
            it = m_tracks.find(k);
            if (it == m_tracks.end()) {
                continue;
            }
            if (it->second.scale) {
                it->second.scale->advance();
            }
            it = m_tracks.find(k);
            if (it == m_tracks.end()) {
                continue;
            }
            if (it->second.shaderTime) {
                it->second.shaderTime->advance();
            }
            // Re-find again: shaderTime->advance may have completed +
            // erased via legCompleted. Then push per-frame dynamic
            // uniforms (iTimeDelta, iFrame, iMouse, audio spectrum)
            // so the next paint sees the latest values. Cheap on
            // identity (each setter early-returns when unchanged).
            it = m_tracks.find(k);
            if (it == m_tracks.end()) {
                continue;
            }
            if (it->second.shaderItem) {
                pushDynamicShaderUniforms(it->second, shaderDeltaSecs);
            }
        }
        // Stop the driver if every track completed during this tick.
        if (m_tracks.empty() && m_driverTimer.isActive()) {
            m_driverTimer.stop();
            // Reset the tick-time anchor so the first tick after the
            // next ensureDriving() reports 0 delta instead of the
            // wall-clock gap accrued while the driver was idle.
            m_lastShaderTickNs = 0;
        }
    }

    /// Push per-frame dynamic shader uniforms onto an active track's
    /// shader item. Mirrors the overlay path's per-frame
    /// `OverlayService::updateShaderUniforms` writes for `iTimeDelta`
    /// and `iFrame`, plus the on-update CAVA `audioSpectrum` dispatch —
    /// animation shaders attached programmatically by `SurfaceAnimator`
    /// have no QML scene to bind through, so the animator pumps these
    /// directly each tick. Cheap on identity: every `ShaderEffect`
    /// setter early-returns on unchanged input.
    ///
    /// Note: `iMouse` is NOT pumped here — it's auto-driven by the
    /// per-shader-item `QQuickHoverHandler` installed in
    /// `attachShaderToAnchor`, mirroring the overlay path's QML-side
    /// `MouseArea`/`HoverHandler` wiring exactly.
    void pushDynamicShaderUniforms(Track& track, qreal deltaSecs)
    {
        if (!track.shaderItem) {
            return;
        }
        track.shaderItem->setITimeDelta(deltaSecs);
        // Post-increment so the FIRST push after attach reports
        // iFrame=0, matching overlay's `m_frameCount.fetch_add(1)`
        // post-increment semantics.
        track.shaderItem->setIFrame(track.shaderFrameCount++);
        if (!m_audioSpectrum.isEmpty()) {
            track.shaderItem->setAudioSpectrum(m_audioSpectrum);
        }
    }

    /// Seed the dynamic uniforms (audio spectrum, iMouse) onto a freshly
    /// attached or reused shader item BEFORE its first paint, so the
    /// initial frame doesn't render silent or with stale cursor data
    /// when the consumer already has audio flowing or the shader is
    /// being reused after a parked idle phase. Resets the per-leg
    /// `iFrame` counter to 0 — a reused shader item starts a new leg's
    /// frame counter from scratch even though the underlying
    /// QQuickShaderEffect is the same instance.
    ///
    /// `iMouse` is seeded by querying the persistent
    /// `QQuickHoverHandler` installed by `attachShaderToAnchor`. On
    /// fresh attach the handler reports `isHovered()` false until Qt
    /// delivers the first hover event, so the seed lands `(-1, -1)` —
    /// matching the GLSL contract's off-region sentinel. On reuse the
    /// handler reflects the live cursor state at wake time, which
    /// prevents a stale value from the previous leg (set while the
    /// item was parked invisible) from leaking into the new leg's
    /// first frame.
    void seedShaderUniformsAtAttach(Track& track)
    {
        track.shaderFrameCount = 0;
        if (!track.shaderItem) {
            return;
        }
        if (!m_audioSpectrum.isEmpty()) {
            track.shaderItem->setAudioSpectrum(m_audioSpectrum);
        }
        // FindDirectChildrenOnly: the hover handler is parented
        // directly to the shaderItem in attachShaderToAnchor; a
        // recursive search could pick up an unrelated handler from a
        // QQuickItem subtree (e.g. a future scene-graph internal that
        // installs its own handler) and seed iMouse from the wrong
        // source.
        QQuickHoverHandler* hover =
            track.shaderItem->findChild<QQuickHoverHandler*>(QString{}, Qt::FindDirectChildrenOnly);
        if (hover && hover->isHovered()) {
            track.shaderItem->setIMouse(hover->point().position());
        } else {
            track.shaderItem->setIMouse(QPointF(-1.0, -1.0));
        }
    }

    /// Ensure the driver timer is running. Idempotent.
    void ensureDriving()
    {
        if (!m_driverTimer.isActive()) {
            m_driverTimer.start();
        }
    }

    PhosphorAnimation::PhosphorProfileRegistry& m_registry;
    PhosphorAnimationShaders::AnimationShaderRegistry* m_shaderRegistry = nullptr;
    Config m_defaultConfig;
    /// Global animation enable. Mirrors `Settings::animationsEnabled`
    /// — flipped via `SurfaceAnimator::setEnabled`. When false,
    /// `beginShow`/`beginHide` snap to the target opacity and fire
    /// completion synchronously, skipping the opacity / scale / shader
    /// legs entirely. Default true so an animator built without
    /// explicit setEnabled honors the historic always-on behaviour.
    bool m_enabled = true;
    /// Latest CAVA / audio-spectrum sample fed by the consumer via
    /// `SurfaceAnimator::setAudioSpectrum`. Pushed verbatim to every
    /// active animation shader item per-tick (see
    /// `pushDynamicShaderUniforms`) and at attach time (see
    /// `seedShaderUniformsAtAttach`). Empty = no audio data yet.
    QVector<float> m_audioSpectrum;
    /// Steady-clock-ns timestamp of the last `tickAll` invocation.
    /// Used to compute real-time `iTimeDelta` for active shader items.
    /// Reset to 0 when the driver stops so the first tick after the
    /// next `ensureDriving()` reports 0 delta instead of the
    /// wall-clock gap accrued while idle.
    qint64 m_lastShaderTickNs = 0;
    /// Keyed on `Role::scopePrefix`. Lookup (configFor) is longest-
    /// prefix-match — a linear O(N) scan, which is fine for the handful
    /// of roles consumers register.
    QHash<QString, Config> m_configByRole;
    /// Steady-clock-backed clock shared across every track. MUST be
    /// declared BEFORE m_tracks so reverse-declaration destruction
    /// order tears down the AVs (held by non-owning pointer to this
    /// clock) before the clock disappears.
    internal::SteadyClock m_clock;
    /// Tracks only in-flight animations; missing entry means "no
    /// active animation". std::unordered_map (not QHash) because
    /// Track is move-only and QHash still requires copy-constructible
    /// values in Qt6.
    std::unordered_map<TrackKey, Track, TrackKeyHash> m_tracks;
    /// Per-(surface, target) stash for ShaderEffect/Source pieces between
    /// legs. Populated by teardownShaderLeg, drained on reuse-claim in
    /// runLeg or on surface destruction (connectSurfaceCleanup wires
    /// the destroyed signal). Keeps one shader render-tower alive for
    /// the (surface, target) pair's lifetime instead of the
    /// create-then-destroy churn that used to leak Qt RHI resource
    /// update batches under rapid show/hide toggling.
    std::unordered_map<TrackKey, PendingReuseShader, TrackKeyHash> m_pendingReuse;
    /// Per-surface destroyed-signal connections, keyed by raw Surface*
    /// pointer. Replaces the earlier `p_surfaceAnimatorCleanupConnected`
    /// dynamic-property gate which leaked across animator instances —
    /// a fresh animator that re-encountered the same Surface would skip
    /// wiring its own cleanup, leaking its m_pendingReuse entries on
    /// surface destruction. Per-instance tracking ensures each animator
    /// installs (and on its own dtor disconnects via m_driverTimer's
    /// auto-disconnect) exactly one slot per surface. Map entries are
    /// removed when the surface dies (slot self-removes its own key).
    QHash<PhosphorLayer::Surface*, QMetaObject::Connection> m_destroyedConnections;
    /// Graveyard for AVs whose final tick fired legCompleted from
    /// inside their own spec.onComplete (AnimatedValue.h:547 forbids
    /// destroying *this from a spec callback). Drained at the start
    /// of the next tickAll. MUST be declared after m_clock and
    /// before m_driverTimer so the destruction order is sound.
    std::vector<std::unique_ptr<PhosphorAnimation::AnimatedValue<qreal>>> m_pendingDestroy;
    /// Drives advance() at ~60Hz while any track is in flight.
    /// Declared LAST so the timer stops BEFORE m_tracks tears down on
    /// dtor — otherwise a final tick could fire after the tracks
    /// have been freed.
    QTimer m_driverTimer;
};

// ══════════════════════════════════════════════════════════════════════════
// SurfaceAnimator
// ══════════════════════════════════════════════════════════════════════════

SurfaceAnimator::SurfaceAnimator(PhosphorAnimation::PhosphorProfileRegistry& registry, Config defaults)
    : SurfaceAnimator(registry, nullptr, std::move(defaults))
{
}

SurfaceAnimator::SurfaceAnimator(PhosphorAnimation::PhosphorProfileRegistry& registry,
                                 PhosphorAnimationShaders::AnimationShaderRegistry* shaderRegistry, Config defaults)
    : d(std::make_unique<Private>(registry, shaderRegistry, std::move(defaults)))
{
}

SurfaceAnimator::SurfaceAnimator(PhosphorAnimation::PhosphorProfileRegistry& registry)
    : SurfaceAnimator(registry, Config{})
{
}

SurfaceAnimator::~SurfaceAnimator()
{
    // Stop the timer FIRST so no tick races with the teardown below.
    d->m_driverTimer.stop();

    // Move-out for stable iteration (cancelTracking erases from
    // m_tracks; iterating + erase on unordered_map is fragile).
    // Each onComplete is dropped per the cancellation contract.
    //
    // Skip the park-then-drain dance teardownShaderLeg would do (move
    // shader pieces to m_pendingReuse, then immediately drain it on
    // the next loop): the animator is dying, no future runLeg will
    // reclaim, and parking would also re-fire connectSurfaceCleanup
    // for surfaces whose Qt destroyed-signal connection is already
    // tracked in m_destroyedConnections. Build a transient
    // PendingReuseShader on the stack and route through
    // destroyPendingReuseEntry directly so the deleteLater path is
    // identical to the normal teardown.
    auto leftovers = std::move(d->m_tracks);
    d->m_tracks.clear();
    for (auto& [key, track] : leftovers) {
        (void)key;
        if (track.opacity) {
            track.opacity->cancel();
            d->m_pendingDestroy.push_back(std::move(track.opacity));
        }
        if (track.scale) {
            track.scale->cancel();
            d->m_pendingDestroy.push_back(std::move(track.scale));
        }
        if (track.shaderTime) {
            track.shaderTime->cancel();
            d->m_pendingDestroy.push_back(std::move(track.shaderTime));
        }
        for (const QPointer<QQuickItem>& sibling : track.hiddenSiblings) {
            if (sibling) {
                sibling->setVisible(true);
            }
        }
        if (track.shaderItem) {
            Private::PendingReuseShader transient;
            transient.shaderItem = std::move(track.shaderItem);
            transient.shaderSource = std::move(track.shaderSource);
            transient.shaderAnchor = track.shaderAnchor;
            d->destroyPendingReuseEntry(transient);
        }
    }

    // Drain the reuse stash. Animator is dying; no future runLeg will
    // reclaim. destroyPendingReuseEntry calls deleteLater on each
    // shader item / source so QObject's deferred-delete event-loop
    // catches them rather than leaking past dtor.
    auto reuseLeftovers = std::move(d->m_pendingReuse);
    d->m_pendingReuse.clear();
    for (auto& [_, pending] : reuseLeftovers) {
        d->destroyPendingReuseEntry(pending);
    }

    // Drain the graveyard explicitly — defence-in-depth on top of
    // member-declaration order. Keeps the invariant local to the dtor
    // so a future maintainer who reorders members can't UAF on
    // shutdown.
    d->m_pendingDestroy.clear();
}

void SurfaceAnimator::registerConfigForRole(const PhosphorLayer::Role& role, Config cfg)
{
    d->m_configByRole.insert(role.scopePrefix, std::move(cfg));
}

SurfaceAnimator::Config SurfaceAnimator::configForRole(const PhosphorLayer::Role& role) const
{
    return d->configFor(role);
}

void SurfaceAnimator::setDefaultConfig(Config cfg)
{
    d->m_defaultConfig = std::move(cfg);
}

SurfaceAnimator::Config SurfaceAnimator::defaultConfig() const
{
    return d->m_defaultConfig;
}

void SurfaceAnimator::setAnimationShaderRegistry(PhosphorAnimationShaders::AnimationShaderRegistry* registry)
{
    d->m_shaderRegistry = registry;
}

void SurfaceAnimator::setAudioSpectrum(const QVector<float>& spectrum)
{
    // Cache for newly-attached shader items (seedShaderUniformsAtAttach
    // reads this field) AND push immediately to every active item so
    // the next paint frame picks it up — same eager-dispatch contract
    // as OverlayService::onAudioSpectrumUpdated → writeQmlProperty
    // for the overlay path.
    d->m_audioSpectrum = spectrum;
    for (auto& [_, track] : d->m_tracks) {
        if (track.shaderItem) {
            track.shaderItem->setAudioSpectrum(spectrum);
        }
    }
}

void SurfaceAnimator::beginShow(PhosphorLayer::Surface* surface, QQuickItem* rootItem, CompletionCallback onComplete)
{
    if (!surface) {
        if (onComplete) {
            onComplete();
        }
        return;
    }
    // Global animations toggle off — snap directly to the visible end
    // state and fire completion synchronously. Same contract as the
    // kwin-effect's `m_windowAnimator->isEnabled()` gate; both runtimes
    // honour `Settings::animationsEnabled` identically. Cancel any
    // in-flight track first so a rapid toggle mid-animation can't leave
    // the surface stuck at intermediate opacity.
    if (!d->m_enabled) {
        if (!rootItem) {
            qCWarning(lcSurfaceAnimator)
                << "beginShow on null rootItem with gate off; surface visibility unchanged but onComplete will fire";
        }
        // Salvage any in-flight previous-track onComplete BEFORE
        // cancelTracking drops it. The bare cancellation contract
        // (legCompleted-only firing) is correct for `cancel()` calls,
        // but the gate-off short-circuit here also fires the NEW
        // caller's onComplete synchronously below — leaving the
        // previous caller's callback unfired strands them while a
        // sibling caller gets serviced.
        //
        // Dispatch order: the salvaged previous-track onComplete fires
        // SYNCHRONOUSLY before cancelTracking, then cancelTracking
        // erases the track, then the new caller's onComplete runs
        // synchronously below. Natural completion order (oldest-first);
        // consumers can safely call back into SurfaceAnimator from
        // either callback since neither runs from a dtor — re-entry
        // touches a fully-constructed `*d`. cancelTracking itself is
        // synchronous and does not re-touch `m_tracks` after erasing
        // the entry, so the salvaged callback's potential re-entry
        // (which may mutate `m_tracks`) is bounded: the callback runs
        // AFTER cancelTracking has already erased our `surface` entry,
        // so any new entry the callback inserts is its own to manage.
        if (const auto it = d->m_tracks.find(Private::TrackKey{surface, rootItem}); it != d->m_tracks.end()) {
            if (auto prevOnComplete = std::move(it->second.onComplete)) {
                auto cb = std::move(prevOnComplete);
                d->cancelTrackingFor(surface, rootItem);
                cb();
            } else {
                d->cancelTrackingFor(surface, rootItem);
            }
        } else {
            d->cancelTrackingFor(surface, rootItem);
        }
        if (rootItem) {
            rootItem->setOpacity(1.0);
            rootItem->setScale(1.0);
        }
        if (onComplete) {
            onComplete();
        }
        return;
    }
    const Config cfg = d->configFor(surface->config().role);

    // Always start from fully transparent. Picking up from a mid-hide
    // opacity value causes a ghost frame: the surface is visible at
    // partial opacity with stale geometry while the compositor processes
    // the new layer-shell configure. Starting from 0.0 is visually
    // indistinguishable for the typical 150ms animation and eliminates
    // the flash on rapid re-show (e.g. layout switch during OSD fade-out).
    const qreal fromOpacity = 0.0;

    const qreal toScale = 1.0;
    qreal fromScale = 1.0;
    if (!cfg.showScaleProfile.isEmpty()) {
        const qreal liveScale = rootItem ? rootItem->scale() : 1.0;
        fromScale = (liveScale < toScale) ? liveScale : cfg.showScaleFrom;
    }

    d->runLeg(surface, rootItem, fromOpacity, /*toOpacity=*/1.0, cfg.showProfile, fromScale, toScale,
              cfg.showScaleProfile, cfg.showShaderEffectId, cfg.showShaderProfile, cfg.showShaderParameters,
              std::move(onComplete));
}

void SurfaceAnimator::beginHide(PhosphorLayer::Surface* surface, QQuickItem* rootItem, CompletionCallback onComplete)
{
    if (!surface) {
        if (onComplete) {
            onComplete();
        }
        return;
    }
    if (!d->m_enabled) {
        if (!rootItem) {
            qCWarning(lcSurfaceAnimator)
                << "beginHide on null rootItem with gate off; surface visibility unchanged but onComplete will fire";
        }
        // Salvage previous-track onComplete before cancelTracking drops it.
        // See beginShow's matching block for the rationale (gate-off path
        // fires the new caller's onComplete synchronously below — silently
        // dropping the in-flight caller's callback is the bug, not the
        // cancellation contract).
        //
        // Dispatch order: the salvaged previous-track onComplete fires
        // SYNCHRONOUSLY before cancelTracking, then cancelTracking
        // erases the track, then the new caller's onComplete runs
        // synchronously below. Natural completion order (oldest-first);
        // consumers can safely call back into SurfaceAnimator from
        // either callback since neither runs from a dtor — re-entry
        // touches a fully-constructed `*d`. cancelTracking is fully
        // synchronous and does not re-touch `m_tracks` after erasing
        // the entry, so the salvaged callback's potential re-entry
        // (which may mutate `m_tracks`) is bounded: the callback runs
        // AFTER cancelTracking has already erased our `surface` entry,
        // so any new entry the callback inserts is its own to manage.
        if (const auto it = d->m_tracks.find(Private::TrackKey{surface, rootItem}); it != d->m_tracks.end()) {
            if (auto prevOnComplete = std::move(it->second.onComplete)) {
                auto cb = std::move(prevOnComplete);
                d->cancelTrackingFor(surface, rootItem);
                cb();
            } else {
                d->cancelTrackingFor(surface, rootItem);
            }
        } else {
            d->cancelTrackingFor(surface, rootItem);
        }
        if (rootItem) {
            rootItem->setOpacity(0.0);
            rootItem->setScale(1.0);
        }
        if (onComplete) {
            onComplete();
        }
        return;
    }
    const Config cfg = d->configFor(surface->config().role);
    // Read live opacity so hide-while-showing supersession picks up
    // from the current visible state, not from a hardcoded 1.0.
    const qreal fromOpacity = rootItem ? rootItem->opacity() : 1.0;
    const qreal fromScale = (cfg.hideScaleProfile.isEmpty() || !rootItem) ? 1.0 : rootItem->scale();
    const qreal toScale = cfg.hideScaleProfile.isEmpty() ? 1.0 : cfg.hideScaleTo;
    d->runLeg(surface, rootItem, fromOpacity, /*toOpacity=*/0.0, cfg.hideProfile, fromScale, toScale,
              cfg.hideScaleProfile, cfg.hideShaderEffectId, cfg.hideShaderProfile, cfg.hideShaderParameters,
              std::move(onComplete));
}

void SurfaceAnimator::cancel(PhosphorLayer::Surface* surface)
{
    if (!surface) {
        return;
    }
    d->cancelAllForSurface(surface);
}

void SurfaceAnimator::beginShow(PhosphorLayer::Surface* surface, QQuickItem* rootItem,
                                const PhosphorLayer::Role& configRole, CompletionCallback onComplete)
{
    // Same contract as the no-arg beginShow but the config is resolved
    // from `configRole` instead of `surface->config().role`. Used by
    // the unified PassiveOverlayShell pattern where the surface's role
    // is the shell's (PassiveShell) and per-content motion/shader
    // configs would otherwise homogenise across content types.
    if (!surface) {
        if (onComplete) {
            onComplete();
        }
        return;
    }
    if (!d->m_enabled) {
        if (!rootItem) {
            qCWarning(lcSurfaceAnimator)
                << "beginShow on null rootItem with gate off; surface visibility unchanged but onComplete will fire";
        }
        if (const auto it = d->m_tracks.find(Private::TrackKey{surface, rootItem}); it != d->m_tracks.end()) {
            if (auto prevOnComplete = std::move(it->second.onComplete)) {
                auto cb = std::move(prevOnComplete);
                d->cancelTrackingFor(surface, rootItem);
                cb();
            } else {
                d->cancelTrackingFor(surface, rootItem);
            }
        } else {
            d->cancelTrackingFor(surface, rootItem);
        }
        if (rootItem) {
            rootItem->setOpacity(1.0);
            rootItem->setScale(1.0);
        }
        if (onComplete) {
            onComplete();
        }
        return;
    }
    const Config cfg = d->configFor(configRole);
    const qreal fromOpacity = 0.0;
    const qreal toScale = 1.0;
    qreal fromScale = 1.0;
    if (!cfg.showScaleProfile.isEmpty()) {
        const qreal liveScale = rootItem ? rootItem->scale() : 1.0;
        fromScale = (liveScale < toScale) ? liveScale : cfg.showScaleFrom;
    }
    d->runLeg(surface, rootItem, fromOpacity, /*toOpacity=*/1.0, cfg.showProfile, fromScale, toScale,
              cfg.showScaleProfile, cfg.showShaderEffectId, cfg.showShaderProfile, cfg.showShaderParameters,
              std::move(onComplete));
}

void SurfaceAnimator::beginHide(PhosphorLayer::Surface* surface, QQuickItem* rootItem,
                                const PhosphorLayer::Role& configRole, CompletionCallback onComplete)
{
    if (!surface) {
        if (onComplete) {
            onComplete();
        }
        return;
    }
    if (!d->m_enabled) {
        if (!rootItem) {
            qCWarning(lcSurfaceAnimator)
                << "beginHide on null rootItem with gate off; surface visibility unchanged but onComplete will fire";
        }
        if (const auto it = d->m_tracks.find(Private::TrackKey{surface, rootItem}); it != d->m_tracks.end()) {
            if (auto prevOnComplete = std::move(it->second.onComplete)) {
                auto cb = std::move(prevOnComplete);
                d->cancelTrackingFor(surface, rootItem);
                cb();
            } else {
                d->cancelTrackingFor(surface, rootItem);
            }
        } else {
            d->cancelTrackingFor(surface, rootItem);
        }
        if (rootItem) {
            rootItem->setOpacity(0.0);
            rootItem->setScale(1.0);
        }
        if (onComplete) {
            onComplete();
        }
        return;
    }
    const Config cfg = d->configFor(configRole);
    const qreal fromOpacity = rootItem ? rootItem->opacity() : 1.0;
    const qreal fromScale = (cfg.hideScaleProfile.isEmpty() || !rootItem) ? 1.0 : rootItem->scale();
    const qreal toScale = cfg.hideScaleProfile.isEmpty() ? 1.0 : cfg.hideScaleTo;
    d->runLeg(surface, rootItem, fromOpacity, /*toOpacity=*/0.0, cfg.hideProfile, fromScale, toScale,
              cfg.hideScaleProfile, cfg.hideShaderEffectId, cfg.hideShaderProfile, cfg.hideShaderParameters,
              std::move(onComplete));
}

void SurfaceAnimator::setEnabled(bool enabled)
{
    // Skip the assignment when the gate value hasn't changed — settings
    // sweeps re-push every gate on every save, and there's no point
    // touching the member for a no-op write.
    if (d->m_enabled == enabled) {
        return;
    }
    d->m_enabled = enabled;
    // Gate semantics are "next dispatch", not "kill in progress" — the
    // toggle itself does not touch any tracks. A track that is currently
    // mid-leg will continue to its natural `legCompleted` (firing its
    // stored onComplete normally) ONLY if no subsequent
    // `beginShow` / `beginHide` arrives in the meantime. If a new
    // dispatch does arrive while the gate is off, that dispatch
    // short-circuits via the gate-off branch in beginShow/beginHide,
    // which calls `cancelTracking` on the in-flight track. Per the
    // cancellation contract documented on `cancelTracking`,
    // legCompleted does not fire on cancellation, so the in-flight
    // track's onComplete would be dropped — the gate-off branch
    // therefore salvages that callback and dispatches it synchronously
    // (after cancelTracking has already erased the entry) before
    // running the new caller's onComplete, so callers are not stranded
    // when their in-flight animation is preempted by a gate-off
    // dispatch.
    //
    // Parity with kwin-effect: `WindowAnimator::setEnabled` (see
    // kwin-effect/windowanimator.cpp) is also a pure flag flip; it does
    // not actively cancel in-flight window animations on the compositor
    // path. Both runtimes therefore share "next dispatch" semantics for
    // the global animation gate.
}

bool SurfaceAnimator::isEnabled() const
{
    return d->m_enabled;
}

} // namespace PhosphorAnimationLayer
