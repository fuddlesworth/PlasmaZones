// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/SurfaceAnimator.h>

#include <PhosphorAnimation/AnimatedValue.h>
#include <PhosphorAnimation/IMotionClock.h>
#include <PhosphorAnimation/MotionSpec.h>
#include <PhosphorAnimation/PhosphorProfileRegistry.h>
#include <PhosphorAnimation/Profile.h>

#include <PhosphorAnimation/AnimationShaderContract.h>
#include <PhosphorAnimation/AnimationShaderRegistry.h>
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

/// Driver tick interval. SteadyClock::refreshRate MUST agree so
/// velocity-based curves (Spring) sample at the timer's cadence.
constexpr int kTickIntervalMs = 16;
constexpr qreal kRefreshRateHz = 1000.0 / kTickIntervalMs;

/// Hard ceiling on `boundsPadding` after the parent-margin clamp.
/// Without this, an anchor centred in a much larger parent permits an
/// arbitrarily large pad (`min(left, right) / anchorW` could be e.g.
/// 50 for a tiny anchor in a 4K parent), which inflates the shader
/// item to (1+2*pad)² × anchor area — gigabytes of FBO at extreme
/// values. The metadata-side clamp at AnimationShaderEffect::fromJson
/// caps at 2.0; this is a runtime backstop against any path that
/// bypasses that clamp (test fixtures, future scripted-shader hooks).
// Must match the metadata clamp in AnimationShaderEffect::fromJson()
constexpr qreal kMaxBoundsPaddingFraction = 2.0;

/// Compute the effective boundsPadding fraction for a shader item
/// parented to @p anchor's parent. Result is clamped to:
///   1. Non-negative AND non-NaN (`qMax(0, ...)` propagates NaN, so an
///      explicit `qIsNaN` short-circuit is required — a NaN pad would
///      cascade into NaN geometry on the shader item and the SG
///      gracelessly skips painting).
///   2. The smallest margin between the anchor and its parent's bounds.
///      Without clamping, the shader item extends past the parent → the
///      framebuffer surface clips → QRhi viewport shrinks to the visible
///      subset → vTexCoord interpolates across the rasterized QUAD into
///      the CLIPPED viewport, shifting UV values relative to the item's
///      logical bounds. Morph then samples iChannel0 at wrong positions
///      ("rendered further down and smaller" symptom on tight popups).
///   3. `kMaxBoundsPaddingFraction` ceiling — stops a centred-in-huge-
///      parent anchor from asking for a multi-GB FBO.
///
/// Centralised here so the initial-attach and the live geometry-sync
/// path share one clamp implementation; previously the math was
/// duplicated and the two copies had drifted slightly.
inline qreal clampPaddingToParent(QQuickItem* anchor, qreal requestedPad)
{
    if (qIsNaN(requestedPad)) {
        return 0.0;
    }
    qreal pad = qMax(qreal(0.0), requestedPad);
    if (anchor) {
        if (QQuickItem* anchorParent = anchor->parentItem()) {
            const qreal parentW = anchorParent->width();
            const qreal parentH = anchorParent->height();
            const qreal anchorW = anchor->width();
            const qreal anchorH = anchor->height();
            // Reject NaN derived from anchor/parent geometry too — the
            // input-only NaN guard above doesn't catch the case where
            // anchor->y() is NaN (anchor mid-recompute, layout binding
            // partially evaluated), which would propagate through qMax
            // and yield a NaN clamp result that cascades into NaN
            // shader-item geometry. `qIsFinite` catches NaN AND Inf.
            const bool geomFinite = qIsFinite(parentW) && qIsFinite(parentH) && qIsFinite(anchorW) && qIsFinite(anchorH)
                && qIsFinite(anchor->x()) && qIsFinite(anchor->y());
            if (geomFinite && parentW > 0.0 && parentH > 0.0 && anchorW > 0.0 && anchorH > 0.0) {
                const qreal availTop = qMax(qreal(0.0), anchor->y());
                const qreal availBottom = qMax(qreal(0.0), parentH - (anchor->y() + anchorH));
                const qreal availLeft = qMax(qreal(0.0), anchor->x());
                const qreal availRight = qMax(qreal(0.0), parentW - (anchor->x() + anchorW));
                const qreal maxPadH = qMin(availTop, availBottom) / anchorH;
                const qreal maxPadW = qMin(availLeft, availRight) / anchorW;
                pad = qMin(pad, qMin(maxPadH, maxPadW));
            }
        }
    }
    return qMin(pad, kMaxBoundsPaddingFraction);
}

/// Sanitise a `boundsPadding` source value into a NaN-free, finite
/// qreal suitable for storage in a Track's `requestedPadPtr`. The
/// `clampPaddingToParent` clamp above also rejects NaN, but inputs
/// to this helper land in a `shared_ptr<qreal>` cell that the
/// syncGeometry lambda dereferences without a NaN-check of its own
/// — so a NaN in the cell would propagate through clampPaddingToParent's
/// own input-side `qIsNaN` guard producing a 0.0 result on every paint.
/// Sanitising at the assignment boundary keeps both the reuse-path
/// refresh and the fresh-attach init using the same single-source
/// rule and centralises a guard that previously duplicated.
inline qreal sanitiseBoundsPadding(qreal requested)
{
    return qIsNaN(requested) ? qreal(0.0) : requested;
}

/// Apply the shader-item geometry derived from @p anchor + @p requestedPad.
/// Centralises the math behind the per-leg syncGeometry lambda so the
/// reuse path can re-fire it explicitly after a metadata hot-reload that
/// changes `boundsPadding` between legs — without it, the persistent
/// lambda only re-runs on the next anchor geometry signal, which for a
/// static popup (snap-assist, OSD) may never come, leaving the shader
/// rendered with the previous leg's pad until the anchor next moves or
/// resizes. Idempotent on identity (every setter no-ops when the value
/// is unchanged), so the common no-hot-reload reuse path costs nothing.
inline void syncShaderGeometryNow(QQuickItem* anchor, PhosphorRendering::ShaderEffect* shaderItem,
                                  QQuickShaderEffectSource* shaderSource, qreal requestedPad)
{
    if (!anchor || !shaderItem) {
        return;
    }
    const qreal w = anchor->width();
    const qreal h = anchor->height();
    if (w <= 0.0 || h <= 0.0) {
        return;
    }
    const qreal pad = clampPaddingToParent(anchor, requestedPad);
    const qreal padW = w * pad;
    const qreal padH = h * pad;
    shaderItem->setWidth(w + 2.0 * padW);
    shaderItem->setHeight(h + 2.0 * padH);
    shaderItem->setX(anchor->x() - padW);
    shaderItem->setY(anchor->y() - padH);
    shaderItem->setIResolution(QSizeF(w + 2.0 * padW, h + 2.0 * padH));
    QVector4D structural = shaderItem->customParamAt(7);
    structural.setX(static_cast<float>(pad));
    shaderItem->setCustomParamAt(7, structural);
    if (shaderSource) {
        shaderSource->setWidth(w);
        shaderSource->setHeight(h);
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
    bool warnedDuplicate = false;
    while (!stack.isEmpty()) {
        QQuickItem* item = stack.pop();
        if (!item) {
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
    return firstMatch;
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
    // `panel.popup` propagates to every `panel.popup.*.*` leg even
    // when the bundled per-leaf JSONs (panel.popup.layoutPicker.show
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
    /// Live boundsPadding — written here by attachShaderToAnchor at
    /// fresh attach, re-written by the reuse path on metadata hot-
    /// reload. The syncGeometry lambda captures this shared_ptr by
    /// value so it always re-reads the current value when anchor
    /// geometry signals fire mid-leg.
    std::shared_ptr<qreal> requestedPadPtr;
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

    const QVariantMap translated =
        PhosphorAnimationShaders::AnimationShaderRegistry::translateAnimationParams(effect, shaderParameters);
    if (!translated.isEmpty()) {
        shaderItem->setShaderParams(translated);
    }

    // Size + position the shader.
    //
    // boundsPadding (from metadata.json) enlarges the shader effect's
    // bounding box by a fraction of the anchor's size on every side so
    // shaders that distort their silhouette outward (morph's UV warp)
    // have room to render the rippled outline before the bounding box
    // clips it. The anchor sits in the centre of the padded box; shaders
    // that opt in (boundsPadding > 0) must remap vTexCoord back to anchor
    // [0,1] for their UV math.
    //
    // iResolution stays in LOGICAL units to match the project-wide shader
    // convention (libs/phosphor-rendering/src/shadereffect.cpp:763-765,
    // common.glsl pxScale()). Animation shaders derive UV from the vertex-
    // stage `vTexCoord` varying, not from `gl_FragCoord.xy / iResolution`
    // — gl_FragCoord is post-DPR pixel coords and would overshoot [0,1]
    // by a factor of DPR.
    // Clamp the metadata pad to fit the anchor's parent bounds. See
    // clampPaddingToParent docstring for why this matters (viewport
    // clipping breaks the morph shader's UV remap).
    const qreal pad = clampPaddingToParent(shaderAnchor, effect.boundsPadding);
    const qreal padW = shaderAnchor->width() * pad;
    const qreal padH = shaderAnchor->height() * pad;
    shaderItem->setWidth(shaderAnchor->width() + 2.0 * padW);
    shaderItem->setHeight(shaderAnchor->height() + 2.0 * padH);
    shaderItem->setX(shaderAnchor->x() - padW);
    shaderItem->setY(shaderAnchor->y() - padH);
    shaderItem->setIResolution(QSizeF(shaderAnchor->width() + 2.0 * padW, shaderAnchor->height() + 2.0 * padH));

    // Push the bounds-padding fraction into the structural slot
    // customParams[7].x so opt-in shaders (e.g. morph) can remap
    // vTexCoord → anchor-space without hardcoding a constant that
    // can drift from metadata.json. This slot is reserved by
    // SurfaceAnimator — `translateAnimationParams` fills user-declared
    // parameters from customParams[0] up sequentially, and no current
    // shader declares >24 float params, so customParams[7] (slots 28-31)
    // is unused by the user-parameter mapping. Documented in
    // libs/phosphor-rendering/include/PhosphorRendering/ShaderEffect.h
    // (customParams8 Q_PROPERTY). Note: writes the CLAMPED pad (above)
    // so the shader's UV remap matches the clamped geometry.
    QVector4D structuralParams = shaderItem->customParamAt(7);
    structuralParams.setX(static_cast<float>(pad));
    shaderItem->setCustomParamAt(7, structuralParams);

    // setSourceItem is what wires up FBO sampling and triggers the
    // shader effect's first sync into the scene graph. Setting it
    // LAST — after all initial uniforms, custom params, and geometry
    // are in place — guarantees that the shader's first painted frame
    // sees a fully-initialised state. Mirrors the setITime ordering
    // rationale above: any property set after setSourceItem could
    // race the first paint and produce a visible flash with default-
    // valued uniforms (boundsPadding=0 collapses the morph remap
    // regardless of the metadata; un-translated shaderParameters
    // leaves user uniforms at -1 sentinel; pre-padding geometry would
    // clip rippled silhouettes).
    if (shaderSource) {
        shaderItem->setSourceItem(shaderSource);
    }

    // Sync geometry if anchor resizes mid-transition. iResolution stays
    // in logical units (animation shaders derive UV from `vTexCoord`).
    // Padding around the shader effect tracks the anchor's live size.
    //
    // The lambda re-runs the same parent-margin clamp the initial-attach
    // block above does. Anchor x/y/w/h can change mid-leg (parent-layout
    // reflow on sibling-hide; host's anchored layout re-evaluating on
    // parent resize), and the clamp depends on the LIVE values — a
    // fixed `pad` captured at attach time would let the shader item drift
    // past the parent's bounds again whenever the anchor moves toward an
    // edge, re-introducing the viewport-clipping vTexCoord shift the
    // initial clamp avoids. Re-writes customParams[7].x with the
    // recomputed pad so the shader's UV remap stays consistent with the
    // updated geometry.
    //
    // shaderSource is captured as QPointer so that any teardown path that
    // destroys it independently of shaderItem (scene-graph rebuild,
    // explicit deleteLater outside teardownShaderLeg) doesn't leave the
    // lambda dereferencing freed memory. Auto-disconnect on shaderItem
    // destruction normally protects this — QPointer is belt-and-braces.
    // requestedPad is captured by `shared_ptr<qreal>` so the reuse
    // path can rewrite it under metadata hot-reload (a settings edit
    // that swaps the effect's `boundsPadding` between legs) and the
    // already-connected syncGeometry lambda picks up the new value
    // on the next anchor geometry signal. Capturing by value would
    // freeze the lambda on the original metadata.
    auto requestedPadPtr = std::make_shared<qreal>(sanitiseBoundsPadding(effect.boundsPadding));
    QPointer<PhosphorRendering::ShaderEffect> shaderItemPtr{shaderItem};
    auto syncGeometry = [shaderItemPtr, shaderSourcePtr = QPointer<QQuickShaderEffectSource>(shaderSource),
                         requestedPadPtr, anchorPtr = QPointer<QQuickItem>(shaderAnchor)]() {
        syncShaderGeometryNow(anchorPtr.data(), shaderItemPtr.data(), shaderSourcePtr.data(), *requestedPadPtr);
    };
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
    out.requestedPadPtr = std::move(requestedPadPtr);
    return out;
}

} // namespace

// ══════════════════════════════════════════════════════════════════════════
// SurfaceAnimator::Private
// ══════════════════════════════════════════════════════════════════════════

class SurfaceAnimator::Private
{
public:
    /// Per-surface in-flight bookkeeping. unique_ptr<AnimatedValue>
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
        /// Shared with the syncGeometry lambda so the reuse path can
        /// rewrite `boundsPadding` under metadata hot-reload and the
        /// already-connected lambda picks up the new value on the
        /// next anchor geometry signal.
        std::shared_ptr<qreal> requestedPadPtr;
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
        /// See Track::requestedPadPtr.
        std::shared_ptr<qreal> requestedPadPtr;
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

        // Park to m_pendingReuse[surface]. If a previous pending entry
        // exists for this surface (rare — should only happen if a
        // shader was parked but never reclaimed), destroy it first to
        // avoid leaking a stale ShaderEffect.
        connectSurfaceCleanup(surface);
        PendingReuseShader& pending = m_pendingReuse[surface];
        if (pending.shaderItem) {
            destroyPendingReuseEntry(pending);
        }
        pending.shaderItem = std::move(track.shaderItem);
        pending.shaderSource = std::move(track.shaderSource);
        pending.shaderAnchor = track.shaderAnchor;
        pending.foundExplicitAnchor = track.foundExplicitAnchor;
        pending.shaderEffectId = std::move(track.shaderEffectId);
        pending.target = track.target;
        pending.requestedPadPtr = std::move(track.requestedPadPtr);

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
        track.requestedPadPtr.reset();
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
        pending.requestedPadPtr.reset();
    }

    /// Drop the reuse stash for one surface — used when the surface is
    /// destroyed, when the next leg uses a different effect / target,
    /// or when a non-shader leg supersedes a shader leg.
    void destroyPendingReuseFor(PhosphorLayer::Surface* surface)
    {
        const auto it = m_pendingReuse.find(surface);
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
                // Also drop any still-tracked entry; consumer's
                // SurfaceAnimator::cancel may not have run for surfaces
                // that bypass the standard ~Surface cancel path.
                const auto trackIt = m_tracks.find(surf);
                if (trackIt != m_tracks.end()) {
                    m_tracks.erase(trackIt);
                }
                m_destroyedConnections.remove(surf);
            });
        m_destroyedConnections.insert(surface, conn);
    }

    /// Cancel any in-flight legs for a surface. Called directly and
    /// also implicitly on supersession. AnimatedValue::cancel clears
    /// m_isAnimating without firing spec.onComplete (cancel = non-
    /// completion termination per ISurfaceAnimator). AVs are parked in
    /// m_pendingDestroy because a re-entrant cancel from inside
    /// spec.onValueChanged must not destroy *this mid-advance
    /// (AnimatedValue.h:547); graveyard drains on next tickAll. Shader
    /// pieces are parked in m_pendingReuse (see teardownShaderLeg) so
    /// they survive the external Surface::show()/hide() pre-cancel and
    /// can be reclaimed by the immediately-following beginShow/Hide.
    void cancelTracking(PhosphorLayer::Surface* surface)
    {
        const auto it = m_tracks.find(surface);
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

    /// Look up the per-role config (longest-prefix-match on
    /// scopePrefix, fall back to default). Consumers derive per-
    /// surface roles via withScopePrefix("base-{screenId}") and
    /// register configs against the unsuffixed base, so a bare find()
    /// would always miss. Match boundary is '-' or end-of-string so
    /// "plasmazones-layout" doesn't collide with "plasmazones-layout-
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
        // cancelTracking parks any in-flight shader pieces in
        // m_pendingReuse[surface] (see parkShaderForReuse) so they
        // survive the external Surface::show()/hide() pre-cancel and
        // can be reclaimed below if the new leg uses the same effect.
        cancelTracking(surface);

        if (!target) {
            qCWarning(lcSurfaceAnimator) << "runLeg called with null target — onComplete fires synchronously";
            // Surface won't re-attach without a target; drop any
            // stashed shader pieces for this surface.
            destroyPendingReuseFor(surface);
            // Synchronous onComplete: same forbidden-ops contract as a
            // legCompleted-fired callback (SurfaceAnimator.h:79-90).
            if (onComplete) {
                onComplete();
            }
            return;
        }

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
        std::shared_ptr<qreal> reusedRequestedPadPtr;
        if (hasShaderLeg) {
            const auto pendIt = m_pendingReuse.find(surface);
            if (pendIt != m_pendingReuse.end()) {
                PendingReuseShader& pending = pendIt->second;
                // Re-validate anchor against the current QML scene.
                // NotificationOverlay's per-mode Loader unloads + loads
                // its content on `mode` flip (e.g. layout-osd ↔ navigation-
                // osd), destroying the previously-found shaderAnchor
                // descendant. The cached QPointer auto-nulls in that
                // case, but during the deleteLater window it's still
                // truthy — and even when truthy, it points at the
                // previous Loader generation's anchor, not the freshly-
                // loaded one. findShaderAnchorRecursive on the current
                // target reflects the live scene; only reuse when the
                // cache still resolves to the same QQuickItem the
                // attach found (or both fall back to target itself).
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
                    reusedRequestedPadPtr = std::move(pending.requestedPadPtr);
                    // Refresh the live boundsPadding so the persistent
                    // syncGeometry lambda (still connected from the
                    // original attach) picks up the new metadata value
                    // on the next anchor geometry signal. Without this
                    // refresh, an in-place metadata.json hot-reload that
                    // changes `boundsPadding` between legs leaves the
                    // lambda using the OLD value when the anchor next
                    // moves/resizes.
                    if (reusedRequestedPadPtr) {
                        *reusedRequestedPadPtr = sanitiseBoundsPadding(resolvedShaderEff.boundsPadding);
                    }
                    m_pendingReuse.erase(pendIt);
                } else {
                    // Mismatch (different effect, different target,
                    // QML scene rebuild swapped the anchor, or stale
                    // QPointers from item destruction) — drop the
                    // stash now rather than letting it sit with dead
                    // pointers indefinitely.
                    destroyPendingReuseFor(surface);
                }
            }
        } else {
            // Non-shader leg following a shader leg — no chance of
            // reuse. Drop any stashed shader for this surface.
            destroyPendingReuseFor(surface);
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
            Track& slot = m_tracks[surface];
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
            slot.requestedPadPtr.reset();
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
            auto it = m_tracks.find(surface);
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
                        // setHideSource(true) re-installs the anchor's
                        // QQuickItemLayer so the FBO captures the anchor
                        // content (and the anchor is suppressed from
                        // direct scene render — the shader effect is
                        // the sole renderer for the leg's duration).
                        reusedShaderSource->setHideSource(true);
                        reusedShaderSource->setLive(true);
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
                    // Re-fire the geometry sync so a boundsPadding
                    // hot-reload between legs (refreshed into the
                    // shared cell at the reuse-claim site above) takes
                    // effect immediately on the new leg's first frame.
                    // The persistent syncGeometry lambda only runs on
                    // anchor geometry signals; for static popups
                    // (snap-assist, OSD) those may never fire while the
                    // pad is wrong. Manually invoking the same logic
                    // here closes that gap.
                    syncShaderGeometryNow(reusedShaderAnchor.data(), reusedShaderItem.data(), reusedShaderSource.data(),
                                          reusedRequestedPadPtr ? *reusedRequestedPadPtr : qreal(0.0));
                    // Reset iTime to the new leg's start so the first
                    // painted frame after the shaderTime AV starts
                    // matches the AnimatedValue's intended `from` —
                    // same defence as attachShaderToAnchor's setITime
                    // block. Without this the parked iTime (terminal
                    // value of the previous leg) leaks into the first
                    // frame of the new leg.
                    reusedShaderItem->setITime(isShowLeg ? qreal(0.0) : qreal(1.0));
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
                    it->second.requestedPadPtr = std::move(reusedRequestedPadPtr);
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
                    it->second.requestedPadPtr = std::move(attached.requestedPadPtr);
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
            legCompleted(surface);
        } else {
            auto it = m_tracks.find(surface);
            if (it == m_tracks.end() || !it->second.opacity) {
                return;
            }
            it->second.opacity->start(fromOpacity, toOpacity,
                                      buildSpec(
                                          resolveProfile(m_registry, opacityProfilePath), clock,
                                          /*onValueChanged=*/
                                          [this, surface](const qreal& v) {
                                              auto sit = m_tracks.find(surface);
                                              if (sit == m_tracks.end() || !sit->second.target) {
                                                  return;
                                              }
                                              sit->second.target->setOpacity(v);
                                          },
                                          /*onComplete=*/
                                          [this, surface]() {
                                              legCompleted(surface);
                                          }));
        }

        if (hasScaleLeg) {
            auto it = m_tracks.find(surface);
            if (it == m_tracks.end() || !it->second.scale) {
                // The opacity leg's start() (or a synchronous callback)
                // cancelled the entry; let the cancellation stand.
                return;
            }
            it->second.scale->start(fromScale, toScale,
                                    buildSpec(
                                        resolveProfile(m_registry, scaleProfilePath), clock,
                                        /*onValueChanged=*/
                                        [this, surface](const qreal& v) {
                                            auto sit = m_tracks.find(surface);
                                            if (sit == m_tracks.end() || !sit->second.target) {
                                                return;
                                            }
                                            sit->second.target->setScale(v);
                                        },
                                        /*onComplete=*/
                                        [this, surface]() {
                                            legCompleted(surface);
                                        }));
        }

        if (hasShaderLeg) {
            auto it = m_tracks.find(surface);
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
                                             [this, surface](const qreal& v) {
                                                 auto sit = m_tracks.find(surface);
                                                 // shaderItem QPointer auto-nulls if the
                                                 // ShaderEffect's parent (the anchor) was torn
                                                 // down between ticks — bail rather than UAF.
                                                 if (sit == m_tracks.end() || !sit->second.shaderItem) {
                                                     return;
                                                 }
                                                 // Geometry sync runs off the anchor's
                                                 // widthChanged/heightChanged signals (see
                                                 // syncGeometry); the per-tick callback only
                                                 // threads the time value through.
                                                 sit->second.shaderItem->setITime(v);
                                             },
                                             /*onComplete=*/
                                             [this, surface]() {
                                                 legCompleted(surface);
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
    void legCompleted(PhosphorLayer::Surface* surface)
    {
        const auto it = m_tracks.find(surface);
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
        std::vector<PhosphorLayer::Surface*> surfaces;
        surfaces.reserve(m_tracks.size());
        for (auto& [s, _] : m_tracks) {
            surfaces.push_back(s);
        }
        // Real-time delta for shader iTimeDelta — measured between
        // SurfaceAnimator ticks (which fire at kTickIntervalMs cadence
        // while any track is in flight). First tick after a quiescent
        // period reports 0 instead of a stale wall-clock gap, matching
        // OverlayService::updateShaderUniforms's clamp-on-resume idiom
        // for the overlay path. Capped at 100 ms (same value as
        // overlay's `maxDelta`) so a sleep/resume hiccup doesn't push
        // a multi-second jump into the shader.
        constexpr qreal kMaxShaderDeltaSecs = 0.1;
        const qint64 nowNs = m_clock.now().count();
        qreal shaderDeltaSecs = 0.0;
        if (m_lastShaderTickNs > 0) {
            shaderDeltaSecs = qBound(qreal(0.0), qreal(nowNs - m_lastShaderTickNs) / 1.0e9, kMaxShaderDeltaSecs);
        }
        m_lastShaderTickNs = nowNs;

        for (auto* s : surfaces) {
            auto it = m_tracks.find(s);
            if (it == m_tracks.end()) {
                continue;
            }
            if (it->second.opacity) {
                it->second.opacity->advance();
            }
            // Re-find: advance may have completed + erased via legCompleted.
            it = m_tracks.find(s);
            if (it == m_tracks.end()) {
                continue;
            }
            if (it->second.scale) {
                it->second.scale->advance();
            }
            it = m_tracks.find(s);
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
            it = m_tracks.find(s);
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
    std::unordered_map<PhosphorLayer::Surface*, Track> m_tracks;
    /// Per-surface stash for ShaderEffect/Source pieces between legs.
    /// Populated by teardownShaderLeg, drained on reuse-claim in
    /// runLeg or on surface destruction (connectSurfaceCleanup wires
    /// the destroyed signal). Keeps one shader render-tower alive for
    /// the surface's lifetime instead of the create-then-destroy
    /// churn that used to leak Qt RHI resource update batches under
    /// rapid show/hide toggling. std::unordered_map (not QHash) for
    /// the same move-only-value reason as m_tracks.
    std::unordered_map<PhosphorLayer::Surface*, PendingReuseShader> m_pendingReuse;
    /// Per-surface destroyed-signal connections, keyed by raw Surface*
    /// pointer. Replaces the earlier `pz_surfaceAnimatorCleanupConnected`
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
    for (auto& [surface, track] : leftovers) {
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
    d->cancelTracking(surface);
}

} // namespace PhosphorAnimationLayer
