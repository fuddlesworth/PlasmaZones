// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorRendering/ShaderEffect.h>
#include <PhosphorRendering/ShaderNodeRhi.h>

#include "internal.h"

#include <PhosphorShaders/CustomParamsKey.h>

#include <QJSValue>
#include <QMutexLocker>
#include <QVariant>

namespace PhosphorRendering {

// Keep the slot-array sizes in lock-step with the key producer's budgets. The
// loops below index `m_customParams` / `m_customColors` by the slot number the
// key encodes, so a drift between the two would either drop the tail slots the
// producer can emit or read past what the arrays hold. Same form and reason as
// the kMaxUserTextureSlots assert in shadereffect.cpp.
static_assert(kMaxCustomParams == PhosphorShaders::CustomParams::kVecCount,
              "ShaderNodeRhi::kMaxCustomParams must equal PhosphorShaders::CustomParams::kVecCount");
static_assert(kMaxCustomColors == PhosphorShaders::CustomColors::kColorCount,
              "ShaderNodeRhi::kMaxCustomColors must equal PhosphorShaders::CustomColors::kColorCount");

QVariant peelQmlVariant(const QVariant& value)
{
    // Bounded loop, not `while (true)`: a wrapper chain this deep is already
    // pathological, and an unbounded peel would hang on a self-referencing
    // one. Five iterations is the previous maximum peel depth (one QJSValue
    // step ahead of a four-deep QVariant loop) preserved exactly, with the
    // QJSValue check moved INSIDE the loop so a chain that terminates in a
    // QJSValue — rather than starting with one — is peeled too.
    QVariant unwrapped = value;
    for (int depth = 0; depth < 5; ++depth) {
        const int id = unwrapped.metaType().id();
        if (id == qMetaTypeId<QJSValue>()) {
            unwrapped = unwrapped.value<QJSValue>().toVariant();
        } else if (id == QMetaType::QVariant) {
            unwrapped = unwrapped.value<QVariant>();
        } else {
            break;
        }
    }
    return unwrapped;
}

// ============================================================================
// Pre-built per-slot key tables for setShaderParams
// ============================================================================
//
// `setShaderParams` is called per-frame on transition shaders that drive
// the customParams uniforms from a QML binding, and per-paint on shaders
// that re-push the texture map. Constructing 12 fresh QString objects via
// `QString::arg("uTexture%1").arg(i)` for the texture / wrap / svgSize
// keys on every call regardless of whether the params map references
// them is wasted heap traffic. Pre-bake the keys into static
// `QLatin1String` lookup tables (constexpr-constructible since Qt 6.4;
// project minimum is 6.6 — see top-level CMakeLists.txt) and index the
// loop by slot. The keys are still rendered to the same QString lookup
// path inside `QVariantMap::contains` / `value`, but we avoid the
// per-call allocation when the map doesn't carry the key in the first
// place. Sizes pinned to `kMaxUserTextureSlots` so adding a slot
// requires growing the literal array (compile-time aggregate-init
// failure if mis-sized) — keeps the trio in lock-step.
namespace {
constexpr std::array<QLatin1String, kMaxUserTextureSlots> kUserTexturePathKeys = {
    QLatin1String("uTexture0"),
    QLatin1String("uTexture1"),
    QLatin1String("uTexture2"),
    QLatin1String("uTexture3"),
};
constexpr std::array<QLatin1String, kMaxUserTextureSlots> kUserTextureWrapKeys = {
    QLatin1String("uTexture0_wrap"),
    QLatin1String("uTexture1_wrap"),
    QLatin1String("uTexture2_wrap"),
    QLatin1String("uTexture3_wrap"),
};
constexpr std::array<QLatin1String, kMaxUserTextureSlots> kUserTextureSvgSizeKeys = {
    QLatin1String("uTexture0_svgSize"),
    QLatin1String("uTexture1_svgSize"),
    QLatin1String("uTexture2_svgSize"),
    QLatin1String("uTexture3_svgSize"),
};
} // namespace

// ============================================================================
// SVG rasterisation safety caps
// ============================================================================
//
// The per-axis ceiling (`kMaxSvgDimension`) and byte budget
// (`kMaxSvgPixelBytes`) are public `static constexpr` members on
// `ShaderEffect` so consumers (UI sliders, validators, tests) can mirror the
// clamp without hardcoding the value. See ShaderEffect.h for the contract.

// ============================================================================
// Shader Parameters / Buffer Setters
// ============================================================================

void ShaderEffect::setShaderParams(const QVariantMap& params)
{
    // Normal fast path: byte-equal params map means there's nothing to
    // re-parse and no GPU-visible state to mutate. ONE exception:
    // `setUserTexture` may have intervened since the last params push,
    // clearing the per-slot path cache and storing a QImage directly.
    // In that case the unchanged `uTextureN` entry in `params` must be
    // honoured (re-loaded from disk) so the path-driven contract wins
    // back over the direct push. We bypass the early-return exactly
    // once per intervening direct push; the flag is cleared at the end
    // of the parse branch below.
    if (m_shaderParams == params && !m_userTexturesDirectlyOverridden) {
        return;
    }
    // Always update the stored map BEFORE the per-key parsing below — the
    // next call's equality short-circuit depends on this cache being
    // current, even when the only differences between the previous and
    // incoming map are irrelevant keys (no customParams / customColor /
    // uTexture* / *_wrap / *_svgSize entries). Without an unconditional
    // cache update, an irrelevant-key churn (e.g. a sibling QML binding
    // that round-trips an unrelated metadata field through the params
    // map) would re-enter the full parse on every call because the
    // equality check stayed false forever.
    //
    // Whether the stored map actually changed is tracked separately: the
    // `shaderParams` Q_PROPERTY reads this member, so anything bound to it
    // goes stale if a push that differs only in keys this decoder ignores
    // updates the member without notifying. Property notification and
    // scene-graph invalidation are separate concerns — the notify follows the
    // member, `update()` follows `anyMutation` below.
    const bool storedParamsChanged = (m_shaderParams != params);
    m_shaderParams = params;

    // Per-call mutation tracker. The stored-map update above keeps the
    // equality check honest, but `update()` should only fire when this
    // call actually changed a
    // uniform / texture / colour slot the GPU consumes — payloads that
    // differ only in irrelevant keys carry no rendering-visible change
    // and should not invalidate the scene graph. Each branch below that
    // mutates a member sets `anyMutation = true`; the trailing `update()`
    // checks the flag.
    bool anyMutation = false;

    // Parse the canonical slot-keyed entries that both registries
    // (`PhosphorShaders::ShaderRegistry::translateParamsToUniforms` for
    // overlay shaders, `PhosphorAnimationShaders::AnimationShaderRegistry::
    // translateAnimationParams` for animation shaders) emit:
    //   • `customParams1_x` … `customParams8_w` → `m_customParams[0..7]`
    //   • `customColor1`     … `customColor16`  → `m_customColors[0..15]`
    //
    // Slot-key format comes from `PhosphorShaders::CustomParams::slotKey`
    // — the cross-library canonical helper alongside `BaseUniforms`.
    //
    // Until this lived in the base class, only `ZoneShaderItem` (overlay)
    // performed the parse — animation shaders driven by bare `ShaderEffect`
    // (e.g. `SurfaceAnimator::runLeg` for daemon overlay-surface
    // transitions) silently dropped every declared parameter. Now any
    // consumer that calls `setShaderParams` with translated keys lands the
    // values in the UBO via `setCustomParamAt` / `setCustomColorAt`.
    // Single-pass key scan to short-circuit the 32 + 16 hash lookups when the
    // payload contains no slot-keyed entries (the common case for callers that
    // only push, e.g., uTextureN paths or shader-source URLs). Both helpers
    // emit fixed prefixes (`customParams`, `customColor`); a single startsWith
    // pass over the params keys is cheaper than 48 hash misses on every call.
    bool hasParamsKey = false;
    bool hasColorKey = false;
    for (auto it = params.constBegin(); it != params.constEnd() && !(hasParamsKey && hasColorKey); ++it) {
        const QString& key = it.key();
        if (!hasParamsKey && key.startsWith(QLatin1String("customParams"))) {
            hasParamsKey = true;
        } else if (!hasColorKey && key.startsWith(QLatin1String("customColor"))) {
            hasColorKey = true;
        }
    }

    if (hasParamsKey) {
        auto extractFloat = [&params](const QString& key, float defaultVal) -> float {
            const auto it = params.constFind(key);
            if (it == params.constEnd()) {
                return defaultVal;
            }
            bool ok = false;
            const float val = it->toFloat(&ok);
            return ok ? val : defaultVal;
        };

        for (int i = 0; i < kMaxCustomParams; ++i) {
            const QVector4D before = customParamAt(i);
            QVector4D cp = before;
            cp.setX(extractFloat(PhosphorShaders::CustomParams::slotKey(i, 'x'), cp.x()));
            cp.setY(extractFloat(PhosphorShaders::CustomParams::slotKey(i, 'y'), cp.y()));
            cp.setZ(extractFloat(PhosphorShaders::CustomParams::slotKey(i, 'z'), cp.z()));
            cp.setW(extractFloat(PhosphorShaders::CustomParams::slotKey(i, 'w'), cp.w()));
            if (cp != before) {
                setCustomParamAt(i, cp);
                anyMutation = true;
            }
        }
    }

    if (hasColorKey) {
        auto extractColor = [&params](const QString& key, const QColor& defaultVal) -> QColor {
            const auto it = params.constFind(key);
            if (it == params.constEnd()) {
                return defaultVal;
            }
            const QVariant& val = *it;
            // QString FIRST, not last. QtGui registers a QString→QColor
            // converter, so `canConvert<QColor>()` answers true for EVERY
            // string — which made the string branch below unreachable and,
            // worse, handed an unparseable name ("", "notacolor") straight
            // through as an INVALID QColor that then got stored and uploaded.
            // Parsing here and falling back on failure is what that branch was
            // always meant to do.
            if (val.typeId() == QMetaType::QString) {
                const QColor color(val.toString());
                return color.isValid() ? color : defaultVal;
            }
            if (val.canConvert<QColor>()) {
                // Same invalid-result guard for the generic path: a converter
                // that fails still yields a default-constructed (invalid)
                // QColor rather than signalling the failure.
                const QColor color = val.value<QColor>();
                if (color.isValid()) {
                    return color;
                }
            }
            return defaultVal;
        };

        for (int i = 0; i < kMaxCustomColors; ++i) {
            const QColor before = customColorAt(i);
            const QColor next = extractColor(PhosphorShaders::CustomColors::colorKey(i), before);
            if (next != before) {
                setCustomColorAt(i, next);
                anyMutation = true;
            }
        }
    }

    // ── User textures (uTexture0..3, uTexture0_wrap, uTexture0_svgSize) ──
    //
    // Single shared path for both runtimes that drive a ShaderEffect:
    //   • Overlay zone backgrounds (ZoneShaderItem inherits this class)
    //   • Animation overlay-surface transitions (SurfaceAnimator runs the
    //     animation shader through a ShaderEffect on a layer-enabled
    //     anchor; pack-bundled textures from metadata.json arrive in
    //     `params` after AnimationShaderRegistry merges them with any
    //     per-leg runtime overrides)
    //
    // Format mirrors the params keys ZoneShaderItem already accepted
    // before unification:
    //   • `uTextureN`       — file path (relative paths are NOT resolved
    //                         here; the caller hands us absolute paths so
    //                         the loader stays caller-agnostic)
    //   • `uTextureN_wrap`  — "clamp" / "repeat" / "mirror" (empty defaults
    //                         to the runtime's clamp behaviour)
    //   • `uTextureN_svgSize` — SVG rasterise max-axis dimension in logical
    //                         pixels (clamped 64..2048; ignored for bitmap
    //                         formats)
    //
    // Path-change detection: we track the last-resolved path per slot and
    // skip the file load when the path is unchanged. SVG-size changes
    // force a re-rasterise of the same path so a slider can drive
    // resolution live without the consumer having to re-emit the path.
    //
    // Slot-key strings come from the pre-baked tables at the top of this
    // file — `kUserTextureSvgSizeKeys`, `kUserTexturePathKeys`, and
    // `kUserTextureWrapKeys` — to skip the per-call `QString::arg(i)`
    // parse that the previous `arg`-formatted lookup paid on every
    // iteration regardless of which (if any) of the keys the params map
    // references. A lookup still synthesises a temporary `QString` from the
    // QLatin1String, so we bind it to a local `QString` once per slot per key,
    // and we probe with a single `constFind` whose iterator carries the value
    // — one hash lookup per key instead of the contains+value pair, matching
    // the float / colour extractors above. Tables sized to `kMaxUserTextures`, which
    // is also the loop bound (pinned equal to the contract's
    // kMaxUserTextureSlots by the static_assert in shadereffect.cpp).
    for (int i = 0; i < kMaxUserTextures; ++i) {
        const QString sizeKey(kUserTextureSvgSizeKeys[i]);
        bool svgSizeChanged = false;
        const auto sizeIt = params.constFind(sizeKey);
        if (sizeIt != params.constEnd()) {
            // Use the `bool ok` parse pattern (matches the float / colour
            // extractors above). Without it, a non-numeric or missing-but-
            // present QVariant returns 0 from toInt, then qBound(64, 0, max)
            // = 64 — silently substituting the floor for any malformed
            // value rather than retaining the prior, possibly user-tuned
            // size. Skip the assignment entirely on parse failure so the
            // prior value persists and a malformed setting can't downgrade
            // the rasterise resolution.
            bool ok = false;
            // One hash lookup per key, reusing the iterator for the value —
            // matches the `constFind` form the float / colour extractors above
            // already use instead of a contains+value pair.
            const int v = sizeIt->toInt(&ok);
            if (ok) {
                const int clamped = qBound(kMinSvgDimension, v, kMaxSvgDimension);
                if (m_userTextureSvgSizes[i] != clamped) {
                    m_userTextureSvgSizes[i] = clamped;
                    svgSizeChanged = true;
                    anyMutation = true;
                }
            }
        }

        // Same single-lookup `constFind` form as `sizeIt` above.
        const QString texKey(kUserTexturePathKeys[i]);
        const auto texIt = params.constFind(texKey);
        const bool hasTexKey = (texIt != params.constEnd());
        const QString incomingPath = hasTexKey ? texIt->toString() : m_userTexturePaths[i];
        const bool pathChanged = hasTexKey && (m_userTexturePaths[i] != incomingPath);
        const bool needsReload = pathChanged || (svgSizeChanged && !m_userTexturePaths[i].isEmpty());

        if (pathChanged) {
            m_userTexturePaths[i] = incomingPath;
            anyMutation = true;
        }

        if (needsReload) {
            const QString path = m_userTexturePaths[i];
            // Drop the preceding `QFile::exists()` check: it's a TOCTOU
            // race against the load below (file can vanish between the
            // two calls), so it cannot serve as a true gate. The
            // QImage / QSvgRenderer constructors already produce a null
            // result on missing-file, and the keep-prior-image branch
            // below (where `loaded.isNull()` falls through to the
            // existing `m_userTextureImages[i]`) handles that case
            // correctly. The exists() call only added a redundant
            // stat() per setShaderParams call.
            const int maxDim = qBound(kMinSvgDimension, m_userTextureSvgSizes[i], kMaxSvgDimension);
            QImage loaded = path.isEmpty() ? QImage() : loadUserTextureFile(path, maxDim);
            // Empty path → intentional clear (sampler reads transparent black).
            // Non-empty path that produced a null image → load failure (file
            // missing, parse error, OOM). In the failure case, KEEP the prior
            // image so a transient FS hiccup or a half-written replacement
            // file doesn't drop a previously-valid texture mid-session;
            // log a warning so the author notices.
            if (path.isEmpty() || !loaded.isNull()) {
                if (m_userTextureImages[i].cacheKey() != loaded.cacheKey()) {
                    m_userTextureImages[i] = loaded;
                    anyMutation = true;
                }
            } else {
                qCWarning(lcShaderNode) << "ShaderEffect: failed to load user texture slot" << i << "from" << path
                                        << "— keeping previously-loaded image";
            }
        }

        // Same single-lookup `constFind` form as `sizeIt` / `texIt` above.
        const QString wrapKey(kUserTextureWrapKeys[i]);
        const auto wrapIt = params.constFind(wrapKey);
        if (wrapIt != params.constEnd()) {
            // Normalize-then-guard, mirroring setUserTextureWrap below: a raw
            // store lets two spellings that normalize identically ("" vs
            // "clamp") defeat the equality guard and fire a spurious update()
            // per push. The registries currently emit only canonical tokens,
            // so this is consistency hardening, not a live-producer fix.
            const QString incomingWrap = ShaderNodeRhi::normalizeWrapMode(wrapIt->toString());
            if (m_userTextureWraps[i] != incomingWrap) {
                m_userTextureWraps[i] = incomingWrap;
                anyMutation = true;
            }
        }
    }

    // Re-parse complete: any pending direct-push override has now been
    // honoured (or the loop above had nothing to reload). Clear the
    // dirty flag so the equality short-circuit can resume on the next
    // call. Doing this here, after the full per-slot scan, guarantees
    // the cleared-path / unchanged-params race window described on
    // `setUserTexture` is closed exactly once per intervening direct
    // push and never permanently degrades the early-return fast path.
    m_userTexturesDirectlyOverridden = false;

    if (storedParamsChanged) {
        Q_EMIT shaderParamsChanged();
    }
    if (anyMutation) {
        update();
    }
}

void ShaderEffect::setBufferShaderPath(const QString& path)
{
    if (m_bufferShaderPath == path) {
        return;
    }
    m_bufferShaderPath = path;
    // Coalesce singular/plural updates: mutate both members then emit both
    // signals once. Previously each setter emitted `...Changed` twice and
    // scheduled two scene-graph updates when a QML binding drove the other
    // property; a single update() pass is enough.
    const QStringList newPaths = path.isEmpty() ? QStringList() : QStringList{path};
    const bool pathsChanged = (m_bufferShaderPaths != newPaths);
    if (pathsChanged) {
        m_bufferShaderPaths = newPaths;
    }
    // No m_shaderDirty here: changing buffer paths reloads the BUFFER shader
    // (handled by ShaderNodeRhi's own m_bufferShaderDirty), not the main shader.
    Q_EMIT bufferShaderPathChanged();
    if (pathsChanged) {
        Q_EMIT bufferShaderPathsChanged();
    }
    update();
}

void ShaderEffect::setBufferShaderPaths(const QStringList& paths)
{
    if (m_bufferShaderPaths == paths) {
        return;
    }
    m_bufferShaderPaths = paths;
    const QString newPath = paths.isEmpty() ? QString() : paths.constFirst();
    const bool singularChanged = (m_bufferShaderPath != newPath);
    if (singularChanged) {
        m_bufferShaderPath = newPath;
    }
    // Main shader is unaffected — see setBufferShaderPath above.
    Q_EMIT bufferShaderPathsChanged();
    if (singularChanged) {
        Q_EMIT bufferShaderPathChanged();
    }
    update();
}

void ShaderEffect::setBufferFeedback(bool enable)
{
    if (m_bufferFeedback == enable) {
        return;
    }
    m_bufferFeedback = enable;
    Q_EMIT bufferFeedbackChanged();
    update();
}

void ShaderEffect::setBufferScale(qreal scale)
{
    const qreal clamped = qBound(PhosphorShaders::kMinBufferScale, scale, PhosphorShaders::kMaxBufferScale);
    if (qFuzzyCompare(m_bufferScale, clamped)) {
        return;
    }
    m_bufferScale = clamped;
    Q_EMIT bufferScaleChanged();
    update();
}

void ShaderEffect::setHalfFloatBuffers(bool enable)
{
    if (m_halfFloatBuffers == enable) {
        return;
    }
    m_halfFloatBuffers = enable;
    Q_EMIT halfFloatBuffersChanged();
    update();
}

void ShaderEffect::setBufferWrap(const QString& wrap)
{
    const QString use = ShaderNodeRhi::normalizeWrapMode(wrap);
    if (m_bufferWrap == use) {
        return;
    }
    m_bufferWrap = use;
    Q_EMIT bufferWrapChanged();
    update();
}

void ShaderEffect::setBufferWraps(const QStringList& wraps)
{
    if (m_bufferWraps == wraps) {
        return;
    }
    m_bufferWraps = wraps;
    Q_EMIT bufferWrapsChanged();
    update();
}

void ShaderEffect::setBufferFilter(const QString& filter)
{
    const QString use = ShaderNodeRhi::normalizeFilterMode(filter);
    if (m_bufferFilter == use) {
        return;
    }
    m_bufferFilter = use;
    Q_EMIT bufferFilterChanged();
    update();
}

void ShaderEffect::setBufferFilters(const QStringList& filters)
{
    if (m_bufferFilters == filters) {
        return;
    }
    m_bufferFilters = filters;
    Q_EMIT bufferFiltersChanged();
    update();
}

// ============================================================================
// Indexed accessors — thin wrappers over the slot arrays for callers that
// already know which slot they want (keeps QML-facing Q_PROPERTYs intact while
// removing boilerplate from consumer code that iterates slots).
// ============================================================================

QVector4D ShaderEffect::customParamAt(int index) const
{
    if (index < 0 || index >= static_cast<int>(m_customParams.size())) {
        return QVector4D();
    }
    return m_customParams[index];
}

void ShaderEffect::setCustomParamAt(int index, const QVector4D& params)
{
    if (index < 0 || index >= static_cast<int>(m_customParams.size())) {
        return;
    }
    if (m_customParams[index] == params) {
        return;
    }
    m_customParams[index] = params;
    Q_EMIT customParamsChanged();
    update();
}

QColor ShaderEffect::customColorAt(int index) const
{
    if (index < 0 || index >= static_cast<int>(m_customColors.size())) {
        return QColor();
    }
    return m_customColors[index];
}

void ShaderEffect::setCustomColorAt(int index, const QColor& color)
{
    if (index < 0 || index >= static_cast<int>(m_customColors.size())) {
        return;
    }
    if (m_customColors[index] == color) {
        return;
    }
    m_customColors[index] = color;
    Q_EMIT customColorsChanged();
    update();
}

// ============================================================================
// Audio Spectrum
// ============================================================================

QVariant ShaderEffect::audioSpectrumVariant() const
{
    return QVariant::fromValue(m_audioSpectrum);
}

void ShaderEffect::setAudioSpectrumVariant(const QVariant& spectrum)
{
    // Peel the QML wrappers first, for the same reason
    // setWallpaperTextureVariant does. A JS array handed over through a
    // `createObject` initial-property map arrives as a QJSValue, and
    // `QVariant(QJSValue).toList()` answers an EMPTY list — which used to
    // clear a perfectly good spectrum without a word.
    const QVariant unwrapped = peelQmlVariant(spectrum);

    // Measured (Qt 6.11.2): QML `undefined`, an ABSENT key on a JS object, an
    // undefined `property var`, and both `Binding` shapes ALL arrive as an
    // invalid QVariant. A host letting its source go absent is the ordinary
    // way to say "no spectrum", and the setter cannot tell that apart from a
    // Binding's delivery, so invalid clears — silently, since an absent key is
    // expected rather than an error.
    if (!unwrapped.isValid()) {
        setAudioSpectrum({});
        return;
    }

    // Fast path: QVector<float> from C++ (no per-element conversion)
    if (unwrapped.metaType() == QMetaType::fromType<QVector<float>>()) {
        setAudioSpectrum(unwrapped.value<QVector<float>>());
        return;
    }

    const int typeId = unwrapped.metaType().id();
    if (typeId == QMetaType::Nullptr || typeId == QMetaType::Void) {
        // Deliberate "no spectrum" from the host.
        setAudioSpectrum({});
        return;
    }
    if (!unwrapped.canConvert<QVariantList>()) {
        // Valid but not a list (a QString, a number, an object): converting
        // would silently yield an empty vector and drop the live spectrum.
        qCWarning(lcShaderNode) << "ShaderEffect: audioSpectrum written with an unconvertible value of type"
                                << unwrapped.metaType().name() << "— keeping the previous spectrum";
        return;
    }

    // Slow path: QVariantList from QML (JS array)
    const QVariantList list = unwrapped.toList();
    QVector<float> vec;
    vec.reserve(list.size());
    for (const QVariant& v : list) {
        bool ok = false;
        const float f = v.toFloat(&ok);
        vec.append(ok ? qBound(0.0f, f, 1.0f) : 0.0f);
    }
    if (m_audioSpectrum == vec) {
        return;
    }
    m_audioSpectrum = std::move(vec);
    Q_EMIT audioSpectrumChanged();
    update();
}

void ShaderEffect::setAudioSpectrum(const QVector<float>& spectrum)
{
    // Build the clamped vector first so the value-changed guard reflects
    // the post-clamp state actually pushed to the GPU. Comparing the raw
    // input against the cached (always-clamped) member would falsely
    // diff for two unclamped inputs that clamp to the same result and
    // emit + repaint redundantly.
    QVector<float> clamped;
    clamped.reserve(spectrum.size());
    for (const float v : spectrum) {
        clamped.append(qBound(0.0f, v, 1.0f));
    }
    if (m_audioSpectrum == clamped) {
        return;
    }
    m_audioSpectrum = std::move(clamped);
    Q_EMIT audioSpectrumChanged();
    update();
}

// ============================================================================
// User Textures
// ============================================================================

void ShaderEffect::setUserTexture(int slot, const QImage& image)
{
    if (slot < 0 || slot >= kMaxUserTextures) {
        qCWarning(lcShaderNode) << "setUserTexture: slot" << slot << "out of range [0," << (kMaxUserTextures - 1)
                                << "]";
        return;
    }
    // Value-changed guard: identity image push is a no-op. Without
    // this, repeated calls with the same QImage clobber
    // `m_userTexturePaths[slot]` on every call, so a caller that mixes
    // `setUserTexture(slot, img)` with `setShaderParams({"uTextureN":
    // "..."})` of the SAME path triggers a fresh disk reload on every
    // params push (path-change detection sees the now-empty cache and
    // assumes the path is new). Mirrors the existing cacheKey guard in
    // `ShaderNodeRhi::setUserTexture` (shadernoderhisetters.cpp ShaderNodeRhi::setUserTexture).
    if (m_userTextureImages[slot].cacheKey() == image.cacheKey()) {
        return;
    }
    m_userTextureImages[slot] = image;
    // Clear the cached path so a subsequent setShaderParams call with
    // the previously-cached path correctly forces a reload from disk.
    // Without this, a caller that mixes the direct image setter with
    // params-driven loads would see the directly-set image silently
    // persist when the next params push happens to repeat the old path
    // (path-change detection thinks nothing changed). The guard above
    // ensures this clear only fires when the image actually changed.
    m_userTexturePaths[slot].clear();
    // Companion to the cleared path: signal `setShaderParams` to bypass
    // its `m_shaderParams == params` early-return on the next call.
    // Without this, the byte-equal-params fast path would skip the
    // re-parse and the cleared `uTextureN` cached path would never get
    // a chance to reload — the directly-set QImage above would silently
    // persist past a re-push of the same params map. The flag is reset
    // at the end of the parse branch in `setShaderParams` so the
    // bypass costs at most one extra parse per intervening direct push.
    m_userTexturesDirectlyOverridden = true;
    // Symmetric reset of the companion path-only state: clearing the path
    // without resetting svgSize/wrap would leave stale per-slot settings
    // that silently re-apply if a later setShaderParams reattaches the
    // same path (path-change branch sees an empty cached path and triggers
    // the load, but the prior svgSize/wrap would persist). Reset both to
    // their library defaults so each setUserTexture pushes a fully-fresh
    // slot.
    // The same constant the constructor fills the array with, so a reset and a
    // fresh item agree by construction.
    m_userTextureSvgSizes[slot] = kDefaultUserTextureSvgSize;
    m_userTextureWraps[slot] = QStringLiteral("clamp");
    update();
}

void ShaderEffect::setUserTextureWrap(int slot, const QString& wrap)
{
    if (slot < 0 || slot >= kMaxUserTextures) {
        qCWarning(lcShaderNode) << "setUserTextureWrap: slot" << slot << "out of range [0," << (kMaxUserTextures - 1)
                                << "]";
        return;
    }
    // Mirror ShaderNodeRhi::setUserTextureWrap's normalize-then-guard order so
    // a capitalised "Repeat" stored here matches the lower-cased "repeat" the
    // node holds; otherwise syncBasePropertiesToNode re-pushes every paint and
    // ShaderNodeRhi's value-changed guard fails on the lexical mismatch.
    const QString normalized = ShaderNodeRhi::normalizeWrapMode(wrap);
    if (m_userTextureWraps[slot] == normalized) {
        return;
    }
    m_userTextureWraps[slot] = normalized;
    update();
}

// ============================================================================
// Wallpaper Texture
// ============================================================================

QImage ShaderEffect::wallpaperTexture() const
{
    QMutexLocker lock(&m_wallpaperTextureMutex);
    return m_wallpaperTexture;
}

void ShaderEffect::setWallpaperTexture(const QImage& image)
{
    {
        QMutexLocker lock(&m_wallpaperTextureMutex);
        if (m_wallpaperTexture.cacheKey() == image.cacheKey()) {
            return;
        }
        m_wallpaperTexture = image;
    }
    Q_EMIT wallpaperTextureChanged();
    update();
}

QVariant ShaderEffect::wallpaperTextureVariant() const
{
    return QVariant::fromValue(wallpaperTexture());
}

void ShaderEffect::setWallpaperTextureVariant(const QVariant& texture)
{
    // Unwrapped, not just converted. Measured delivery shapes (Qt 6.11.2):
    //
    //   - a direct assignment, a `createObject` initial-property map, and a
    //     C++ `setProperty` all deliver a plain QVariant(QImage);
    //   - QML `null` arrives as a VALID std::nullptr_t;
    //   - QML `undefined`, an ABSENT key on a JS object, an undefined
    //     `property var`, and BOTH `Binding` shapes (the
    //     `Binding { target: …; property: … }` element and
    //     `Binding on wallpaperTexture`) ALL arrive as an INVALID QVariant
    //     carrying no payload. Retyping the property to QVariant did not
    //     rescue the Binding forms; only a direct write delivers the image.
    //
    // Those last two groups are INDISTINGUISHABLE here: a host clearing its
    // backdrop by letting a key go absent looks exactly like a Binding firing.
    // Invalid therefore clears, which is right for the clear and cannot be
    // defended against for the Binding. That is why the QML-source sweep test
    // is the guard against a host driving one property from both a direct
    // assignment and a Binding — this setter structurally cannot catch it.
    //
    // A JS-side expression can still hand over a QJSValue, and a QVariant
    // nested in a QVariant is cheap to survive, so the peel below stays as
    // belt-and-braces defensive code rather than a handler for a shape we have
    // measured. value<QImage>() answers null for either wrapper, which is how
    // a real backdrop would turn into the pack's no-backdrop fallback with
    // nothing logged anywhere.
    const QVariant unwrapped = peelQmlVariant(texture);

    // Invalid, `null`, `undefined`, or an absent key: a host with no backdrop.
    // That is the ordinary "nothing behind this surface" state, not an error,
    // and resolves to a null image without warning.
    const int typeId = unwrapped.metaType().id();
    if (!unwrapped.isValid() || typeId == QMetaType::Nullptr || typeId == QMetaType::Void) {
        setWallpaperTexture(QImage());
        return;
    }
    if (!unwrapped.canConvert<QImage>()) {
        // Valid but not an image (a QString, a QUrl, a number). Clearing is
        // still the honest answer — there is no image here — but it must not
        // happen silently, which is the whole failure class this path exists
        // to close.
        qCWarning(lcShaderNode) << "ShaderEffect: wallpaperTexture written with an unconvertible value of type"
                                << unwrapped.metaType().name() << "— clearing the texture";
        setWallpaperTexture(QImage());
        return;
    }

    setWallpaperTexture(unwrapped.value<QImage>());
}

void ShaderEffect::setUseWallpaper(bool use)
{
    if (m_useWallpaper == use) {
        return;
    }
    m_useWallpaper = use;
    Q_EMIT useWallpaperChanged();
    update();
}

void ShaderEffect::setUseDepthBuffer(bool use)
{
    if (m_useDepthBuffer == use) {
        return;
    }
    m_useDepthBuffer = use;
    Q_EMIT useDepthBufferChanged();
    update();
}

void ShaderEffect::setGridSubdivisions(int subdivisions)
{
    const int clamped = qBound(0, subdivisions, ShaderNodeRhi::kMaxGridSubdivisions);
    if (m_gridSubdivisions == clamped) {
        return;
    }
    m_gridSubdivisions = clamped;
    Q_EMIT gridSubdivisionsChanged();
    update();
}

} // namespace PhosphorRendering
