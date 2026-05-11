// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/AnimationShaderEffect.h>

#include <PhosphorAnimation/AnimationShaderContract.h>

#include <QJsonArray>
#include <QJsonValue>
#include <QLoggingCategory>

namespace PhosphorAnimationShaders {

namespace {
Q_LOGGING_CATEGORY(lcAnimationShader, "phosphoranimationshaders.effect")

/// Parse a `fboExtent` string into the internal `FboExtentKind` +
/// `fboExtentRing` representation. Grammar:
///   * `"anchor"`            → Anchor, pad = 0
///   * `"anchor+0.5"`        → Anchor, pad = 0.5 (fraction form)
///   * `"anchor+50%"`        → Anchor, pad = 0.5 (percent form, identical effect)
///   * `"surface"`           → Surface, pad = 0
/// Padding is clamped to `[0, kMaxFboExtentRing]` at the parse boundary
/// to mirror the legacy `fboExtentRing` read clamp.
///
/// Returns `true` on success and writes through `outExtent` / `outPad`;
/// returns `false` for an unknown / malformed string and emits a
/// `qCWarning` so a typo in metadata.json surfaces on the journal
/// rather than degrading silently. Caller keeps the struct's default
/// values (Anchor, pad=0) on failure.
bool parseFboExtent(const QString& raw, AnimationShaderEffect::FboExtentKind& outExtent, qreal& outPad)
{
    const QString s = raw.trimmed();
    if (s.isEmpty()) {
        return false;
    }
    if (s.compare(QLatin1String("anchor"), Qt::CaseInsensitive) == 0) {
        outExtent = AnimationShaderEffect::FboExtentKind::Anchor;
        outPad = 0.0;
        return true;
    }
    if (s.compare(QLatin1String("surface"), Qt::CaseInsensitive) == 0) {
        outExtent = AnimationShaderEffect::FboExtentKind::Surface;
        outPad = 0.0;
        return true;
    }
    // `anchor+N` / `anchor+N%` form. Split on '+'; left half must be
    // `anchor`, right half is a float (with optional trailing `%`).
    const int plusIdx = s.indexOf(QLatin1Char('+'));
    if (plusIdx > 0 && s.left(plusIdx).trimmed().compare(QLatin1String("anchor"), Qt::CaseInsensitive) == 0) {
        QString tail = s.mid(plusIdx + 1).trimmed();
        bool percent = false;
        if (tail.endsWith(QLatin1Char('%'))) {
            percent = true;
            tail.chop(1);
            tail = tail.trimmed();
        }
        bool ok = false;
        const double v = tail.toDouble(&ok);
        // QString::toDouble parses "nan" / "inf" / "+inf" / "-inf" as
        // floating-point literals (ok == true) but those values aren't
        // safe to flow downstream: qBound propagates NaN through
        // qMax/qMin rather than clamping it, and consumers that read
        // `effect.fboExtentRing` raw (e.g. osd.cpp's
        // `resolveOsdShaderPadding` feeding QML's `shaderBoundsPadding`)
        // would collapse window dimensions to NaN. Reject at the parse
        // boundary so bad metadata surfaces on the warning channel
        // rather than corrupting OSD geometry silently.
        if (ok && qIsFinite(v)) {
            const qreal pad = percent ? (v / 100.0) : v;
            // Reject negatives at the parse boundary instead of silently
            // clamping to 0. An authoring typo like "anchor+-0.5" would
            // otherwise masquerade as a valid `anchor` extent with no ring,
            // losing the operator's chance to spot the bad value in
            // metadata. The unrecognised-grammar fallback below emits the
            // warning and returns false; caller keeps the struct defaults.
            if (pad >= 0.0) {
                outExtent = AnimationShaderEffect::FboExtentKind::Anchor;
                outPad = qMin(pad, AnimationShaderEffect::kMaxFboExtentRing);
                return true;
            }
        }
    }
    qCWarning(lcAnimationShader)
        << "AnimationShaderEffect::fromJson: unrecognised fboExtent" << raw
        << "Accepted forms are \"anchor\", \"anchor+0.5\", \"anchor+50%\", \"surface\". Falling back to defaults.";
    return false;
}

/// Emit the internal `FboExtentKind` + `fboExtentRing` representation
/// as a `fboExtent` string. Inverse of `parseFboExtent`. Empty result
/// = "Anchor extent with zero padding" (omitted from JSON to keep
/// authored metadata terse, same idiom as the rest of `toJson`).
QString formatFboExtent(AnimationShaderEffect::FboExtentKind extent, qreal pad)
{
    if (extent == AnimationShaderEffect::FboExtentKind::Surface) {
        return QStringLiteral("surface");
    }
    // Anchor extent: omit when there's no ring (the common case in
    // 53 of 56 shipping shaders).
    const qreal clamped = qBound(qreal(0.0), pad, AnimationShaderEffect::kMaxFboExtentRing);
    if (clamped <= 0.0) {
        return QString();
    }
    // Pin `g` format to 17 significant digits so a programmatically
    // assigned ring (e.g. 1.0/3.0 in C++ code) survives toJson -> fromJson
    // round-trip. `arg(double)` defaults to 6 sig digits, which collapses
    // 0.333333... to "0.333333" and breaks strict-equality comparison on
    // the resulting AnimationShaderEffect. 17 digits is the IEEE-754
    // double-precision round-trip width.
    return QStringLiteral("anchor+%1").arg(clamped, 0, 'g', 17);
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
    if (!fragmentShaderPath.isEmpty())
        obj.insert(QLatin1String("fragmentShader"), fragmentShaderPath);
    if (!vertexShaderPath.isEmpty())
        obj.insert(QLatin1String("vertexShader"), vertexShaderPath);
    if (!previewPath.isEmpty())
        obj.insert(QLatin1String("preview"), previewPath);
    // Single `fboExtent` string field replaces the previous split
    // `fboExtentKind` + `fboExtentRing` pair. See `parseFboExtent` /
    // `formatFboExtent` for the grammar. Emit only when the combined
    // value diverges from the Anchor-no-pad default (53 of 56 shipping
    // shaders); the empty-string return from `formatFboExtent` signals
    // that case so authored metadata.json files stay terse.
    {
        const QString fboExtentStr = formatFboExtent(fboExtentKind, fboExtentRing);
        if (!fboExtentStr.isEmpty())
            obj.insert(QLatin1String("fboExtent"), fboExtentStr);
    }
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

    // `fboExtent` (string). Single grammar replaces the previous split
    // `fboExtentKind` + `fboExtentRing` pair. Accepted forms (see
    // `parseFboExtent` for full details):
    //   "anchor"        is Anchor extent, no padding (default)
    //   "anchor+0.5"    is Anchor extent with ring-padding fraction
    //   "anchor+50%"    is the percent form
    //   "surface"       fills the anchor's QQuickWindow content
    //                     root (= the wl_surface scene root on daemon)
    // Missing field falls through to the struct's defaults (Anchor,
    // pad=0); a recognised but malformed value (`parseFboExtent`
    // returns false) emits a journal warning and ALSO falls through to
    // the defaults. Same lenient pattern as the legacy split fields,
    // but typos now surface to the operator instead of being silent.
    // Per the project's no-legacy-shims rule, the prior `fboExtentRing`
    // / `fboExtentKind` JSON keys are NOT read here. Authored metadata
    // must use `fboExtent`.
    const QString fboExtentRaw = obj.value(QLatin1String("fboExtent")).toString();
    if (!fboExtentRaw.isEmpty()) {
        parseFboExtent(fboExtentRaw, e.fboExtentKind, e.fboExtentRing);
    }

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
    if (fragmentShaderPath != other.fragmentShaderPath || vertexShaderPath != other.vertexShaderPath)
        return false;
    if (sourceDir != other.sourceDir || isUserEffect != other.isUserEffect)
        return false;
    if (previewPath != other.previewPath)
        return false;
    if (fboExtentKind != other.fboExtentKind)
        return false;
    // `fboExtentRing` is semantically dead when kind == Surface (the FBO
    // already covers the entire surface, ring padding is ignored); the
    // docstring on the field declares this. Mirror that contract in
    // operator==: a programmatic Surface effect with ring != 0 would
    // otherwise compare unequal to its `fromJson(toJson(x))` round-trip
    // because formatFboExtent drops the ring for Surface and parseFboExtent
    // reads it back as 0.
    if (fboExtentKind == FboExtentKind::Anchor && !qFuzzyCompare(fboExtentRing + 1.0, other.fboExtentRing + 1.0))
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

} // namespace PhosphorAnimationShaders
