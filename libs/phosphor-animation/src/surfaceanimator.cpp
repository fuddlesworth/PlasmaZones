// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/SurfaceAnimator.h>

#include <PhosphorAnimation/AnimatedValue.h>
#include <PhosphorAnimation/IMotionClock.h>
#include <PhosphorAnimation/MotionSpec.h>
#include <PhosphorAnimation/PhosphorProfileRegistry.h>
#include <PhosphorAnimation/Profile.h>

#include <PhosphorAnimation/AnimationShaderRegistry.h>
#include <PhosphorRendering/ShaderEffect.h>

#include <PhosphorLayer/Role.h>
#include <PhosphorLayer/Surface.h>
#include <PhosphorLayer/SurfaceConfig.h>

#include <QHash>
#include <QLoggingCategory>
#include <QObject>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <private/qquickshadereffectsource_p.h>
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
    if (auto p = registry.resolve(path)) {
        return p->withDefaults();
    }
    qCWarning(lcSurfaceAnimator).nospace() << "Profile path '" << path
                                           << "' did not resolve through registry — "
                                              "falling back to library defaults (150 ms OutCubic). "
                                              "Check the profile name for typos and that the "
                                              "corresponding JSON ships under data/profiles/.";
    return PhosphorAnimation::Profile{}.withDefaults();
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
};

/// Build the per-leg ShaderEffect: anchor resolution + layer-enable +
/// setSourceItem (gated on explicit anchors — layer-enabling the
/// fallback QQuickRootItem breaks scene-graph rendering) + parameter
/// translation + live geometry sync.
ShaderAttachResult attachShaderToAnchor(QQuickItem* target,
                                        const PhosphorAnimationShaders::AnimationShaderEffect& effect,
                                        const QString& shaderEffectId, const QVariantMap& shaderParameters,
                                        bool isShowLeg)
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
    shaderItem->setShaderSource(QUrl::fromLocalFile(effect.fragmentShaderPath));
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
    const qreal pad = qMax(qreal(0.0), effect.boundsPadding);
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
    // (customParams8 Q_PROPERTY).
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
    // shaderSource is captured as QPointer so that any teardown path that
    // destroys it independently of shaderItem (scene-graph rebuild,
    // explicit deleteLater outside teardownShaderLeg) doesn't leave the
    // lambda dereferencing freed memory. Auto-disconnect on shaderItem
    // destruction normally protects this — QPointer is belt-and-braces.
    auto syncGeometry = [shaderItem, shaderSourcePtr = QPointer<QQuickShaderEffectSource>(shaderSource), pad,
                         anchorPtr = QPointer<QQuickItem>(shaderAnchor)]() {
        if (!anchorPtr) {
            return;
        }
        const qreal w = anchorPtr->width();
        const qreal h = anchorPtr->height();
        if (w <= 0.0 || h <= 0.0) {
            return;
        }
        const qreal padW = w * pad;
        const qreal padH = h * pad;
        shaderItem->setWidth(w + 2.0 * padW);
        shaderItem->setHeight(h + 2.0 * padH);
        shaderItem->setX(anchorPtr->x() - padW);
        shaderItem->setY(anchorPtr->y() - padH);
        shaderItem->setIResolution(QSizeF(w + 2.0 * padW, h + 2.0 * padH));
        if (shaderSourcePtr) {
            shaderSourcePtr->setWidth(w);
            shaderSourcePtr->setHeight(h);
        }
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

    // Hide visible decorator siblings of the anchor for the leg. We
    // would prefer to redirect their `source` to the shader so the
    // decoration (e.g. drop shadow) keeps tracking the shader's output,
    // but Qt 6 MultiEffect crashes inside its private QQuickShaderEffect
    // when given a non-stock QQuickItem as source. Hiding gives a clean
    // shader transition at the cost of the decoration popping back in
    // at teardown. Skipped for fallback anchors.
    QList<QPointer<QQuickItem>> hiddenSiblings;
    if (foundExplicitAnchor && shaderAnchor->parentItem()) {
        const auto siblings = shaderAnchor->parentItem()->childItems();
        for (QQuickItem* sibling : siblings) {
            if (sibling == shaderAnchor || sibling == shaderItem || sibling == shaderSource) {
                continue;
            }
            if (sibling->isVisible()) {
                hiddenSiblings.append(QPointer<QQuickItem>(sibling));
                sibling->setVisible(false);
            }
        }
    }

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
        int pendingLegs = 0;
        bool shaderExclusive = false; ///< Shader replaces motion legs
        qreal targetOpacity = 1.0; ///< Snapped on completion when shaderExclusive
        ISurfaceAnimator::CompletionCallback onComplete;
    };

    Private(PhosphorAnimation::PhosphorProfileRegistry& registry,
            PhosphorAnimationShaders::AnimationShaderRegistry* shaderRegistry, Config defaults)
        : m_registry(registry)
        , m_shaderRegistry(shaderRegistry)
        , m_defaultConfig(std::move(defaults))
    {
        // Driver fires kTickIntervalMs cadence while any track is in
        // flight; tickAll stops the timer when m_tracks empties.
        m_driverTimer.setInterval(std::chrono::milliseconds(kTickIntervalMs));
        m_driverTimer.setTimerType(Qt::PreciseTimer);
        QObject::connect(&m_driverTimer, &QTimer::timeout, [this]() {
            tickAll();
        });
    }

    /// Tear down the per-leg ShaderEffect. layer.enabled is restored
    /// only on explicit anchors AND only when THIS animator was the
    /// one that flipped it on — restoring on a fallback QQuickRootItem
    /// re-introduces the OSD-doesn't-render bug, and disabling a layer
    /// a third party had already enabled for unrelated reasons would
    /// silently break that third party.
    void teardownShaderLeg(Track& track)
    {
        if (track.shaderTime) {
            track.shaderTime->cancel();
            m_pendingDestroy.push_back(std::move(track.shaderTime));
        }
        if (track.shaderItem) {
            // Disconnect the anchor→shaderItem signal connections BEFORE
            // queueing the shaderItem for delete. Qt's receiver-destroyed
            // auto-disconnect runs only when the shaderItem is actually
            // destroyed by the event loop, leaving a window between
            // deleteLater() and dispatch where surrounding teardown work
            // (sibling visibility restore, scene-graph reflow on
            // shaderSource deleteLater) can still fire xChanged/y/width/
            // heightChanged on the anchor against a queued-for-delete
            // receiver. The explicit disconnect closes that window.
            if (track.shaderAnchor) {
                QObject::disconnect(track.shaderAnchor.data(), nullptr, track.shaderItem.data(), nullptr);
            }
            track.shaderItem->deleteLater();
            track.shaderItem = nullptr;
        }
        if (track.shaderSource) {
            track.shaderSource->deleteLater();
            track.shaderSource = nullptr;
        }
        // Restore visibility of the siblings we hid for the leg. QPointer
        // skips items the QML scene tore down meanwhile.
        for (const QPointer<QQuickItem>& sibling : track.hiddenSiblings) {
            if (sibling) {
                sibling->setVisible(true);
            }
        }
        track.hiddenSiblings.clear();
        track.shaderAnchor.clear();
        track.foundExplicitAnchor = false;
    }

    /// Cancel any in-flight legs for a surface. Called directly and
    /// also implicitly on supersession. AnimatedValue::cancel clears
    /// m_isAnimating without firing spec.onComplete (cancel = non-
    /// completion termination per ISurfaceAnimator). AVs are parked in
    /// m_pendingDestroy because a re-entrant cancel from inside
    /// spec.onValueChanged must not destroy *this mid-advance
    /// (AnimatedValue.h:547); graveyard drains on next tickAll.
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
        teardownShaderLeg(it->second);
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
        // Supersede BEFORE the null-target check — leaving a prior
        // entry behind on null-target strands its AV (custom
        // orchestrators bypass Surface::Impl::drive()'s pre-cancel).
        cancelTracking(surface);

        if (!target) {
            qCWarning(lcSurfaceAnimator) << "runLeg called with null target — onComplete fires synchronously";
            // Synchronous onComplete: same forbidden-ops contract as a
            // legCompleted-fired callback (SurfaceAnimator.h:79-90).
            if (onComplete) {
                onComplete();
            }
            return;
        }

        PhosphorAnimation::IMotionClock* clock = &m_clock;

        // Resolve shader up front so legCount is accurate.
        PhosphorAnimationShaders::AnimationShaderEffect resolvedShaderEff;
        if (!shaderEffectId.isEmpty() && m_shaderRegistry) {
            resolvedShaderEff = m_shaderRegistry->effect(shaderEffectId);
        }
        const bool hasShaderLeg = resolvedShaderEff.isValid();

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
                // resolvedShaderEff already validated by hasShaderLeg.
                // isShowLeg lets attachShaderToAnchor seed iTime to the
                // leg's start value (0 for show, 1 for hide) so the very
                // first paint after the QQuickShaderEffect is added to
                // the scene matches the AnimatedValue's intended start
                // — see attachShaderToAnchor's setITime block.
                const bool isShowLeg = (toOpacity > fromOpacity);
                ShaderAttachResult attached =
                    attachShaderToAnchor(target, resolvedShaderEff, shaderEffectId, shaderParameters, isShowLeg);
                it->second.shaderItem = attached.shaderItem;
                it->second.shaderSource = attached.shaderSource;
                it->second.shaderAnchor = attached.shaderAnchor;
                it->second.foundExplicitAnchor = attached.foundExplicitAnchor;
                it->second.hiddenSiblings = std::move(attached.hiddenSiblings);
                it->second.shaderTime = std::make_unique<PhosphorAnimation::AnimatedValue<qreal>>();
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
            it->second.shaderTime->start(shaderFrom, shaderTo,
                                         buildSpec(
                                             resolveProfile(m_registry, shaderLegProfilePath), clock,
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
        teardownShaderLeg(it->second);
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

        // Snapshot — legCompleted may erase from m_tracks while iterating.
        std::vector<PhosphorLayer::Surface*> surfaces;
        surfaces.reserve(m_tracks.size());
        for (auto& [s, _] : m_tracks) {
            surfaces.push_back(s);
        }
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
        }
        // Stop the driver if every track completed during this tick.
        if (m_tracks.empty() && m_driverTimer.isActive()) {
            m_driverTimer.stop();
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
    auto leftovers = std::move(d->m_tracks);
    d->m_tracks.clear();
    for (auto& [_, track] : leftovers) {
        if (track.opacity) {
            track.opacity->cancel();
            d->m_pendingDestroy.push_back(std::move(track.opacity));
        }
        if (track.scale) {
            track.scale->cancel();
            d->m_pendingDestroy.push_back(std::move(track.scale));
        }
        d->teardownShaderLeg(track);
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
