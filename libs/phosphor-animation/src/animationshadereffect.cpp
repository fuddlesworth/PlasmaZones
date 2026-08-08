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
/// Writes through `outExtent` on success and leaves it alone otherwise, so a
/// caller that passes a struct field already holding the default (Anchor)
/// needs no failure branch — which is why this reports nothing. An unknown or
/// malformed string emits a `qCWarning` here, so a typo in metadata.json
/// surfaces on the journal rather than degrading silently.
void parseFboExtent(const QString& raw, AnimationShaderEffect::FboExtentKind& outExtent)
{
    const QString s = raw.trimmed();
    if (s.isEmpty()) {
        return;
    }
    if (s.compare(QLatin1String("anchor"), Qt::CaseInsensitive) == 0) {
        outExtent = AnimationShaderEffect::FboExtentKind::Anchor;
        return;
    }
    if (s.compare(QLatin1String("surface"), Qt::CaseInsensitive) == 0) {
        outExtent = AnimationShaderEffect::FboExtentKind::Surface;
        return;
    }
    qCWarning(lcAnimationShader) << "AnimationShaderEffect::fromJson: unrecognised fboExtent" << raw
                                 << "Accepted forms are \"anchor\" and \"surface\". Falling back to defaults.";
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
    if (useAudio)
        obj.insert(QLatin1String("audio"), true);

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
    // vocabulary — "geometry" / "appearance" / "desktop" / "move" / "strip" —
    // is accepted; an unknown token is a typo or a foreign import and is
    // dropped with a warning so it neither restricts the picker on a class
    // that doesn't exist nor round-trips the typo back to disk via toJson. An
    // array that validates down to empty is indistinguishable from
    // "universal", which is the correct fallback (the effect applies
    // everywhere except the opt-in desktop, move and strip classes — see
    // shaderEffectAppliesToEventPath).
    {
        namespace PP = PhosphorAnimation::ProfilePaths;
        const QJsonValue appliesVal = obj.value(QLatin1String("appliesTo"));
        // Shape guard: a present-but-non-array appliesTo (a bare string is
        // the plausible author typo) would silently reduce to universal via
        // toArray(); an in-array unknown token warns below, so the shape
        // error deserves the same journal signal.
        if (!appliesVal.isUndefined() && !appliesVal.isArray()) {
            qCWarning(lcAnimationShader) << "AnimationShaderEffect::fromJson: appliesTo for effect" << e.id
                                         << "is not an array; ignoring it (pack treated as universal).";
        }
        // Validated against the exported vocabulary, not a hand-copied list:
        // a class token added to ProfilePaths becomes accepted here and named
        // in the diagnostic without touching this loop.
        const QStringList validTokens = PP::allEventClassTokens();
        const QJsonArray appliesArr = appliesVal.toArray();
        for (const QJsonValue& v : appliesArr) {
            const QString token = v.toString().trimmed();
            if (validTokens.contains(token)) {
                if (!e.appliesTo.contains(token))
                    e.appliesTo.append(token);
            } else if (!token.isEmpty()) {
                qCWarning(lcAnimationShader)
                    << "AnimationShaderEffect::fromJson: unknown appliesTo token" << token << "for effect" << e.id
                    << "— accepted values are" << qPrintable(validTokens.join(QLatin1String(", "))) << "; dropping.";
            }
        }
    }
    // Shared shape guard for the array-valued fields, modelled on the
    // appliesTo check above: a present-but-non-array value (a bare string is
    // the plausible author typo) silently reduces to empty via toArray(), and
    // the parameters/textures/bufferShaders fields deserve the same journal
    // signal appliesTo gets. Returns the array (empty on mismatch). appliesTo
    // keeps its own copy rather than calling this because its consequence is
    // different enough to say out loud: an ignored appliesTo does not leave
    // the field empty, it makes the pack UNIVERSAL.
    const auto arrayOrWarn = [&obj, &e](const char* key) -> QJsonArray {
        const QJsonValue v = obj.value(QLatin1String(key));
        if (!v.isUndefined() && !v.isArray()) {
            qCWarning(lcAnimationShader) << "AnimationShaderEffect::fromJson:" << key << "for effect" << e.id
                                         << "is not an array; ignoring it.";
        }
        return v.toArray();
    };
    e.fragmentShaderPath = obj.value(QLatin1String("fragmentShader")).toString();
    e.vertexShaderPath = obj.value(QLatin1String("vertexShader")).toString();
    e.previewPath = obj.value(QLatin1String("preview")).toString();
    e.isMultipass = obj.value(QLatin1String("multipass")).toBool(false);
    // EVERY entry is kept IN PLACE (no empty-string compaction): bufferWraps /
    // bufferFilters below are positionally aligned with this list, so
    // compacting an empty entry here would shift every later buffer's
    // override onto the wrong buffer — the exact misalignment the loops
    // below were rewritten to prevent. An empty entry fails the existence
    // check at scan time and fail-closes multipass coherently, like any
    // other missing buffer source. Capped at the contract budget with a
    // warning (mirroring the texture cap below): the runtime binds at most
    // kMaxBufferPasses passes and would silently drop the tail.
    const QJsonArray bufArr = arrayOrWarn("bufferShaders");
    for (const QJsonValue& v : bufArr) {
        if (e.bufferShaderPaths.size() >= AnimationShaderContract::kMaxBufferPasses) {
            qCWarning(lcAnimationShader).nospace()
                << "AnimationShaderEffect " << e.id << ": bufferShaders declares " << bufArr.size()
                << " passes; the contract budget is " << AnimationShaderContract::kMaxBufferPasses
                << " — surplus passes dropped";
            break;
        }
        e.bufferShaderPaths.append(v.toString());
    }
    e.useWallpaper = obj.value(QLatin1String("wallpaper")).toBool(false);
    e.bufferFeedback = obj.value(QLatin1String("bufferFeedback")).toBool(false);
    e.bufferScale = qBound(kMinBufferScale, obj.value(QLatin1String("bufferScale")).toDouble(1.0), kMaxBufferScale);
    // Buffer wrap / filter tokens are validated exactly like the texture `wrap`
    // below, and for the same reason: an unknown token is silently coerced by the
    // runtime, survives operator== and toJson, and is re-persisted to disk on the
    // next save. These four fields had NO validation at all, so centralising the
    // vocabulary for textures left the buffer side still accepting typos.
    const auto validatedWrap = [](QString wrap, const char* field) -> QString {
        if (!wrap.isEmpty() && !AnimationShaderContract::isValidWrapToken(wrap)) {
            qCWarning(lcAnimationShader) << "AnimationShaderEffect::fromJson: unknown" << field << "value" << wrap
                                         << ", reset to runtime default";
            wrap.clear();
        }
        return wrap;
    };
    const auto validatedFilter = [](QString filter, const char* field) -> QString {
        if (!filter.isEmpty() && !AnimationShaderContract::isValidFilterToken(filter)) {
            qCWarning(lcAnimationShader) << "AnimationShaderEffect::fromJson: unknown" << field << "value" << filter
                                         << ", reset to runtime default";
            filter.clear();
        }
        return filter;
    };
    e.bufferWrap = validatedWrap(obj.value(QLatin1String("bufferWrap")).toString(), "bufferWrap");
    // EVERY entry is kept in place, matching the surface twin. These lists are
    // positionally aligned with `bufferShaderPaths`, so dropping an entry — which
    // the old `if (!w.isEmpty())` shape did for any empty one — shifted every
    // later buffer's override onto the wrong buffer. An invalid token becomes
    // empty (that slot falls back to the default); an originally-empty entry is
    // the explicit "default for this slot" marker and toJson re-emits it, so
    // dropping one broke alignment on the very next load of a saved pack.
    const QJsonArray wrapsArr = arrayOrWarn("bufferWraps");
    for (const QJsonValue& v : wrapsArr) {
        e.bufferWraps.append(validatedWrap(v.toString(), "bufferWraps"));
    }
    e.bufferFilter = validatedFilter(obj.value(QLatin1String("bufferFilter")).toString(), "bufferFilter");
    const QJsonArray filtersArr = arrayOrWarn("bufferFilters");
    for (const QJsonValue& v : filtersArr) {
        e.bufferFilters.append(validatedFilter(v.toString(), "bufferFilters"));
    }
    // Positional alignment means entries past the (capped) path list can
    // never bind to a buffer; retaining them only bloats operator== / toJson
    // round-trips, so trim with a warning. Entries UP TO the path count are
    // kept even when empty (the "default for this slot" markers).
    const auto trimAligned = [&](QStringList& list, const char* field) {
        if (list.size() > e.bufferShaderPaths.size()) {
            qCWarning(lcAnimationShader).nospace()
                << "AnimationShaderEffect " << e.id << ": " << field << " declares " << list.size() << " entries for "
                << e.bufferShaderPaths.size() << " buffer passes — surplus entries dropped";
            list = list.mid(0, e.bufferShaderPaths.size());
        }
    };
    trimAligned(e.bufferWraps, "bufferWraps");
    trimAligned(e.bufferFilters, "bufferFilters");
    e.useDepthBuffer = obj.value(QLatin1String("depthBuffer")).toBool(false);
    e.useAudio = obj.value(QLatin1String("audio")).toBool(false);

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
    // a missing field falls through to the struct default (0). Bounded above
    // (the ONE pack scalar that lands on a per-frame compositor allocation
    // as n² — see kMaxGeometryGridSubdivisions) with a warning, mirroring
    // the texture-cap warning below.
    const int rawGrid = obj.value(QLatin1String("geometryGrid")).toInt();
    e.geometryGridSubdivisions = qBound(0, rawGrid, kMaxGeometryGridSubdivisions);
    if (rawGrid > kMaxGeometryGridSubdivisions) {
        qCWarning(lcAnimationShader).nospace()
            << "AnimationShaderEffect " << e.id << ": geometryGrid " << rawGrid << " exceeds the cap ("
            << kMaxGeometryGridSubdivisions << ") and was clamped — n*n quads are allocated per painted frame";
    }

    const QJsonArray params = arrayOrWarn("parameters");
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
    // silently dropped — the canonical UBO only declares uTexture1..3
    // and exposing more would require both runtimes to grow more
    // sampler bindings. A future contract bump (kMaxUserTextureSlots > 3)
    // would loosen this cap automatically.
    const QJsonArray texArr = arrayOrWarn("textures");
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
        if (!t.wrap.isEmpty() && !PhosphorAnimationShaders::AnimationShaderContract::isValidWrapToken(t.wrap)) {
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
        // textures bound at uTexture1+uTexture2 instead of uTexture2+
        // uTexture3 as the metadata reads. Loud so authors notice the
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
    // appliesTo compares as a SET, not a sequence. It is a set everywhere it
    // is consumed — every reader asks "does it contain X" — so two packs
    // declaring the same classes in different order are behaviourally
    // identical, and reporting them unequal made a metadata reorder look like
    // a content change to every equality-gated path (registry reload
    // diffing, the settings dirty check). fromJson already dedupes, so a
    // sorted copy is a faithful set comparison; both lists are at most the
    // five class tokens, so the sort is free.
    //
    // The one consumer that used to care about order was the shader
    // browser's type badge, which read appliesTo[0]; it now picks by catalog
    // order (ShaderBrowserPage._effectTypeKey), so nothing observes the
    // declaration order any more.
    {
        QStringList mine = appliesTo;
        QStringList theirs = other.appliesTo;
        mine.sort();
        theirs.sort();
        if (mine != theirs)
            return false;
    }
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
        || useDepthBuffer != other.useDepthBuffer || useAudio != other.useAudio)
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
    // The move class (the held interactive drag) is opt-in for the same
    // structural reason: a drag installs a held transition with no old→new
    // crossfade, so only a pack consuming the move-physics inputs — declared
    // via `appliesTo: ["move"]` — does anything there. A universal or
    // geometry pack on the move leaf would install a dead transition that
    // pins full-output repaints for the whole drag.
    if (cls == PP::EventClassMove)
        return effect.appliesTo.contains(cls);
    // The strip class (the scrolling strip's view leg) is opt-in for the same
    // structural reason as move: the view spring retargets continuously under
    // wheel scrolling, so there is no discrete from/to leg for a crossfade
    // pack to play — only a pack consuming the strip inputs (uStrip /
    // iStripMotion / iStripRect, declared via `appliesTo: ["strip"]`)
    // decorates the live per-output capture. A universal or single-surface
    // pack on the strip row would install a dead full-output pass for every
    // scroll.
    if (cls == PP::EventClassStrip)
        return effect.appliesTo.contains(cls);
    // Universal effect (no declared constraint) runs on every single-surface path.
    if (effect.appliesTo.isEmpty())
        return true;
    // Only report false on a PROVABLE mismatch: the path resolves to a
    // concrete class AND the effect doesn't list it. An ambiguous row
    // (mixed ancestor / non-window path → empty class) is left compatible
    // so the picker never dims an effect on a row it can't classify — EXCEPT
    // an effect that declares NEITHER geometry NOR appearance, which is to
    // say a pack that is exclusively one of the three opt-in classes
    // (desktop / move / strip).
    //
    // What an ambiguous row can actually feed is only the geometry and
    // appearance legs beneath it. The move leaf takes no inherited shader at
    // all (ShaderProfileTree::resolve leaf isolation), and while the desktop
    // and strip leaves DO inherit — so a screen-level-only pack assigned at
    // `global` would in fact be picked up and run — offering it on a row that
    // spans mostly single-surface events advertises a behaviour it does not
    // have there. So for those two this is picker POLICY (assign it on the
    // leaf that runs it) rather than a deadness proof, while for move-only it
    // is a genuine proof.
    //
    // The test is on the single-surface classes rather than on the opt-in
    // ones so a HYBRID keeps working: a pack declaring ["strip",
    // "appearance"] is live on every appearance leg under an ambiguous row,
    // and excluding it by its strip token would deny it the assignment its
    // appearance leg earns.
    if (cls.isEmpty()) {
        return effect.appliesTo.contains(PP::EventClassGeometry) || effect.appliesTo.contains(PP::EventClassAppearance);
    }
    return effect.appliesTo.contains(cls);
}

bool shaderEffectIsCompositorOnly(const AnimationShaderEffect& effect)
{
    namespace PP = PhosphorAnimation::ProfilePaths;
    // Universal packs (no declared constraint) run on every single-surface
    // path, daemon overlays included. A constrained pack reaches the daemon
    // only through the appearance class — desktop / geometry / move / strip
    // events exist solely inside the kwin-effect.
    return !effect.appliesTo.isEmpty() && !effect.appliesTo.contains(PP::EventClassAppearance);
}

} // namespace PhosphorAnimationShaders
