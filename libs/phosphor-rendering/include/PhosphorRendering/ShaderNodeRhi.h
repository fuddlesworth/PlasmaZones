// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorRendering/ShaderNodeLiveness.h>
#include <PhosphorRendering/phosphorrendering_export.h>

#include <PhosphorShaders/BaseUniforms.h>
#include <PhosphorShaders/IUboProfile.h>
#include <PhosphorShaders/IUniformExtension.h>
#include <PhosphorShaders/ShaderBindings.h>
#include <PhosphorShaders/ShaderEntryPoint.h>

#include <QColor>
#include <QImage>
#include <QMatrix4x4>
#include <QPointF>
#include <QPointer>
#include <QSizeF>
#include <QQuickItem>
#include <QSGRenderNode>
#include <QSGTextureProvider>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QVector4D>

#include <array>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <optional>

#include <rhi/qrhi.h>

namespace PhosphorRendering {

// Forward declare constants used in member declarations.
// A namespace-scope `constexpr int` in a header is NOT implicitly inline: it is
// const and therefore has internal linkage, so each translation unit gets its
// own copy. That is harmless for an integral constant used only in constant
// expressions and array bounds, which is every use here, and it matches the
// existing style of kFirstFreeConsumerBinding / kMaxConsumerBinding below. Only
// a constexpr STATIC DATA MEMBER became implicitly inline in C++17, which is the
// rule this comment used to cite.
constexpr int kMaxBufferPasses = PhosphorShaders::kMaxBufferPasses;
constexpr int kMaxUserTextures = PhosphorShaders::Bindings::kUserTextureCount;
constexpr int kMaxCustomParams = 8;
constexpr int kMaxCustomColors = 16;

// ── Consumer binding range (for setExtraBinding) ────────────────────────
// The binding table is PhosphorShaders::Bindings (ShaderBindings.h), the one
// layout every shader family's shared GLSL header declares and the validator
// reflects baked stages against. Library-managed bindings: 0 (UBO), 2..9
// (multipass buffers), 10 (audio), 11..14 (user textures), 15 (wallpaper /
// backdrop), 16 (depth). Assigning any of those via setExtraBinding() would
// duplicate SRB entries and is rejected at runtime. Binding 1 is the one
// "free-in-the-gap" slot; 17..31 are free as well.
//
// Phosphor uses binding 1 for its zone-labels texture by convention.
// Other consumers that interoperate with Phosphor should pick from
// (kReservedBindingRangeEnd + 1)..kMaxConsumerBinding to avoid a per-instance
// overwrite. (The binding table spells that lower bound kExtraBase, but that
// name lives in PhosphorShaders::Bindings and is not declared in this
// namespace, so the local spelling is the one to use here.)
constexpr int kFirstFreeConsumerBinding =
    PhosphorShaders::Bindings::kConsumer; ///< First slot usable via setExtraBinding()
/// Highest binding this library will hand out. 31 is a PROJECT ceiling chosen to
/// stay inside what every RHI backend realistically offers, not a value read
/// back from Qt: QRhi exposes no such limit query, so nothing here can assert it
/// and no upstream guarantee is being quoted. Going higher risks
/// pipeline-creation failure on conservative drivers.
constexpr int kMaxConsumerBinding = PhosphorShaders::Bindings::kMaxBinding;
constexpr int kReservedBindingRangeStart =
    PhosphorShaders::Bindings::kChannelBase; ///< First library-managed binding above 0
constexpr int kReservedBindingRangeEnd = PhosphorShaders::Bindings::kReservedEnd; ///< Last library-managed binding

/// First SRB binding for the user-texture slots (slot 0 → binding 11).
/// Both `setSourceTextureProvider`'s slot-0 override and the
/// QImage-uploaded user textures key off this base.
constexpr int kUserTextureBaseBinding = PhosphorShaders::Bindings::kUserTextureBase;

/// @return true if @p binding is usable by consumers via setExtraBinding().
constexpr bool isConsumerBinding(int binding) noexcept
{
    return PhosphorShaders::Bindings::isConsumerBinding(binding);
}

/**
 * @brief QSGRenderNode for fullscreen-quad shader rendering via Qt RHI (Vulkan / OpenGL)
 *
 * Generalized render node extracted from Phosphor's ZoneShaderNodeRhi.
 * Manages a Shadertoy-compatible UBO (a profile-supplied layout, BaseUniforms
 * by default), multipass buffer system, texture bindings (audio, user,
 * wallpaper, depth), and shader baking.
 *
 * Application-specific UBO data is appended via IUniformExtension.
 * Application-specific texture bindings use setExtraBinding() / removeExtraBinding().
 *
 * Uses QRhi and QShaderBaker (runtime SPIR-V + GLSL 330 bake). Requires Qt 6.6+
 * (commandBuffer(), renderTarget()).
 *
 * @par Threading contract
 * Setters on **this class** (ShaderNodeRhi — setTime, setResolution, setCustomParams,
 * setExtraBinding, etc.) must be called from QQuickItem::updatePaintNode() during
 * the scene graph sync phase — the GUI thread is blocked and the render thread is
 * idle at that point. Calling these setters outside updatePaintNode() is a data
 * race with prepare()/render() on the render thread. Only invalidateItem() is
 * safe to call from the GUI thread outside the sync phase: it is the only entry
 * point built for it, with its flag atomic AND m_itemMutex serialising the
 * dereference against the render thread. (Not "the only flag exposed as
 * std::atomic", which this said before and which is simply untrue — the cached
 * geometry below is atomic too. Those are private and reached only through
 * rect(), which the scene graph calls on the render thread, so the rule itself
 * is unaffected.)
 *
 * Two sanctioned routes reach releaseResources() on the render thread OUTSIDE
 * the sync phase, and a consumer deciding what may call it needs both: the
 * QQuickWindow::NoStage render job posted by
 * ShaderEffect::releaseIdleGraphicsResources, and the Qt::DirectConnection to
 * QQuickWindow::sceneGraphAboutToStop, whose lambda calls it as well. The render
 * job certainly runs while the GUI thread is NOT blocked, and the same safety
 * argument covers both either way: no GUI-thread path mutates node members
 * directly, because every ShaderEffect setter stages into the item's own members
 * and defers the node push to the next sync, so neither route can race a
 * concurrent member write.
 *
 * (Setters on the sibling ShaderEffect class are a different story — those run
 * on the GUI thread and stage their changes into ShaderEffect's own members, to
 * be pushed down to this node during the next sync phase.)
 *
 * @par Extra binding lifetime (CRITICAL)
 * The consumer-owned QRhiTexture/QRhiSampler pointers passed to setExtraBinding()
 * are stored as raw pointers and re-bound on every prepare() until
 * removeExtraBinding() is called. If the consumer drops the texture/sampler
 * (e.g. by calling unique_ptr::reset() or letting it fall out of scope) without
 * first calling removeExtraBinding(), the next prepare() will dereference a
 * dangling pointer — undefined behaviour, typically a crash inside the RHI
 * backend. ALWAYS call removeExtraBinding(binding) before destroying the
 * underlying QRhiTexture/QRhiSampler. No automatic lifetime tracking
 * exists — callers are solely responsible for the remove-before-destroy
 * contract.
 */
class PHOSPHORRENDERING_EXPORT ShaderNodeRhi : public QSGRenderNode
{
public:
    /// @param item    Owning QQuickItem (must outlive the node until
    ///                invalidateItem() is called).
    /// @param profile Pluggable UBO profile. The default (nullptr) installs a
    ///                BaseUniformProfile so every existing caller — including
    ///                ZoneShaderNodeRhi's `ShaderNodeRhi(item)` forward — keeps
    ///                the legacy overlay/animation UBO (BaseUniforms, currently
    ///                672 bytes) unchanged. The surface-decoration runtime passes
    ///                a SurfaceUniformProfile here to reuse the engine with the
    ///                leaner surface UBO. UBO size is always profile-defined
    ///                (m_uboProfile->baseSize()), never hard-coded.
    explicit ShaderNodeRhi(QQuickItem* item, std::unique_ptr<PhosphorShaders::IUboProfile> profile = nullptr);
    ~ShaderNodeRhi() override;

    /**
     * @brief Notify the render node that its owning item is being destroyed.
     *
     * Called from the owning QQuickItem destructor on the GUI thread.
     * After this call, the render node will no longer dereference m_item.
     * Thread-safe: uses an atomic flag checked by prepare()/render().
     */
    void invalidateItem();

    /**
     * @brief Whether this node still holds a usable back-pointer to its item.
     *
     * False once invalidateItem() has run. This is the node's central
     * threading invariant — every m_item dereference is gated on it — so it is
     * exposed for callers that need to assert the teardown contract held
     * (chiefly: that the item severed the back-pointer before it was freed).
     * Thread-safe, but only a snapshot: do not use it to gate a dereference
     * the node does not own.
     */
    bool hasValidItem() const
    {
        return m_itemValid.load(std::memory_order_acquire);
    }

    /**
     * @brief The liveness block to hand to ShaderEffect::registerRenderNode.
     *
     * Valid for the node's whole lifetime and beyond — the block itself
     * outlives the node, which nulls its own pointer inside it on
     * destruction. See ShaderNodeLiveness.
     */
    const std::shared_ptr<ShaderNodeLiveness>& liveness() const
    {
        return m_liveness;
    }

    // QSGRenderNode
    QSGRenderNode::StateFlags changedStates() const override;
    QSGRenderNode::RenderingFlags flags() const override;
    QRectF rect() const override;
    void prepare() override;
    void render(const RenderState* state) override;
    void releaseResources() override;
    /// Stock-parity dependency pull (mirrors QSGRhiShaderEffectNode): bring
    /// the source provider's layer texture current before this node renders.
    /// Without it, a ShaderEffectSource this node samples only re-grabs when
    /// its OWN subtree is dirtied — a dependency the scene graph cannot see
    /// through a raw provider pointer — so a node captured into an enclosing
    /// layer (SurfaceAnimator's transition capture) can freeze on an empty
    /// first grab for a whole animation leg. Enabled via UsePreprocess in
    /// the constructor.
    void preprocess() override;

    // ── Uniform Extension ──────────────────────────────────────────────
    void setUniformExtension(std::shared_ptr<PhosphorShaders::IUniformExtension> extension);
    /** Access the currently-installed uniform extension (may be nullptr). */
    std::shared_ptr<PhosphorShaders::IUniformExtension> uniformExtension() const
    {
        return m_uniformExtension;
    }

    // ── Timing ─────────────────────────────────────────────────────────
    void setTime(double time);
    void setTimeDelta(float delta);
    void setFrame(int frame);
    void setResolution(float width, float height);
    void setMousePosition(const QPointF& pos);
    /// Direction signal for asymmetric leg rendering. Forward through
    /// to BaseUniforms::iIsReversed at offset 660. SurfaceAnimator
    /// pushes this from the leg's `isShowLeg` flag (false = reverse =
    /// hide leg); kwin-effect parity is handled directly via setUniform
    /// on the kwin path.
    void setIsReversed(bool reverse);

    // ── Custom Parameters (indexed API) ────────────────────────────────
    void setCustomParams(int index, const QVector4D& params);
    void setCustomColor(int index, const QColor& color);

    // ── Surface-only state ─────────────────────────────────────────────
    /**
     * @brief Per-surface inputs consumed by a SurfaceUniformProfile.
     *
     * These feed the surface-only fields of UboFrameState that a surface UBO
     * profile reads (opacity, logical→device scale, focus, surface/frame
     * geometry in device px). A BaseUniformProfile ignores them, so the
     * overlay/animation path is unaffected — the members default to the same
     * values as UboFrameState. A border or rounded-corner pack needs the real
     * surface/frame geometry to know where its edges are, so the host
     * (SurfaceShaderItem) must push these each frame from updatePaintNode.
     */
    void setSurfaceOpacity(float opacity);
    void setSurfaceScale(float scale);
    void setSurfaceFocused(bool focused);
    void setSurfaceSize(float width, float height);
    /// The slice of the bound backdrop this surface samples, normalized
    /// texture coords (xy = min, zw = size). Defaults to the whole texture.
    ///
    /// Only a host that binds ONE shared image across many surfaces needs this
    /// — a daemon or preview host handing every surface the same desktop
    /// wallpaper. The compositor's capture already covers the window's own
    /// canvas, so it leaves the default alone.
    void setBackdropRect(float x, float y, float w, float h);
    void setSurfaceFrameTopLeft(float x, float y);
    void setSurfaceFrameSize(float width, float height);

    // ── App Fields (consumer escape hatch in BaseUniforms) ─────────────
    /**
     * @brief Write the consumer's two int slots inside BaseUniforms (offsets 88, 92).
     *
     * See PhosphorShaders::BaseUniforms for the full escape-hatch rationale.
     * Updating these fields is cheap: the library uploads only the 8-byte
     * K_APP_FIELDS region rather than the full scene header.
     */
    void setAppField0(int value);
    void setAppField1(int value);

    // ── Extra Bindings (consumer-managed texture bindings) ─────────────
    /**
     * @brief Bind a consumer-owned texture/sampler at the given binding number.
     *
     * @warning The texture and sampler are stored as raw pointers and reused
     * across frames. The consumer MUST call removeExtraBinding(binding) before
     * destroying the underlying QRhiTexture/QRhiSampler — failing to do so
     * leaves a dangling pointer that will be dereferenced inside the RHI on
     * the next prepare() (UB, typically a crash).
     *
     * @return true on success; false if `binding` collides with a library-
     * managed slot (0, 2-12) or falls outside the supported range. A `true`
     * return for an identical (binding, texture, sampler) triple is a no-op —
     * the SRB/pipeline is NOT rebuilt when nothing actually changed.
     */
    bool setExtraBinding(int binding, QRhiTexture* texture, QRhiSampler* sampler);
    /// @return true if a binding existed at that slot and was removed.
    bool removeExtraBinding(int binding);

    // ── Geometry ───────────────────────────────────────────────────────
    /**
     * @brief Tessellate the IMAGE pass into an n×n cell grid (0 = the
     * default two-triangle fullscreen quad).
     *
     * Vertex-displacing animation packs (the `geometryGrid` metadata key)
     * deform per vertex, which a 4-vertex quad can only shear linearly —
     * the kwin path emits a window-quad grid for them, and this is the
     * Qt-RHI analogue. Positions stay clip-space, texCoords 0..1, so the
     * same vertex sources run unchanged; only the mesh density changes.
     * Buffer passes keep the plain quad: they are image-space passes and
     * never displace. Clamped to [0, kMaxGridSubdivisions] (index-buffer
     * width); the pack metadata clamp (kMaxGeometryGridSubdivisions = 128)
     * is tighter.
     * Same threading contract as every setter here: updatePaintNode() only.
     */
    void setGridSubdivisions(int subdivisions);

    /// Grid-density ceiling: (255+1)² vertices is the most a quint16 index
    /// buffer can address. Shared by ShaderEffect's property clamp and the
    /// node setter so the two can never disagree — the item↔node equality
    /// short-circuits rely on both sides clamping identically.
    static constexpr int kMaxGridSubdivisions = 255;

    // ── Textures ───────────────────────────────────────────────────────
    void setAudioSpectrum(const QVector<float>& spectrum);
    void setUserTexture(int slot, const QImage& image);
    /// Set per-slot sampler address mode. Accepts "clamp", "repeat", or
    /// "mirror" (case-sensitive); other values fall back to "clamp" via
    /// `normalizeWrapMode`.
    void setUserTextureWrap(int slot, const QString& wrap);
    void setWallpaperTexture(const QImage& image);
    void setUseWallpaper(bool use);
    void setUseDepthBuffer(bool use);

    /// @brief Live texture-provider override for user-texture slot 0
    ///        (SRB binding 11 / `uTexture0`).
    ///
    /// When set, every SRB rebuild reads
    /// `provider->texture()->rhiTexture()` and binds that — superseding
    /// whatever QImage was uploaded via setUserTexture(0, ...). The
    /// provider must outlive the node OR be cleared with `nullptr`
    /// before destruction; we hold a QPointer so a torn-down provider
    /// nulls out cleanly. Designed for QQuickItem::textureProvider()
    /// returns from layer-enabled items, where the underlying
    /// QSGTexture identity changes when the FBO is recreated (resize,
    /// device-loss); the SRB-rebuild detection in `prepare()` notices
    /// that change and refreshes the binding without the consumer
    /// having to re-call this setter every frame.
    void setSourceTextureProvider(QSGTextureProvider* provider);
    QSGTextureProvider* sourceTextureProvider() const
    {
        return m_sourceTextureProvider.data();
    }

    // ── Multi-pass Buffers ─────────────────────────────────────────────
    void setBufferShaderPath(const QString& path);
    void setBufferShaderPaths(const QStringList& paths);
    void setBufferFeedback(bool enable);
    /// The single-value scale, and the seed for every per-pass slot: this writes
    /// @p scale into ALL of them.
    ///
    /// ORDER MATTERS between this and setBufferScales, and it is not symmetric.
    /// Call this one FIRST and setBufferScales second. The reverse order
    /// discards the per-pass list entirely, because this setter overwrites every
    /// slot it just filled. ShaderEffect::syncBasePropertiesToNode pushes them
    /// in that order for exactly this reason.
    void setBufferScale(qreal scale);
    /// Per-pass scales aligned with the buffer paths; a slot past the list's
    /// end follows the single-value scale. Empty means "every pass on the
    /// single-value scale".
    ///
    /// Must be pushed AFTER setBufferScale — see its note.
    void setBufferScales(const QList<qreal>& scales);
    /// Buffer-pass texel format: RGBA16F when true (the default — safe for HDR,
    /// signed-data, and feedback buffers), RGBA8 when a pack's metadata declares
    /// its buffers hold plain clamped colour (`"halfFloatBuffers": false`).
    void setHalfFloatBuffers(bool enable);
    void setBufferWrap(const QString& wrap);
    void setBufferWraps(const QStringList& wraps);
    void setBufferFilter(const QString& filter);
    void setBufferFilters(const QStringList& filters);

    // ── Shader Loading ─────────────────────────────────────────────────
    //
    // BOTH stages are mandatory and the bake FAILS CLOSED without them: with an
    // empty vertex or fragment source the bake aborts with "Vertex or fragment
    // shader source is empty", leaves isShaderReady() false, and render() draws
    // nothing. A consumer that sets only a fragment stage gets a silent blank,
    // not a default vertex stage, so it must supply one of these two.
    //
    // The file form returns false and leaves the previously installed source
    // standing; the inline form replaces it and clears the file-derived
    // bake-cache key (path, mtime and include fingerprint), since inline source
    // has no file behind it.
    bool loadVertexShader(const QString& path);
    bool loadFragmentShader(const QString& path);
    void setVertexShaderSource(const QString& source);
    void setFragmentShaderSource(const QString& source);
    bool isShaderReady() const;
    QString shaderError() const;
    void invalidateShader();
    /// Drop the resident bake, its error, and any pending rebake together.
    /// For the "source is gone" arms of the owning item's sync (a failed
    /// load, or a deliberately cleared source): a resident previous bake
    /// would otherwise keep isShaderReady() true and let the item's status
    /// block promote a just-reported Error/Null back to Ready in the same
    /// sync, and the pending m_shaderDirty would make the next prepare()
    /// manufacture an "empty source" error for a deliberate clear. render()
    /// bails on the cleared readiness; the next real source set re-arms the
    /// bake through the setters.
    void clearBakedShader();
    void invalidateUniforms();

    // ── Include Paths ──────────────────────────────────────────────────
    void setShaderIncludePaths(const QStringList& paths);

    // ── Parameter preamble (T1.1: generated `#define p_<id> ...`) ─────
    /// Set the generated named-parameter preamble that is spliced after the
    /// fragment shader's `#version` line at load time (via
    /// `PhosphorShaders::spliceAfterVersion`), so authors read a parameter by
    /// name instead of hand-decoding a `customParams[N].xyzw` lane. The empty
    /// default is a no-op splice, so non-adopting shaders bake byte-identically
    /// to before. The preamble is folded into the bake-cache key, so two
    /// effects sharing a `.frag` but differing in generated defines never
    /// collide, and a metadata param edit (which changes the preamble without
    /// touching the `.frag` mtime) still invalidates the cache.
    void setParamPreamble(const QString& preamble);

    // ── Entry-point scaffold (T1.4: harness-generated `main()`) ────────
    /// Install the entry-point scaffold for the fragment stage. When set and
    /// the loaded fragment source does NOT define `main()`, `loadFragmentShader`
    /// assembles `prologue + source` and appends the generated `main()` of the
    /// first @p candidates entry function the source defines (via
    /// `PhosphorShaders::composeEntryPoint`), BEFORE include expansion — so the
    /// prologue's `#include` is resolved. A source that defines its own `main()`
    /// is left untouched (every bundled pack today). The scaffold is folded into
    /// the bake-cache key, and MUST match what the warm-bake applies for the
    /// same shader. Empty prologue + empty candidates (the default, e.g. the
    /// animation path) disables assembly entirely — a strict no-op.
    void setEntryScaffold(const QString& prologue, const QList<PhosphorShaders::EntryCandidate>& candidates);

    /// Normalize wrap mode string to "clamp", "repeat", or "mirror" (static
    /// helper, safe to call from any thread — operates on its arguments only).
    /// Unknown / empty inputs fall back to "clamp" — that fallback is
    /// load-bearing for legacy callers that pass an empty string when no
    /// wrap is configured.
    static QString normalizeWrapMode(const QString& wrap);
    /// Normalize filter mode string to "nearest", "linear", or "mipmap".
    static QString normalizeFilterMode(const QString& filter);

    /// Map a normalized wrap-mode string to QRhiSampler::AddressMode. Single
    /// source of truth for sampler address-mode selection so callers can't
    /// drift on the "mirror" / "repeat" / "clamp" mapping.
    static QRhiSampler::AddressMode wrapModeToRhiAddress(const QString& wrap);

protected:
    /**
     * @brief Thread-safe QRhi accessor.
     * @return The active QRhi for the owning window, or nullptr when the item
     * has been invalidated or has no window. Subclasses that override prepare()
     * should route through this helper rather than `commandBuffer()->rhi()` so
     * the post-invalidateItem() guard stays uniform.
     */
    QRhi* safeRhi() const;

    /**
     * @brief Retract this node from its liveness block.
     *
     * MUST be the first statement of EVERY subclass destructor, not just this
     * class's. The most-derived destructor body and its member teardown run
     * before ~ShaderNodeRhi, so a retract that happened only in the base would
     * leave ShaderEffect::withTrackedNode able to dispatch a virtual call into
     * a node whose derived half is already destroyed.
     *
     * Idempotent (nulling an already-null pointer under the same mutex), so
     * the base destructor's own call after a subclass has already retracted
     * costs one uncontended lock and nothing else. Takes
     * ShaderNodeLiveness::mutex, so it must not be called with m_itemMutex
     * held. See ShaderNodeLiveness for the full ordering.
     */
    void retractLiveness() noexcept;

private:
    bool ensurePipeline();
    bool ensureBufferPipeline();
    bool ensureBufferTarget();
    bool ensureDummyChannelResources(QRhi* rhi);
    bool ensureBufferSampler(QRhi* rhi, int index);
    /// Drop every buffer-pass target and everything compiled against it
    /// (render targets, pass descriptors, pipelines, SRBs). Shared by
    /// setBufferScale and setHalfFloatBuffers; ensureBufferTarget rebuilds.
    void resetBufferTargets();
    /// Snapshot the node's live members into a UboFrameState and hand it to the
    /// installed UBO profile's fill(). @p rhi supplies the NDC Y-orientation
    /// the profile folds into qt_Matrix.
    void syncBaseUniforms(QRhi* rhi);
    /// True when this node's current render target is a texture render target
    /// (a ShaderEffectSource layer capture) rather than the window. Null-safe;
    /// the single predicate behind both the qt_Matrix flip decision
    /// (syncBaseUniforms) and prepare()'s retarget detection, so the two can
    /// never disagree within a frame.
    bool renderingIntoTexture() const;
    void uploadDirtyTextures(QRhi* rhi, QRhiCommandBuffer* cb);
    /**
     * Append the extension region to a resource update batch.
     * @pre m_uniformExtension && m_uniformExtension->extensionSize() > 0
     * Reuses m_extensionStaging to avoid per-frame render-thread allocations.
     */
    void uploadExtensionToUbo(QRhiResourceUpdateBatch* batch);
    void releaseRhiResources();
    void appendUserTextureBindings(QVector<QRhiShaderResourceBinding>& bindings) const;
    /// Whether the wallpaper binding carries a real wallpaper rather than the dummy.
    ///
    /// The single source of truth for two things that must never disagree:
    /// which texture appendWallpaperBinding() puts at the wallpaper binding, and what
    /// syncBaseUniforms() reports in uHasBackdrop. A pack branches on that
    /// uniform to decide whether to sample uBackdrop at all, so a gate that
    /// outran the binding would have it sampling the 1x1 dummy (or a
    /// transparent fallback) while believing it had a real backdrop, instead
    /// of taking its documented no-backdrop appearance.
    ///
    /// The image must be non-null as well as the GPU objects: the upload path
    /// deliberately writes a transparent fallback for a null image, so
    /// "a texture exists" alone is not "there is something to show".
    bool wallpaperBindingLive() const
    {
        return m_useWallpaper && m_wallpaperTexture && m_wallpaperSampler && !m_wallpaperImage.isNull();
    }
    void appendWallpaperBinding(QVector<QRhiShaderResourceBinding>& bindings) const;
    /// How the pass an SRB belongs to touches the depth texture.
    ///
    /// Every buffer render target carries the depth texture as its second
    /// colour attachment (ensureBufferTarget), so a buffer pass WRITES it.
    /// Binding it as a sampled texture in that same pass's SRB is a read and a
    /// write of one texture inside a single render pass, which QRhi rejects
    /// ("Texture ... used with different accesses within the same pass, this is
    /// not allowed") — it showed up on the overlay packs that pair `multipass`
    /// with `depthBuffer` (neon-city, voxel-terrain). Those buffer shaders only
    /// ever write depth (`layout(location = 1) out float oDepth`); reading it
    /// back is the image pass's job, and the depth.glsl helper lives on the
    /// image side. So a writing pass gets the dummy texture at the depth binding —
    /// a valid binding for a shader that includes depth.glsl anyway, rather
    /// than a missing one.
    enum class DepthAccess {
        Sampled, ///< Image pass: binds the real depth texture.
        WrittenThisPass ///< Buffer pass: depth is an attachment, bind the dummy.
    };
    void appendDepthBinding(QVector<QRhiShaderResourceBinding>& bindings, DepthAccess access) const;
    void appendExtraBindings(QVector<QRhiShaderResourceBinding>& bindings) const;
    void appendAudioBinding(QVector<QRhiShaderResourceBinding>& bindings) const;
    /// UBO at binding 0 + consumer-managed extra bindings (e.g. binding 1).
    void appendUboAndExtraBindings(QVector<QRhiShaderResourceBinding>& bindings) const;
    /// Bindings 10 (audio), 11-14 (user textures), 15 (wallpaper), 16 (depth) —
    /// shared trailer between buffer-pass and image-pass SRBs. @p depthAccess
    /// is the one thing the two differ on; see DepthAccess.
    void appendCommonTrailerBindings(QVector<QRhiShaderResourceBinding>& bindings, DepthAccess depthAccess) const;
    void resetAllBindingsAndPipelines();
    void bakeBufferShaders();
    QString loadAndExpandShader(const QString& path, QString* outError);
    /// Variant that also collects the canonical paths of every
    /// transitively-included header into @p outIncludedPaths. Used by
    /// `loadVertexShader` / `loadFragmentShader` to fingerprint includes
    /// for the bake-cache key — see `shaderCacheKey` in
    /// shadernoderhicore.cpp for the policy.
    QString loadAndExpandShaderTracked(const QString& path, QStringList* outIncludedPaths, QString* outError);
    /// Schedule another frame from the render thread (QQuickWindow::update()
    /// is documented thread-safe), with safeRhi()'s liveness locking. For
    /// prepare()-side conditions that leave work pending — a grid upload
    /// that got no resource-update batch — where a STATIC item (no clock,
    /// no property churn) would otherwise never be prepared again and
    /// render()'s pending-skip would leave it blank indefinitely.
    void requestAnotherFrame() const;

    QQuickItem* m_item = nullptr;
    std::atomic<bool> m_itemValid{true};

    /// Serialises every m_item dereference (prepare/render/rect/safeRhi) against
    /// invalidateItem(). The atomic flag alone is insufficient: a render-thread
    /// prepare() that passed the m_itemValid check can race with a GUI-thread
    /// QQuickItem destructor mid-function — the flag flips, but the in-flight
    /// dereference still sees a dangling pointer and SIGSEGVs inside
    /// QQuickItem::window(). Holding this mutex around both sides forces
    /// invalidateItem() to wait until the in-flight call returns, so the flag
    /// is observed atomically with respect to subsequent dereferences.
    /// `mutable` so the const accessors (rect/safeRhi) can lock.
    // Lock-ordering invariant: MUST NOT block on the GUI thread while holding
    // m_itemMutex (avoids deadlock during ~ShaderEffect → invalidateItem path).
    mutable std::mutex m_itemMutex;

    /// Published to the tracking ShaderEffect so it can tell a live node from
    /// one the scene graph has already deleted. Seeded with `this` in the
    /// constructor, nulled under its own mutex by retractLiveness() as the
    /// first act of every destructor in the hierarchy. See ShaderNodeLiveness.
    std::shared_ptr<ShaderNodeLiveness> m_liveness = std::make_shared<ShaderNodeLiveness>();

    // ── Uniform Extension ──────────────────────────────────────────────
    std::shared_ptr<PhosphorShaders::IUniformExtension> m_uniformExtension;
    /// Reused staging buffer for extension uploads — resized only when the
    /// extension's reported size changes (avoids a render-thread QByteArray
    /// allocation every frame that the extension is dirty).
    QByteArray m_extensionStaging;

    // ── Shader Include Paths ───────────────────────────────────────────
    QStringList m_shaderIncludePaths;

    // ── Parameter preamble (generated `#define p_<id> ...`) ───────────
    /// Spliced after `#version` into the fragment source at load time and
    /// fingerprinted into the bake-cache key. Empty = no-op.
    QString m_paramPreamble;

    // ── Entry-point scaffold (T1.4) ────────────────────────────────────
    /// Prologue prepended (and candidate `main()` appended) to an entry-only
    /// fragment source before expansion; fingerprinted into the bake-cache key.
    /// Empty prologue + empty candidates = assembly disabled.
    QString m_entryPrologue;
    QList<PhosphorShaders::EntryCandidate> m_entryCandidates;

    /// Effective "tessellated image pass" predicate — see m_gridBuffersFailed.
    /// The pipeline topology and render()'s draw arm must agree on this, or a
    /// Triangles pipeline ends up drawing the 4-vertex strip quad.
    bool gridActive() const
    {
        return m_gridSubdivisions > 0 && !m_gridBuffersFailed;
    }

    // ── RHI Core Resources ─────────────────────────────────────────────
    std::unique_ptr<QRhiBuffer> m_vbo;
    /// Image-pass grid mesh (setGridSubdivisions > 0). Null in quad mode.
    std::unique_ptr<QRhiBuffer> m_gridVbo;
    std::unique_ptr<QRhiBuffer> m_gridIbo;
    int m_gridSubdivisions = 0;
    int m_gridIndexCount = 0;
    bool m_gridUploaded = false;
    /// Latched when the grid VBO/IBO create() failed, so prepare() does not
    /// retry (and warn) every frame — the owning item re-pushes its own
    /// m_gridSubdivisions on every sync, so zeroing the count here would
    /// immediately be undone by setGridSubdivisions and the create retried
    /// at 60Hz. Cleared on a density change and on releaseRhiResources()
    /// (a new device deserves a fresh attempt). While latched the node runs
    /// in quad mode: gridActive() is the single predicate both the pipeline
    /// topology and render()'s draw arm consult.
    bool m_gridBuffersFailed = false;
    std::unique_ptr<QRhiBuffer> m_ubo;
    std::unique_ptr<QRhiShaderResourceBindings> m_srb;
    std::unique_ptr<QRhiGraphicsPipeline> m_pipeline;
    QShader m_vertexShader;
    QShader m_fragmentShader;
    QVector<quint32> m_renderPassFormat;

    // ── Multi-pass: buffer pass(es) (optional). Up to kMaxBufferPasses paths. ──
    QString m_bufferPath;
    QStringList m_bufferPaths;
    bool m_bufferFeedback = false;
    qreal m_bufferScale = 1.0;
    // Per-pass render-target scales (a pack's `bufferScales`), positionally
    // aligned with m_bufferPaths. A slot without its own entry follows
    // m_bufferScale, which setBufferScale writes into every slot; only
    // setBufferScales diverges them. This is what lets a pyramid (dual Kawase)
    // render each level at its own resolution inside one pack.
    //
    // Because setBufferScale SEEDS all of these, it must not compare against
    // them to decide whether anything changed: the sync pushes it first and
    // setBufferScales re-diverges the slots immediately after, so a comparison
    // here would see its own seed undone every frame and rebuild every buffer
    // target twice per frame. It compares the single value alone. See its own
    // body for the matching note.
    std::array<qreal, kMaxBufferPasses> m_bufferScales = []() {
        std::array<qreal, kMaxBufferPasses> a;
        a.fill(1.0);
        return a;
    }();
    // Buffer-pass texel format. Half-float by default so packs that store HDR
    // radiance, signed data, or feedback accumulators keep full precision; a
    // pack whose buffers hold plain clamped [0,1] colour opts down to RGBA8
    // via metadata to halve buffer bandwidth (which is what integrated GPUs
    // are starved of).
    bool m_halfFloatBuffers = true;
    std::array<QString, kMaxBufferPasses> m_bufferWraps = []() {
        std::array<QString, kMaxBufferPasses> a;
        a.fill(QStringLiteral("clamp"));
        return a;
    }();
    QString m_bufferWrapDefault = QStringLiteral("clamp");
    std::array<QString, kMaxBufferPasses> m_bufferFilters = []() {
        std::array<QString, kMaxBufferPasses> a;
        a.fill(QStringLiteral("linear"));
        return a;
    }();
    QString m_bufferFilterDefault = QStringLiteral("linear");
    QString m_bufferFragmentShaderSource;
    QShader m_bufferFragmentShader;
    bool m_bufferShaderReady = false;
    bool m_bufferShaderDirty = true;
    int m_bufferShaderRetries = 0;
    std::unique_ptr<QRhiTexture> m_bufferTexture;
    std::unique_ptr<QRhiRenderPassDescriptor> m_bufferRenderPassDescriptor;
    std::unique_ptr<QRhiTextureRenderTarget> m_bufferRenderTarget;
    std::array<std::unique_ptr<QRhiSampler>, kMaxBufferPasses> m_bufferSamplers;
    std::unique_ptr<QRhiShaderResourceBindings> m_bufferSrb;
    std::unique_ptr<QRhiGraphicsPipeline> m_bufferPipeline;
    QVector<quint32> m_bufferRenderPassFormat;
    // Ping-pong (bufferFeedback): second texture/RT/SRB for buffer pass; image pass has two SRBs
    std::unique_ptr<QRhiTexture> m_bufferTextureB;
    std::unique_ptr<QRhiRenderPassDescriptor> m_bufferRenderPassDescriptorB;
    std::unique_ptr<QRhiTextureRenderTarget> m_bufferRenderTargetB;
    std::unique_ptr<QRhiShaderResourceBindings> m_bufferSrbB;
    std::unique_ptr<QRhiShaderResourceBindings> m_srbB; // image pass SRB with binding 2 = texture B
    bool m_bufferFeedbackCleared = false;

    // Multi-buffer mode (2-8 passes): per-pass resources
    std::array<std::unique_ptr<QRhiTexture>, kMaxBufferPasses> m_multiBufferTextures = {};
    std::array<std::unique_ptr<QRhiTextureRenderTarget>, kMaxBufferPasses> m_multiBufferRenderTargets = {};
    std::array<std::unique_ptr<QRhiRenderPassDescriptor>, kMaxBufferPasses> m_multiBufferRenderPassDescriptors = {};
    std::array<std::unique_ptr<QRhiGraphicsPipeline>, kMaxBufferPasses> m_multiBufferPipelines = {};
    std::array<std::unique_ptr<QRhiShaderResourceBindings>, kMaxBufferPasses> m_multiBufferSrbs = {};
    std::array<QShader, kMaxBufferPasses> m_multiBufferFragmentShaders = {};
    bool m_multiBufferShadersReady = false;
    bool m_multiBufferShaderDirty = true;
    int m_multiBufferShaderRetries = 0;
    // Dummy 1x1 texture for the multipass channel-0 buffer slot (SRB
    // binding 2, GLSL `iChannel0`) when multipass is configured but the
    // backing buffer hasn't been created yet. Distinct from the user-
    // texture slot 0 (SRB binding 11, GLSL `uTexture0`) — the iChannel0
    // name here refers to the buffer-channel binding, not the renamed
    // user-texture.
    std::unique_ptr<QRhiTexture> m_dummyChannelTexture;
    std::unique_ptr<QRhiSampler> m_dummyChannelSampler;
    bool m_dummyChannelTextureNeedsUpload = false;

    // ── Shader Sources ─────────────────────────────────────────────────
    QString m_vertexShaderSource;
    QString m_fragmentShaderSource;
    QString m_vertexPath;
    QString m_fragmentPath;
    qint64 m_vertexMtime = 0;
    qint64 m_fragmentMtime = 0;
    /// Include-file fingerprints (path + mtime per transitively-included
    /// header), computed AT LOAD TIME — the moment the includes were read —
    /// and folded into the bake-cache key. Statting the includes again at
    /// bake time would open a TOCTOU window: an include edited between
    /// expansion and fingerprinting caches old-content SPIR-V under the
    /// new-mtime key, serving stale bakes until the mtime moves again.
    QByteArray m_vertexIncludeFp;
    QByteArray m_fragmentIncludeFp;
    QString m_shaderError;
    bool m_initialized = false;
    bool m_vboUploaded = false;
    bool m_shaderReady = false;
    bool m_shaderDirty = true;
    bool m_uniformsDirty = true;
    bool m_timeDirty = true;
    bool m_timeHiDirty = true; ///< iTimeHi wrap offset changed (rare)
    bool m_sceneDataDirty = true; ///< Scene header (resolution, mouse, date, params) changed
    bool m_appFieldsDirty = false; ///< Only appField0/appField1 changed (8-byte upload, not full scene header)
    bool m_didFullUploadOnce = false;
    /// Whether the last prepared frame rendered into a texture render target
    /// (a ShaderEffectSource layer) rather than the window. The qt_Matrix NDC
    /// Y-flip is per-render-target; prepare() forces a full UBO re-upload when
    /// this flips (hideSource capture attach/detach retargets the same node).
    /// Unset until the first prepare().
    std::optional<bool> m_lastTargetWasTexture;

    // ── UBO Profile (pluggable uniform buffer concern) ─────────────────
    /// Owns the concrete UBO struct, its byte size, per-frame fill, and the
    /// dirty-region dispatch. Installed by the ctor (BaseUniformProfile by
    /// default). The iDate 1 Hz throttle's lastDateRefreshMs lives inside the
    /// profile now.
    std::unique_ptr<PhosphorShaders::IUboProfile> m_uboProfile;

    /// Node-side mirrors of the consumer escape-hatch int slots. The profile
    /// owns the authoritative bytes (and may not expose a getter), so these
    /// mirrors provide the cheap value-changed gate setAppField0/1 need to
    /// avoid dirtying the UBO when nothing actually changed.
    int m_appField0 = 0;
    int m_appField1 = 0;

    // Full-precision elapsed seconds (double). Split into iTime (wrapped lo) +
    // iTimeHi (wrap offset) at upload.
    double m_time = 0.0;
    float m_timeDelta = 0.0f;
    int m_frame = 0;
    bool m_isReversed = false;
    float m_timeHi = 0.0f; // Cached iTimeHi for wrap-offset change detection
    float m_width = 0.0f;
    float m_height = 0.0f;
    /// Lock-free snapshot of the most recently observed item geometry, written
    /// from prepare() OUTSIDE m_itemMutex and read from rect() without locking.
    /// prepare() takes the mutex only to snapshot the item-derived values, drops
    /// it, and publishes these afterwards; the release/acquire pair is what
    /// carries the ordering, and it is sound precisely because these atomics are
    /// independent of the m_item pointer and rect() never touches it. (The
    /// header used to claim the write happened under the mutex, which is the
    /// opposite of what the implementation deliberately does.)
    /// rect() is invoked once per cull pass on every node in the scene; locking
    /// m_itemMutex there fights with prepare()/render() for the same mutex
    /// every frame. The atomics carry "best-effort, last-known" geometry —
    /// stale by at most one frame, which the cull pass tolerates.
    std::atomic<float> m_cachedWidth{0.0f};
    std::atomic<float> m_cachedHeight{0.0f};
    QPointF m_mousePosition;

    // ── Surface-only state (consumed by a SurfaceUniformProfile; ignored by
    //    the BaseUniformProfile). Defaults mirror UboFrameState so the overlay
    //    path is byte-identical whether or not these are ever touched. ──
    float m_surfaceOpacity = 1.0f;
    float m_surfaceScale = 1.0f;
    bool m_surfaceFocused = false;
    float m_surfaceSize[2] = {0.0f, 0.0f};
    float m_backdropRect[4] = {0.0f, 0.0f, 1.0f, 1.0f};
    float m_surfaceFrameTopLeft[2] = {0.0f, 0.0f};
    float m_surfaceFrameSize[2] = {0.0f, 0.0f};

    // ── Custom Parameters (indexed) ────────────────────────────────────
    std::array<QVector4D, kMaxCustomParams> m_customParams;
    std::array<QColor, kMaxCustomColors> m_customColors;

    // ── Extra Bindings (consumer-managed) ──────────────────────────────
    struct ExtraBinding
    {
        QRhiTexture* texture = nullptr;
        QRhiSampler* sampler = nullptr;
    };
    // std::map (ordered) so SRB construction produces a deterministic binding
    // order regardless of insertion/erasure history. An unordered_map can
    // produce different iteration orders after erasures, which Qt RHI
    // backends may hash into different pipeline layout signatures.
    std::map<int, ExtraBinding> m_extraBindings;

    // ── 1x1 Transparent Fallback ───────────────────────────────────────
    QImage m_transparentFallbackImage;

    // ── Audio spectrum texture (binding 10) ─────────────────────────────
    QVector<float> m_audioSpectrum;
    std::unique_ptr<QRhiTexture> m_audioSpectrumTexture;
    std::unique_ptr<QRhiSampler> m_audioSpectrumSampler;
    bool m_audioSpectrumDirty = false;

    // ── User texture slots (bindings 11-14) ─────────────────────────────
    std::array<QImage, kMaxUserTextures> m_userTextureImages;
    std::array<std::unique_ptr<QRhiTexture>, kMaxUserTextures> m_userTextures;
    std::array<std::unique_ptr<QRhiSampler>, kMaxUserTextures> m_userTextureSamplers;
    // Seeded with the explicit "clamp" token rather than default-constructed,
    // matching m_bufferWraps above, so the two arrays answer the same string to
    // anything that compares tokens rather than normalising them.
    //
    // Filled from kMaxUserTextures rather than written out as four literals: the
    // count comes from the binding table, so a literal list silently stops
    // covering the array the moment the table grows a slot.
    std::array<QString, kMaxUserTextures> m_userTextureWraps = []() {
        std::array<QString, kMaxUserTextures> a;
        a.fill(QStringLiteral("clamp"));
        return a;
    }();
    std::array<bool, kMaxUserTextures> m_userTextureDirty = {};
    /// One warning per slot for a failed sampler create. The retry itself is
    /// per-frame by design (the slot stays dirty), so without this the failure
    /// line repeats at vsync for as long as the backend stays unhappy.
    std::array<bool, kMaxUserTextures> m_userTextureSamplerWarned = {};

    // ── Source texture override (slot 0 / binding 11) ───────────────────
    // Texture-provider source — typically a `QQuickItem::textureProvider()`
    // for a layer-enabled item. When non-null this supersedes
    // m_userTextures[0] in the SRB build, so the shader's uTexture0
    // samples a live QML render instead of a static QImage upload.
    // m_sourceSampler is owned here (not a user-texture sampler) so we
    // can keep slot 0's user-texture sampler available for callers that
    // mix the two paths. m_lastSourceRhiTexture caches the QRhiTexture*
    // we last bound; when the provider's underlying texture changes
    // (FBO recreation on resize / device-loss) we drop the SRB so the
    // next rebuild picks up the new pointer.
    QPointer<QSGTextureProvider> m_sourceTextureProvider;
    /// textureChanged → markDirty(DirtyMaterial) bridge for the provider
    /// above. The scene graph cannot see a dependency held as a raw
    /// provider pointer, so without this an enclosing QSGLayer (an item
    /// capturing THIS node's item, e.g. the SurfaceAnimator's transition
    /// capture) never re-grabs when the sampled layer re-renders — the
    /// capture freezes on its creation-time grab (empty on a fresh show)
    /// for the whole leg. Disconnected on provider swap and in the dtor;
    /// provider destruction auto-disconnects (sender-owned).
    QMetaObject::Connection m_sourceTextureChangedConn;
    std::unique_ptr<QRhiSampler> m_sourceSampler;
    QRhiTexture* m_lastSourceRhiTexture = nullptr;
    /// Latch — once the source-provider sampler has failed to create, we stop
    /// retrying every paint pass (avoids per-frame rhi->newSampler() churn when
    /// the backend is unhappy). Cleared in releaseRhiResources() so a device
    /// reset re-attempts.
    bool m_sourceSamplerFailed = false;
    /// Single-shot warning latch: emitted once per node when we detect that
    /// the source provider's QRhiTexture comes from a different QRhi than our
    /// own (cross-window provider). Prevents log spam.
    bool m_warnedForeignRhi = false;
    /// One-shot: the wallpaper binding had neither a wallpaper nor a dummy substitute.
    /// Mutable because appendWallpaperBinding() is const — it only reads state
    /// to build the binding list, and this is a diagnostic latch, not state the
    /// binding depends on.
    mutable bool m_warnedWallpaperBindingOmitted = false;
    /// One-shot latches for the audio-spectrum diagnostics. Both conditions
    /// persist across frames by design (an oversized vector stays oversized;
    /// the create() retry is per-frame), so without a latch each would log at
    /// spectrum cadence. Cleared in releaseRhiResources() beside the RHI latch.
    bool m_warnedAudioTruncated = false;
    bool m_warnedAudioCreateFailed = false;
    /// 1×1 transparent fallback texture used when a source provider is set
    /// but has not yet produced a usable QRhiTexture (or its texture lives
    /// on a foreign QRhi). Bound at slot 0 instead of falling through to
    /// the QImage user-texture path — the documented contract for
    /// setSourceTextureProvider is "supersedes whatever QImage was uploaded
    /// at slot 0", so a transient null must not unmask the QImage. Allocated
    /// lazily on first use; released in releaseRhiResources().
    std::unique_ptr<QRhiTexture> m_transparentFallbackTexture;
    /// Latch for the 1x1 transparent fallback's create failure. That allocation
    /// is retried on EVERY prepare() while a source provider is set and
    /// unresolved, and it used to fail in complete silence.
    bool m_transparentFallbackWarned = false;
    bool m_transparentFallbackTextureNeedsUpload = false;

    // ── Depth buffer (binding 16) ──────────────────────────────────────
    bool m_useDepthBuffer = false;
    bool m_depthMultiBufferWarned = false;
    /// Latch for the once-per-node warning that a depth pack's declared per-pass
    /// bufferScales are discarded. Cleared with the rest on a device reset.
    bool m_depthScalesWarned = false;
    std::unique_ptr<QRhiTexture> m_depthTexture;
    std::unique_ptr<QRhiSampler> m_depthSampler;

    // ── Desktop wallpaper texture (binding 15) ─────────────────────────
    bool m_useWallpaper = false;
    QImage m_wallpaperImage;
    std::unique_ptr<QRhiTexture> m_wallpaperTexture;
    std::unique_ptr<QRhiSampler> m_wallpaperSampler;
    bool m_wallpaperDirty = false;
};

/** Result of warmShaderBakeCacheForPaths for reporting to UI. */
struct WarmShaderBakeResult
{
    bool success = false;
    QString errorMessage;
};

/**
 * Pre-load cache warming: load, bake, and insert shaders for the given paths into the
 * shared bake cache. Safe to call from any thread.
 * @param vertexPath    Path to the vertex shader file
 * @param fragmentPath  Path to the fragment shader file
 * @param includePaths  Directories to search for #include directives
 * @param paramPreamble Generated `#define p_<id> ...` block spliced after the
 *                      fragment shader's `#version` (T1.1). MUST match what the
 *                      live `ShaderNodeRhi::loadFragmentShader` splices for the
 *                      same effect, since both compute the same bake-cache key
 *                      (which is fingerprinted on the preamble) — a mismatch
 *                      would let this warm entry serve the wrong SPIR-V to the
 *                      live load. Empty (the default) is a no-op splice.
 * @param entryPrologue   T1.4 entry-point prologue (`#version`/include/in-out)
 *                        prepended to an entry-only fragment source before
 *                        expansion. Empty (default) disables assembly.
 * @param entryCandidates T1.4 entry functions + their generated `main()`,
 *                        applied when the fragment defines no `main()`. MUST
 *                        match what `ShaderNodeRhi::loadFragmentShader` uses for
 *                        the same shader so warm + live agree on key and source.
 * @return success and error message for UI reporting
 */
PHOSPHORRENDERING_EXPORT WarmShaderBakeResult warmShaderBakeCacheForPaths(
    const QString& vertexPath, const QString& fragmentPath, const QStringList& includePaths = {},
    const QString& paramPreamble = {}, const QString& entryPrologue = {},
    const QList<PhosphorShaders::EntryCandidate>& entryCandidates = {});

} // namespace PhosphorRendering
