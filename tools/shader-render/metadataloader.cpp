// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "metadataloader.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

namespace PlasmaZones::ShaderRender {
namespace {

QColor parseHexColor(const QString& hex)
{
    QColor c(hex);
    if (!c.isValid())
        c = Qt::black;
    return c;
}

// Resolve a relative path against the metadata directory.  Empty or
// already-absolute paths come back unchanged.
QString resolveRelative(const QString& metadataDir, const QString& maybeRelative)
{
    if (maybeRelative.isEmpty())
        return QString();
    if (QFileInfo(maybeRelative).isAbsolute())
        return maybeRelative;
    return QDir(metadataDir).filePath(maybeRelative);
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
    if (slot < 0 || slot >= 32)
        return;
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
    if (slot < 0 || slot >= 16)
        return;
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
        metadataDir, obj.value(QLatin1String("fragmentShader")).toString(QStringLiteral("effect.frag")));
    out.vertexShader =
        resolveRelative(metadataDir, obj.value(QLatin1String("vertexShader")).toString(QStringLiteral("zone.vert")));

    if (out.fragmentShader.isEmpty())
        return false;

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
    const QJsonArray params = obj.value(QLatin1String("parameters")).toArray();
    for (const auto& v : params) {
        if (!v.isObject())
            continue;
        const QJsonObject p = v.toObject();
        const QString type = p.value(QLatin1String("type")).toString(QStringLiteral("float"));
        const int slot = p.value(QLatin1String("slot")).toInt(-1);
        if (slot < 0)
            continue;

        if (type == QLatin1String("float") || type == QLatin1String("int")) {
            seedParam(out.customParams, slot, p.value(QLatin1String("default")).toDouble(0.0));
        } else if (type == QLatin1String("bool")) {
            seedParam(out.customParams, slot, p.value(QLatin1String("default")).toBool(false) ? 1.0 : 0.0);
        } else if (type == QLatin1String("color")) {
            seedColor(out.customColors, slot,
                      parseHexColor(p.value(QLatin1String("default")).toString(QStringLiteral("#000000"))));
        } else if (type == QLatin1String("image") && slot < 4) {
            out.userTextures[slot] = resolveRelative(metadataDir, p.value(QLatin1String("default")).toString());
            out.userTextureWraps[slot] = p.value(QLatin1String("wrap")).toString(QStringLiteral("clamp"));
        }
    }

    return true;
}

} // namespace PlasmaZones::ShaderRender
