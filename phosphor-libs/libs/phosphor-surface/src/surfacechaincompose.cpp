// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PhosphorSurface/SurfaceChainCompose.h"

#include "PhosphorSurface/SurfaceShaderEffect.h"
#include "PhosphorSurface/SurfaceShaderRegistry.h"

#include <QLatin1String>
#include <QUrl>
#include <QVariant>

#include <algorithm>
#include <cmath>

namespace PhosphorSurfaceShaders {

/// A padding request is usable only if it converts to a number AND is finite.
///
/// Both inputs cross a trust boundary: the declared default comes from an
/// installed pack's metadata.json, and the override comes from a stored
/// per-surface profile. QVariant::toDouble() answers 0.0 for anything it
/// cannot convert, so testing convertibility separately is what keeps a
/// wrong-typed override from silently reading as "no padding requested" and
/// suppressing the pack's declared default. Rejecting non-finite values here
/// keeps NaN and infinity out of the callers' clamps, where the compositor's
/// narrowing to int would otherwise be undefined.
static bool usablePadding(const QVariant& value, double* out)
{
    bool ok = false;
    const double v = value.toDouble(&ok);
    if (!ok || !std::isfinite(v)) {
        return false;
    }
    *out = v;
    return true;
}

double paddingRequest(const SurfaceShaderEffect& effect, const QVariantMap& friendlyParams)
{
    if (effect.paddingParam.isEmpty()) {
        return 0.0;
    }
    // The pack must DECLARE the parameter first. This check used to run after
    // the override lookup, so a stored per-surface override keyed on a
    // paddingParam name the pack never declared was returned as padding. That
    // is reachable rather than theoretical: resolveParams copies deltas
    // verbatim and clampToBounds skips ids with no declared bound, so an
    // undeclared id survives the flatten and arrives here.
    const auto declared = std::find_if(effect.parameters.cbegin(), effect.parameters.cend(), [&](const auto& param) {
        return param.id == effect.paddingParam;
    });
    if (declared == effect.parameters.cend()) {
        // paddingParam names a parameter the pack does not declare: no room
        // asked for. Callers clamp anyway, so a bad name degrades to the
        // margin-less 1:1 geometry rather than to an unbounded canvas.
        return 0.0;
    }

    double value = 0.0;
    // Per-surface override wins over the declared default, but only when it is
    // actually a number: an unusable override falls through to the default
    // rather than collapsing the margin to zero.
    const auto override = friendlyParams.constFind(effect.paddingParam);
    if (override != friendlyParams.constEnd() && usablePadding(*override, &value)) {
        return value;
    }
    return usablePadding(declared->defaultValue, &value) ? value : 0.0;
}

QVariantMap composeStageMap(const SurfaceShaderEffect& effect, const QVariantMap& resolvedParams,
                            qreal blurScaleMultiplier)
{
    // The user's blur-quality tier, folded into every declared scale and bounded
    // into the allocator band. Mirrors PlasmaZonesEffect::clampedBufferScale, and
    // the multiplier itself is sanitised first because it arrives over D-Bus in the
    // compositor's case and off a store read here: a non-finite or non-positive
    // value would otherwise floor every pass at kMinBufferScale.
    const qreal multiplier =
        (blurScaleMultiplier > 0.0 && std::isfinite(blurScaleMultiplier)) ? blurScaleMultiplier : 1.0;
    const auto clampedScale = [multiplier](qreal declared) {
        return qBound(SurfaceShaderEffect::kMinBufferScale, declared * multiplier,
                      SurfaceShaderEffect::kMaxBufferScale);
    };
    QVariantMap stageMap;
    // An unusable pack composes to nothing rather than to a half-formed stage.
    // translateSurfaceParams already returns an empty map for one, so without
    // this the result carries a preamble and an `animated` flag alongside an
    // empty `source` url and no params — a shape a host would still add to its
    // chain. Callers treat an empty map as "skip this stage".
    if (!effect.isValid()) {
        return stageMap;
    }
    stageMap.insert(QLatin1String("source"), QUrl::fromLocalFile(effect.fragmentShaderPath));
    stageMap.insert(QLatin1String("vertexSource"),
                    effect.vertexShaderPath.isEmpty() ? QUrl() : QUrl::fromLocalFile(effect.vertexShaderPath));
    stageMap.insert(QLatin1String("preamble"), SurfaceShaderRegistry::paramPreamble(effect));
    stageMap.insert(QLatin1String("params"), SurfaceShaderRegistry::translateSurfaceParams(effect, resolvedParams));
    stageMap.insert(QLatin1String("animated"), effect.animated);

    // See the header: the emptiness half of this gate is what keeps a pack
    // whose builtin: buffer failed to resolve on the single-pass path.
    const bool stageMultipass = effect.isMultipass && !effect.bufferShaderPaths.isEmpty();
    stageMap.insert(QLatin1String("multipass"), stageMultipass);
    if (stageMultipass) {
        stageMap.insert(QLatin1String("bufferShaderPaths"), QVariant::fromValue(effect.bufferShaderPaths));
        stageMap.insert(QLatin1String("bufferFeedback"), effect.bufferFeedback);
        stageMap.insert(QLatin1String("bufferScale"), clampedScale(effect.bufferScale));
        QVariantList scales;
        scales.reserve(effect.bufferScales.size());
        for (qreal s : effect.bufferScales) {
            scales.append(clampedScale(s));
        }
        stageMap.insert(QLatin1String("bufferScales"), scales);
        stageMap.insert(QLatin1String("bufferWrap"), effect.bufferWrap);
        stageMap.insert(QLatin1String("bufferWraps"), QVariant::fromValue(effect.bufferWraps));
        stageMap.insert(QLatin1String("bufferFilter"), effect.bufferFilter);
        stageMap.insert(QLatin1String("bufferFilters"), QVariant::fromValue(effect.bufferFilters));
        stageMap.insert(QLatin1String("useDepthBuffer"), effect.useDepthBuffer);
        stageMap.insert(QLatin1String("halfFloatBuffers"), effect.halfFloatBuffers);
    }
    return stageMap;
}

QString roundBottomCornersParamId()
{
    return QStringLiteral("roundBottomCorners");
}

/// A silhouette answer is usable only if the variant actually holds one.
///
/// This mirrors usablePadding's SHAPE above but not its rationale, and the
/// difference matters. QVariant::toDouble() reports convertibility through an
/// `ok` out-parameter, so usablePadding can reject a wrong-TYPED override.
/// QVariant::toBool() has no such out-parameter and never reports failure, so
/// this rejects only an ABSENT (invalid) or explicitly-null value and converts
/// everything else — a stored string or container still gets an answer. That
/// residual is deliberate: gating the chain vote on a declared `"bool"` type was
/// considered and rejected, because the hosts inject into a pack without testing
/// its declared type, so refusing such a pack a VOTE while still writing the
/// chain's answer into it would be the asymmetry rather than the fix.
///
/// Both inputs cross the same trust boundary usablePadding describes: an
/// installed pack's metadata.json for the declared default, and a stored
/// per-surface profile for the override.
///
/// So a WRONG-TYPED stored value is explicitly not covered: QVariant::toBool() is
/// true for any string that is not empty, "0" or "false", so `"off"` reads as round.
/// Nothing in the tree can produce that today (the settings UI writes a bool and the
/// flatten copies it verbatim), and the alternative — gating on the declared type —
/// was rejected for the reason above. A hand-edited profile gets the value it asked
/// for rather than a refusal.
///
/// A STRING stored here squares the chain rather than abstaining, and there is no
/// empty-vs-null asymmetry to it. Qt 6 dropped the QString::isNull() special case from
/// QVariant::isNull(), so a default-constructed QString and a `""` both report isNull()
/// false, both pass this guard, and both convert to false.
///
/// In practice only an ABSENT key abstains. DecorationProfile::fromJson drops a JSON
/// `null` at both the pack and the parameter level before it can reach here, and says
/// why where it does it, so the null arm below covers a caller that builds the
/// QVariantMap in C++ instead. That is the std::nullptr_t shape the tests pin.
static bool usableBool(const QVariant& value, bool* out)
{
    if (!value.isValid() || value.isNull()) {
        return false;
    }
    *out = value.toBool();
    return true;
}

QVariant chainRoundBottomCorners(const SurfaceShaderRegistry& registry, const QStringList& chain,
                                 const QVariantMap& allPackParams)
{
    const QString key = roundBottomCornersParamId();
    // Step 2's answer, held back until the whole chain has been searched for a
    // step 1 answer. A stored value ANYWHERE in the chain outranks any declared
    // default, including one declared by an earlier pack.
    QVariant declaredFallback;
    for (const QString& packId : chain) {
        // No hasEffect() pre-check: effect() answers a default-constructed
        // SurfaceShaderEffect for an id the registry does not hold, and that has
        // an empty id so isValid() is already false for it. Both calls perform the
        // same factory lookup, so the probe was a second one for the same answer.
        // NOTE this reasoning is local to the resolver, and it does NOT condemn every
        // host probe. The daemon overlay host's and the shell's chainFor are both
        // load-bearing: each distinguishes "not installed" from "installed but broken"
        // for its own pair of diagnostics. A probe with no diagnostic on either arm is
        // the redundant shape.
        const SurfaceShaderEffect effect = registry.effect(packId);
        if (!effect.isValid()) {
            continue;
        }
        const auto declared =
            std::find_if(effect.parameters.cbegin(), effect.parameters.cend(), [&key](const auto& param) {
                return param.id == key;
            });
        if (declared == effect.parameters.cend()) {
            continue;
        }
        // Bound to a named local: constFind on the temporary QVariantMap that
        // value().toMap() returns would leave the iterator dangling at the end
        // of the full expression.
        const QVariantMap packParams = allPackParams.value(packId).toMap();
        bool value = false;
        // An unusable stored value FALLS THROUGH to this pack's declared default
        // rather than terminating the scan, the shape paddingRequest uses above.
        // Skipping the pack outright would be wrong: the scan would go on to a
        // later pack, and a null left in a hand-edited profile would silently
        // hand the chain to someone else.
        const auto stored = packParams.constFind(key);
        if (stored != packParams.constEnd() && usableBool(*stored, &value)) {
            return QVariant(value);
        }
        // A pack that declares the control WITHOUT a default has no opinion, so it
        // abstains and a later pack's stated default decides. Without the usability
        // test, defaultValue.toBool() on an absent default answered false, and
        // because QVariant(false) is itself valid it latched here and blocked every
        // later pack — silence outvoting a statement. The pair is the one
        // translateSurfaceParams already applies to this same field.
        if (!declaredFallback.isValid() && usableBool(declared->defaultValue, &value)) {
            declaredFallback = QVariant(value);
        }
    }
    return declaredFallback;
}

} // namespace PhosphorSurfaceShaders
