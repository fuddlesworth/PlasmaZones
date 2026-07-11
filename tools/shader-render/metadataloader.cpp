// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "metadataloader.h"

#include "colorutil.h"

#include <PhosphorShaders/ShaderParamPreamble.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLoggingCategory>
#include <QSet>

namespace PlasmaZones::ShaderRender {

Q_LOGGING_CATEGORY(lcMetadataLoader, "plasmazones.shader-render.metadata")

namespace {

// Default shader filenames the daemon also assumes when metadata.json omits
// the explicit field. Kept here as named constants so a runtime convention
// change has one place to update on the tool side.
constexpr QLatin1String kDefaultFragmentShaderFilename{"effect.frag"};
constexpr QLatin1String kDefaultVertexShaderFilename{"zone.vert"};

// Resolve a relative path against the metadata directory. Empty paths come
// back unchanged; everything else gets normalised through QDir::cleanPath so
// `../foo.frag`-style entries don't show up as `…/dir/../foo.frag` in logs and
// duplicate-detection.
//
// Boundary check: relative paths that resolve outside @p metadataDir (via `..`
// segments or symlink-style absolute prefixes) are rejected with a warning and
// an empty string so the loader fails closed rather than loading arbitrary
// files. Absolute paths in the JSON are still allowed — the shader catalog
// installer occasionally points at /usr/share/plasmazones/shaders/common — but
// relative paths cannot escape their declared root.
QString resolveRelative(const QString& metadataDir, const QString& maybeRelative)
{
    if (maybeRelative.isEmpty())
        return QString();
    if (QFileInfo(maybeRelative).isAbsolute())
        return QDir::cleanPath(maybeRelative);
    const QString cleanedRoot = QDir::cleanPath(QDir(metadataDir).absolutePath());
    const QString resolved = QDir::cleanPath(QDir(metadataDir).absoluteFilePath(maybeRelative));
    // QDir::cleanPath collapses `..` segments, so an escape attempt is detected
    // by a prefix mismatch on the cleaned-and-absolute paths. The trailing-`/`
    // guard prevents `/foo` matching `/foobar` when metadataDir is `/foo`.
    const QString rootWithSep = cleanedRoot.endsWith(QLatin1Char('/')) ? cleanedRoot : cleanedRoot + QLatin1Char('/');
    if (resolved != cleanedRoot && !resolved.startsWith(rootWithSep)) {
        qCWarning(lcMetadataLoader) << "rejecting path traversal: relative" << maybeRelative << "would resolve to"
                                    << resolved << "outside metadata dir" << cleanedRoot;
        return QString();
    }
    return resolved;
}

// PlasmaZones metadata uses a *flat* parameter slot index across all
// 32 components (8 vec4 slots × 4 channels).  Map slot S to
// customParams[S / 4].<x|y|z|w>:
//   slot 0  → customParams[0].x   slot 1  → customParams[0].y
//   slot 2  → customParams[0].z   slot 3  → customParams[0].w
//   slot 4  → customParams[1].x   …       (etc up to slot 31)
// Indexing the array directly with `slot` writes the wrong vec4 and
// leaves the channel the shader actually reads at zero — that breaks
// every multi-component shader (ring counts, rotation speeds, idle
// animation, etc.) so they render in their dormant/empty state.
void seedParam(std::array<QVector4D, 8>& slots, int slot, double x)
{
    if (slot < 0 || slot >= 32) {
        qCWarning(lcMetadataLoader) << "param slot" << slot << "out of range [0, 32) — dropping";
        return;
    }
    const auto value = static_cast<float>(x);
    auto& v = slots[slot / 4];
    switch (slot % 4) {
    case 0:
        v.setX(value);
        break;
    case 1:
        v.setY(value);
        break;
    case 2:
        v.setZ(value);
        break;
    case 3:
        v.setW(value);
        break;
    }
}

void seedColor(std::array<QColor, 16>& slots, int slot, const QColor& c)
{
    if (slot < 0 || slot >= 16) {
        qCWarning(lcMetadataLoader) << "color slot" << slot << "out of range [0, 16) — dropping";
        return;
    }
    slots[slot] = c;
}

} // namespace

bool loadShaderMetadata(const QString& metadataPath, ShaderMetadata& out)
{
    QFile f(metadataPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    f.close();
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return false;

    const QJsonObject obj = doc.object();
    const QString metadataDir = QFileInfo(metadataPath).absolutePath();

    out.id = obj.value(QLatin1String("id")).toString();
    out.name = obj.value(QLatin1String("name")).toString();
    out.category = obj.value(QLatin1String("category")).toString();
    out.description = obj.value(QLatin1String("description")).toString();
    out.author = obj.value(QLatin1String("author")).toString();
    out.version = obj.value(QLatin1String("version")).toString(QStringLiteral("1.0"));

    // ── Shader source files (resolve absolute) ──────────────────
    out.fragmentShader = resolveRelative(
        metadataDir, obj.value(QLatin1String("fragmentShader")).toString(kDefaultFragmentShaderFilename));
    const QString vertName = obj.value(QLatin1String("vertexShader")).toString();
    if (!vertName.isEmpty()) {
        const QString resolved = resolveRelative(metadataDir, vertName);
        if (!resolved.isEmpty() && QFile::exists(resolved)) {
            out.vertexShader = resolved;
        } else {
            qCWarning(lcMetadataLoader) << "Declared vertexShader" << vertName << "not found in" << metadataDir;
        }
    } else {
        const QString localVert = resolveRelative(metadataDir, kDefaultVertexShaderFilename);
        if (!localVert.isEmpty() && QFile::exists(localVert)) {
            out.vertexShader = localVert;
        }
    }

    // Validate at the boundary: a metadata.json that points at a missing
    // fragment shader fails the loader rather than silently propagating a
    // bad path that would surface later as an opaque shader-compile error.
    // (No separate isEmpty branch: resolveRelative returns either the
    // metadata-dir-prefixed path — the default filename when the JSON omits
    // the field — or an empty string on a rejected path traversal, and
    // QFileInfo::exists("") is false, so this one check rejects both cases.)
    if (!QFileInfo::exists(out.fragmentShader)) {
        qCWarning(lcMetadataLoader) << "fragment shader not found:" << out.fragmentShader;
        return false;
    }

    out.multipass = obj.value(QLatin1String("multipass")).toBool(false);
    out.bufferFeedback = obj.value(QLatin1String("bufferFeedback")).toBool(false);
    out.depthBuffer = obj.value(QLatin1String("depthBuffer")).toBool(false);
    out.wallpaper = obj.value(QLatin1String("wallpaper")).toBool(false);
    out.bufferScale = obj.value(QLatin1String("bufferScale")).toDouble(1.0);
    out.bufferWrap = obj.value(QLatin1String("bufferWrap")).toString(QStringLiteral("clamp"));
    out.bufferFilter = obj.value(QLatin1String("bufferFilter")).toString(QStringLiteral("linear"));

    if (obj.contains(QLatin1String("bufferShader"))) {
        out.bufferShader = resolveRelative(metadataDir, obj.value(QLatin1String("bufferShader")).toString());
    }
    if (obj.contains(QLatin1String("bufferShaders"))) {
        const QJsonArray arr = obj.value(QLatin1String("bufferShaders")).toArray();
        for (const auto& v : arr) {
            out.bufferShaders.append(resolveRelative(metadataDir, v.toString()));
        }
    }
    if (obj.contains(QLatin1String("bufferWraps"))) {
        const QJsonArray arr = obj.value(QLatin1String("bufferWraps")).toArray();
        for (const auto& v : arr)
            out.bufferWraps.append(v.toString());
    }
    if (obj.contains(QLatin1String("bufferFilters"))) {
        const QJsonArray arr = obj.value(QLatin1String("bufferFilters")).toArray();
        for (const auto& v : arr)
            out.bufferFilters.append(v.toString());
    }

    // ── Per-parameter defaults ──────────────────────────────────
    // Slot resolution mirrors the registry's T1.1 automatic assignment
    // (libs/phosphor-shaders/src/shaderregistry.cpp, parseShaderMetadata):
    // explicit `slot` fields are reserved first, then every parameter that
    // omits `slot` is packed into the next free lane of its pool in
    // declaration order — float/int/bool → 0..31, color → 0..15, image →
    // 0..3. A param whose id is not a valid GLSL identifier body claims no
    // lane on either side (no p_<id> define, no upload), so it seeds nothing
    // here either. Keeping this numbering byte-identical to the registry's
    // matters: the generated p_<id> preamble the renderer installs (see
    // renderer.cpp) reads exactly the lane the default is seeded into.
    struct RawParam
    {
        QString id;
        QString type;
        QJsonValue def;
        QString wrap;
        int slot = -1;
    };
    QList<RawParam> raw;
    const QJsonArray params = obj.value(QLatin1String("parameters")).toArray();
    raw.reserve(params.size());
    for (const auto& v : params) {
        if (!v.isObject())
            continue;
        const QJsonObject p = v.toObject();
        RawParam r;
        r.id = p.value(QLatin1String("id")).toString();
        r.type = p.value(QLatin1String("type")).toString(QStringLiteral("float"));
        r.def = p.value(QLatin1String("default"));
        r.wrap = p.value(QLatin1String("wrap")).toString(QStringLiteral("clamp"));
        r.slot = p.value(QLatin1String("slot")).toInt(-1);
        if (!PhosphorShaders::isValidParamId(r.id)) {
            // No define, no lane — even with an explicit slot (registry parity).
            r.slot = -1;
        }
        raw.append(r);
    }

    const auto poolOf = [](const QString& type) -> int { // 0 = scalar, 1 = color, 2 = image
        if (type == QLatin1String("color"))
            return 1;
        if (type == QLatin1String("image"))
            return 2;
        return 0;
    };
    QSet<int> usedScalar, usedColor, usedImage;
    for (const RawParam& r : std::as_const(raw)) {
        if (r.slot < 0 || !PhosphorShaders::isValidParamId(r.id))
            continue;
        (poolOf(r.type) == 1 ? usedColor : poolOf(r.type) == 2 ? usedImage : usedScalar).insert(r.slot);
    }
    int nextScalar = 0, nextColor = 0, nextImage = 0;
    for (RawParam& r : raw) {
        if (r.slot >= 0 || !PhosphorShaders::isValidParamId(r.id))
            continue;
        const int pool = poolOf(r.type);
        QSet<int>& used = (pool == 1 ? usedColor : pool == 2 ? usedImage : usedScalar);
        int& next = (pool == 1 ? nextColor : pool == 2 ? nextImage : nextScalar);
        while (used.contains(next))
            ++next;
        r.slot = next;
        used.insert(next);
        ++next;
    }

    for (const RawParam& r : std::as_const(raw)) {
        if (r.slot < 0) {
            qCWarning(lcMetadataLoader) << "parameter" << r.id << "has no usable id — dropping";
            continue;
        }

        if (r.type == QLatin1String("float") || r.type == QLatin1String("int")) {
            seedParam(out.customParams, r.slot, r.def.toDouble(0.0));
        } else if (r.type == QLatin1String("bool")) {
            seedParam(out.customParams, r.slot, r.def.toBool(false) ? 1.0 : 0.0);
        } else if (r.type == QLatin1String("color")) {
            seedColor(out.customColors, r.slot, parseHexColor(r.def.toString(QStringLiteral("#000000"))));
        } else if (r.type == QLatin1String("image")) {
            if (r.slot >= 4) {
                qCWarning(lcMetadataLoader) << "image slot" << r.slot << "out of range [0, 4) — dropping" << r.id;
                continue;
            }
            out.userTextures[r.slot] = resolveRelative(metadataDir, r.def.toString());
            out.userTextureWraps[r.slot] = r.wrap;
        } else {
            // Deliberate: an unsupported type has already CLAIMED a scalar
            // lane above (the registry's poolOf treats unknown types as
            // scalar too), so the lane numbering of the following params
            // stays byte-identical to the p_<id> preamble — only the
            // seeding is skipped, leaving that lane at the -1 sentinel.
            qCWarning(lcMetadataLoader) << "parameter" << r.id << "has unsupported type" << r.type << "— dropping";
        }
    }

    return true;
}

} // namespace PlasmaZones::ShaderRender
