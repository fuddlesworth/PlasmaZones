// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/AnimationShaderEffect.h>

#include <PhosphorAnimation/AnimationShaderContract.h>
#include <PhosphorAnimation/ProfilePaths.h>

#include <QJsonArray>
#include <QJsonValue>
#include <QLoggingCategory>

namespace PhosphorAnimationShaders {

namespace {
Q_LOGGING_CATEGORY(lcAnimationShader, "phosphoranimationshaders.effect")

/// Parse a `fboExtent` string into the internal `FboExtentKind`.
/// Grammar:
///   * `"anchor"`     → Anchor (default; FBO == captured anchor)
///   * `"surface"`    → Surface (FBO == QQuickWindow contentItem)
///
/// Returns `true` on success and writes through `outExtent`; returns
/// `false` for an unknown / malformed string and emits a `qCWarning`
/// so a typo in metadata.json surfaces on the journal rather than
/// degrading silently. Caller keeps the struct's default value
/// (Anchor) on failure.
bool parseFboExtent(const QString& raw, AnimationShaderEffect::FboExtentKind& outExtent)
{
    const QString s = raw.trimmed();
    if (s.isEmpty()) {
        return false;
    }
    if (s.compare(QLatin1String("anchor"), Qt::CaseInsensitive) == 0) {
        outExtent = AnimationShaderEffect::FboExtentKind::Anchor;
        return true;
    }
    if (s.compare(QLatin1String("surface"), Qt::CaseInsensitive) == 0) {
        outExtent = AnimationShaderEffect::FboExtentKind::Surface;
        return true;
    }
    qCWarning(lcAnimationShader) << "AnimationShaderEffect::fromJson: unrecognised fboExtent" << raw
                                 << "Accepted forms are \"anchor\" and \"surface\". Falling back to defaults.";
    return false;
}

/// Emit the internal `FboExtentKind` as a `fboExtent` string. Inverse
/// of `parseFboExtent`. Empty result = "Anchor extent" (default,
/// omitted from JSON to keep authored metadata terse — same idiom as
/// the rest of `toJson`).
QString formatFboExtent(AnimationShaderEffect::FboExtentKind extent)
{
    if (extent == AnimationShaderEffect::FboExtentKind::Surface) {
        return QStringLiteral("surface");
    }
    return QString();
}
} // namespace

QJsonObject AnimationShaderEffect::toJson() const
{
    QJsonObject obj;
    obj.insert(QLatin1String("id"), id);
    if (!name.isEmpty())
        obj.insert(QLatin1String("name"), name);
    if (!description.isEmpty())
        obj.insert(QLatin1String("description"), description);
    if (!author.isEmpty())
        obj.insert(QLatin1String("author"), author);
    if (!version.isEmpty())
        obj.insert(QLatin1String("version"), version);
    if (!category.isEmpty())
        obj.insert(QLatin1String("category"), category);
    // Emit `appliesTo` only when the effect constrains itself — the empty
    // (universal) default stays omitted so the bulk of metadata.json files
    // are unchanged, same terse-by-default idiom as fboExtent/multipass.
    if (!appliesTo.isEmpty()) {
        QJsonArray arr;
        for (const auto& c : appliesTo)
            arr.append(c);
        obj.insert(QLatin1String("appliesTo"), arr);
    }
    if (!fragmentShaderPath.isEmpty())
        obj.insert(QLatin1String("fragmentShader"), fragmentShaderPath);
    if (!vertexShaderPath.isEmpty())
        obj.insert(QLatin1String("vertexShader"), vertexShaderPath);
    if (!previewPath.isEmpty())
        obj.insert(QLatin1String("preview"), previewPath);
    // `fboExtent` string: emit only when the value diverges from the
    // Anchor default (most shipping shaders); the empty-string return
    // from `formatFboExtent` signals that case so authored metadata.json
    // files stay terse.
    {
        const QString fboExtentStr = formatFboExtent(fboExtentKind);
        if (!fboExtentStr.isEmpty())
            obj.insert(QLatin1String("fboExtent"), fboExtentStr);
    }
    if (geometryGridSubdivisions > 0)
        obj.insert(QLatin1String("geometryGrid"), geometryGridSubdivisions);
    if (isMultipass)
        obj.insert(QLatin1String("multipass"), true);
    if (!bufferShaderPaths.isEmpty()) {
        QJsonArray arr;
        for (const auto& p : bufferShaderPaths)
            arr.append(p);
        obj.insert(QLatin1String("bufferShaders"), arr);
    }
    if (useWallpaper)
        obj.insert(QLatin1String("wallpaper"), true);
    if (bufferFeedback)
        obj.insert(QLatin1String("bufferFeedback"), true);
    // qFuzzyCompare-against-default idiom: emit `bufferScale` only when
    // it diverges from the 1.0 default. The `+ 1.0` shift is the
    // standard Qt workaround for `qFuzzyCompare`'s zero-input
    // pathology — `qFuzzyCompare(0.0, 0.0)` returns true but
    // `qFuzzyCompare(1e-30, 0.0)` returns false. Comparing
    // `bufferScale + 1.0` against `2.0` keeps both operands away from
    // zero so the relative-tolerance check works for `bufferScale`
    // values like 0.125 too.
    if (!qFuzzyCompare(bufferScale + 1.0, 2.0))
        obj.insert(QLatin1String("bufferScale"), bufferScale);
    if (!bufferWrap.isEmpty())
        obj.insert(QLatin1String("bufferWrap"), bufferWrap);
    if (!bufferWraps.isEmpty()) {
        QJsonArray arr;
        for (const auto& w : bufferWraps)
            arr.append(w);
        obj.insert(QLatin1String("bufferWraps"), arr);
    }
    if (!bufferFilter.isEmpty())
        obj.insert(QLatin1String("bufferFilter"), bufferFilter);
    if (!bufferFilters.isEmpty()) {
        QJsonArray arr;
        for (const auto& f : bufferFilters)
            arr.append(f);
        obj.insert(QLatin1String("bufferFilters"), arr);
    }
    if (useDepthBuffer)
        obj.insert(QLatin1String("depthBuffer"), true);

    if (!parameters.isEmpty()) {
        QJsonArray params;
        for (const auto& p : parameters) {
            QJsonObject pObj;
            pObj.insert(QLatin1String("id"), p.id);
            if (!p.name.isEmpty())
                pObj.insert(QLatin1String("name"), p.name);
            if (!p.type.isEmpty())
                pObj.insert(QLatin1String("type"), p.type);
            if (!p.description.isEmpty())
                pObj.insert(QLatin1String("description"), p.description);
            if (!p.group.isEmpty())
                pObj.insert(QLatin1String("group"), p.group);
            if (p.defaultValue.isValid())
                pObj.insert(QLatin1String("default"), QJsonValue::fromVariant(p.defaultValue));
            if (p.minValue.isValid())
                pObj.insert(QLatin1String("min"), QJsonValue::fromVariant(p.minValue));
            if (p.maxValue.isValid())
                pObj.insert(QLatin1String("max"), QJsonValue::fromVariant(p.maxValue));
            if (p.stepValue.isValid())
                pObj.insert(QLatin1String("step"), QJsonValue::fromVariant(p.stepValue));
            params.append(pObj);
        }
        obj.insert(QLatin1String("parameters"), params);
    }

    if (!textures.isEmpty()) {
        QJsonArray texArr;
        for (const auto& t : textures) {
            // Skip empty-path entries to preserve fromJson(toJson(x))
            // round-trip stability — fromJson drops them on read, so
            // emitting them on write would cause the round-trip to
            // shrink the list silently. An entry with empty path but
            // non-empty wrap is also dropped: the wrap is meaningless
            // without a sampler bound to it, and `parseEffect` (see
            // AnimationShaderRegistry::parseEffect) can produce
            // exactly this shape via its path-traversal guard, which
            // clears path while leaving wrap intact (defence in
            // depth). Letting it round-trip would silently smuggle a
            // dead wrap value through future scans.
            if (t.path.isEmpty())
                continue;
            QJsonObject tObj;
            tObj.insert(QLatin1String("path"), t.path);
            if (!t.wrap.isEmpty())
                tObj.insert(QLatin1String("wrap"), t.wrap);
            texArr.append(tObj);
        }
        if (!texArr.isEmpty())
            obj.insert(QLatin1String("textures"), texArr);
    }

    return obj;
}

AnimationShaderEffect AnimationShaderEffect::fromJson(const QJsonObject& obj)
{
    AnimationShaderEffect e;
    e.id = obj.value(QLatin1String("id")).toString();
    e.name = obj.value(QLatin1String("name")).toString();
    e.description = obj.value(QLatin1String("description")).toString();
    e.author = obj.value(QLatin1String("author")).toString();
    e.version = obj.value(QLatin1String("version")).toString();
    e.category = obj.value(QLatin1String("category")).toString();
    // `appliesTo` (array of event-class tokens). Only the documented
    // vocabulary — "geometry" / "appearance" / "desktop" — is accepted; an
    // unknown token is a typo or a foreign import and is dropped with a warning
    // so it neither restricts the picker on a class that doesn't exist nor
    // round-trips the typo back to disk via toJson. An array that validates
    // down to empty is indistinguishable from "universal", which is the
    // correct fallback (the effect applies everywhere except the opt-in
    // desktop class — see shaderEffectAppliesToEventPath).
    {
        namespace PP = PhosphorAnimation::ProfilePaths;
        const QJsonArray appliesArr = obj.value(QLatin1String("appliesTo")).toArray();
        for (const QJsonValue& v : appliesArr) {
            const QString token = v.toString().trimmed();
            if (token == PP::EventClassGeometry || token == PP::EventClassAppearance
                || token == PP::EventClassDesktop) {
                if (!e.appliesTo.contains(token))
                    e.appliesTo.append(token);
            } else if (!token.isEmpty()) {
                qCWarning(lcAnimationShader)
                    << "AnimationShaderEffect::fromJson: unknown appliesTo token" << token << "for effect" << e.id
                    << "— accepted values are \"geometry\", \"appearance\" and \"desktop\"; dropping.";
            }
        }
    }
    e.fragmentShaderPath = obj.value(QLatin1String("fragmentShader")).toString();
    e.vertexShaderPath = obj.value(QLatin1String("vertexShader")).toString();
    e.previewPath = obj.value(QLatin1String("preview")).toString();
    e.isMultipass = obj.value(QLatin1String("multipass")).toBool(false);
    const QJsonArray bufArr = obj.value(QLatin1String("bufferShaders")).toArray();
    for (const QJsonValue& v : bufArr) {
        const QString name = v.toString();
        if (!name.isEmpty())
            e.bufferShaderPaths.append(name);
    }
    e.useWallpaper = obj.value(QLatin1String("wallpaper")).toBool(false);
    e.bufferFeedback = obj.value(QLatin1String("bufferFeedback")).toBool(false);
    e.bufferScale = qBound(kMinBufferScale, obj.value(QLatin1String("bufferScale")).toDouble(1.0), kMaxBufferScale);
    e.bufferWrap = obj.value(QLatin1String("bufferWrap")).toString();
    const QJsonArray wrapsArr = obj.value(QLatin1String("bufferWraps")).toArray();
    for (const QJsonValue& v : wrapsArr) {
        const QString w = v.toString();
        if (!w.isEmpty())
            e.bufferWraps.append(w);
    }
    e.bufferFilter = obj.value(QLatin1String("bufferFilter")).toString();
    const QJsonArray filtersArr = obj.value(QLatin1String("bufferFilters")).toArray();
    for (const QJsonValue& v : filtersArr) {
        const QString f = v.toString();
        if (!f.isEmpty())
            e.bufferFilters.append(f);
    }
    e.useDepthBuffer = obj.value(QLatin1String("depthBuffer")).toBool(false);

    // `fboExtent` (string). Accepted forms:
    //   "anchor"        Anchor extent — FBO == captured anchor (default)
    //   "surface"       Surface extent — FBO fills QQuickWindow content
    //                     root (= the wl_surface scene root on daemon)
    // Missing field falls through to the struct's default (Anchor).
    // A recognised-but-malformed value emits a journal warning and
    // also falls through to the default — typos surface to the
    // operator instead of being silent.
    const QString fboExtentRaw = obj.value(QLatin1String("fboExtent")).toString();
    if (!fboExtentRaw.isEmpty()) {
        parseFboExtent(fboExtentRaw, e.fboExtentKind);
    }

    // `geometryGrid` (int): per-axis quad subdivisions for vertex-stage
    // geometry deformation. Negative values are clamped to 0 (no grid);
    // a missing field falls through to the struct default (0).
    e.geometryGridSubdivisions = qMax(0, obj.value(QLatin1String("geometryGrid")).toInt());

    const QJsonArray params = obj.value(QLatin1String("parameters")).toArray();
    e.parameters.reserve(params.size());
    for (const QJsonValue& v : params) {
        const QJsonObject pObj = v.toObject();
        ParameterInfo p;
        p.id = pObj.value(QLatin1String("id")).toString();
        p.name = pObj.value(QLatin1String("name")).toString();
        p.type = pObj.value(QLatin1String("type")).toString();
        p.description = pObj.value(QLatin1String("description")).toString();
        p.group = pObj.value(QLatin1String("group")).toString();
        if (pObj.contains(QLatin1String("default")))
            p.defaultValue = pObj.value(QLatin1String("default")).toVariant();
        if (pObj.contains(QLatin1String("min")))
            p.minValue = pObj.value(QLatin1String("min")).toVariant();
        if (pObj.contains(QLatin1String("max")))
            p.maxValue = pObj.value(QLatin1String("max")).toVariant();
        if (pObj.contains(QLatin1String("step")))
            p.stepValue = pObj.value(QLatin1String("step")).toVariant();
        e.parameters.append(std::move(p));
    }

    // Cap the texture list at the contract budget. Surplus entries are
    // silently dropped — the canonical UBO only declares iChannel1..3
    // and exposing more would require both runtimes to grow more
    // sampler bindings. A future contract bump (kMaxUserTextureSlots > 3)
    // would loosen this cap automatically.
    const QJsonArray texArr = obj.value(QLatin1String("textures")).toArray();
    e.textures.reserve(qMin<qsizetype>(texArr.size(), AnimationShaderContract::kMaxUserTextureSlots));
    qsizetype slotIndex = 0;
    int droppedEmpty = 0;
    for (const QJsonValue& v : texArr) {
        if (e.textures.size() >= AnimationShaderContract::kMaxUserTextureSlots)
            break;
        const QJsonObject tObj = v.toObject();
        TextureSlot t;
        t.path = tObj.value(QLatin1String("path")).toString();
        t.wrap = tObj.value(QLatin1String("wrap")).toString();
        // Validate wrap against the documented vocabulary. An empty
        // string is allowed and means "use the runtime default"
        // (clamp on both runtimes). Any other value is a typo or a
        // deprecated/foreign vocabulary import — log a warning and
        // reset to empty so the runtime applies its default rather
        // than carrying a string the runtime will silently coerce
        // to clamp anyway. Keeping unknown values in the in-memory
        // struct would also round-trip them back through toJson,
        // re-persisting the typo to disk on the next save.
        if (!t.wrap.isEmpty() && t.wrap != QLatin1String("clamp") && t.wrap != QLatin1String("repeat")
            && t.wrap != QLatin1String("mirror")) {
            qCWarning(lcAnimationShader) << "AnimationShaderEffect::fromJson: unknown wrap value" << t.wrap
                                         << "for slot" << slotIndex << ", reset to runtime default";
            t.wrap.clear();
        }
        // Drop entries with no path — they would map to a sampler with
        // nothing bound. The runtimes would fall back to transparent
        // black, but persisting the empty slot in JSON is just noise.
        // The visible warning here matters: TextureSlot has no explicit
        // slot-index field; an empty entry preceding a populated one
        // SHIFTS the populated entry's runtime slot. e.g. authoring
        // [{path:""}, {path:"foo.png"}, {path:"bar.png"}] yields
        // textures bound at iChannel1+iChannel2 instead of iChannel2+
        // iChannel3 as the metadata reads. Loud so authors notice the
        // implicit re-mapping.
        if (t.path.isEmpty()) {
            ++droppedEmpty;
        } else {
            if (droppedEmpty > 0) {
                qCWarning(lcAnimationShader)
                    << "AnimationShaderEffect::fromJson: textures[" << slotIndex << "] populated after" << droppedEmpty
                    << "empty entries; runtime slot will be shifted by that count "
                       "(empty entries are dropped, not preserved as gaps).";
            }
            e.textures.append(std::move(t));
        }
        ++slotIndex;
    }

    return e;
}

bool AnimationShaderEffect::operator==(const AnimationShaderEffect& other) const
{
    // Equality is "is this the same effect from the same on-disk
    // origin?", not "do these two structs serialise identically."
    // `sourceDir` and `isUserEffect` are stamped by the registry loader
    // (`AnimationShaderRegistry::parseEffect`) at scan time and are not
    // round-tripped through `toJson`. A struct freshly built from
    // `fromJson(toJson(x))` therefore has empty `sourceDir` and
    // `isUserEffect == false`, so `x == fromJson(toJson(x))` is FALSE
    // whenever `x` came from the registry — that's the contract, not a
    // bug. Tests that need round-trip equality should compare against a
    // copy that's had `sourceDir` / `isUserEffect` cleared.
    if (id != other.id || name != other.name || description != other.description)
        return false;
    if (author != other.author || version != other.version || category != other.category)
        return false;
    if (appliesTo != other.appliesTo)
        return false;
    if (fragmentShaderPath != other.fragmentShaderPath || vertexShaderPath != other.vertexShaderPath)
        return false;
    if (sourceDir != other.sourceDir || isUserEffect != other.isUserEffect)
        return false;
    if (previewPath != other.previewPath)
        return false;
    if (fboExtentKind != other.fboExtentKind)
        return false;
    if (geometryGridSubdivisions != other.geometryGridSubdivisions)
        return false;
    if (isMultipass != other.isMultipass || useWallpaper != other.useWallpaper || bufferFeedback != other.bufferFeedback
        || useDepthBuffer != other.useDepthBuffer)
        return false;
    if (!qFuzzyCompare(bufferScale + 1.0, other.bufferScale + 1.0))
        return false;
    if (bufferShaderPaths != other.bufferShaderPaths || bufferWrap != other.bufferWrap
        || bufferWraps != other.bufferWraps || bufferFilter != other.bufferFilter
        || bufferFilters != other.bufferFilters)
        return false;
    if (parameters.size() != other.parameters.size())
        return false;
    for (int i = 0; i < parameters.size(); ++i) {
        const auto& a = parameters[i];
        const auto& b = other.parameters[i];
        if (a.id != b.id || a.name != b.name || a.type != b.type)
            return false;
        if (a.description != b.description || a.group != b.group)
            return false;
        if (a.defaultValue != b.defaultValue || a.minValue != b.minValue || a.maxValue != b.maxValue
            || a.stepValue != b.stepValue)
            return false;
    }
    if (textures != other.textures)
        return false;
    return true;
}

bool shaderEffectAppliesToEventPath(const AnimationShaderEffect& effect, const QString& path)
{
    namespace PP = PhosphorAnimation::ProfilePaths;
    const QString cls = PP::eventClassForPath(path);
    // The desktop class is a SEPARATE two-texture (from/to) contract, so it is
    // opt-in rather than universal-permissive: only an effect that explicitly
    // lists `desktop` in appliesTo runs on a desktop path. A universal
    // single-surface effect (empty appliesTo) must NOT bleed onto desktop
    // paths, where its lone surface sampler would be unbound; conversely a
    // desktop effect declaring appliesTo:["desktop"] is dimmed on window/OSD
    // paths (their class isn't `desktop`) by the concrete-mismatch check below.
    if (cls == PP::EventClassDesktop)
        return effect.appliesTo.contains(cls);
    // Universal effect (no declared constraint) runs on every single-surface path.
    if (effect.appliesTo.isEmpty())
        return true;
    // Only report false on a PROVABLE mismatch: the path resolves to a
    // concrete class AND the effect doesn't list it. An ambiguous row
    // (mixed ancestor / non-window path → empty class) is left compatible
    // so the picker never dims an effect on a row it can't classify — EXCEPT a
    // desktop-declaring effect. Its two-texture (from/to) contract must never be
    // offered on a non-desktop or ambiguous row, where its second sampler is
    // unbound. This is the inverse of the universal-excluded-from-desktop rule
    // above, keeping the desktop opt-in symmetric in both directions.
    if (cls.isEmpty())
        return !effect.appliesTo.contains(PP::EventClassDesktop);
    return effect.appliesTo.contains(cls);
}

} // namespace PhosphorAnimationShaders
