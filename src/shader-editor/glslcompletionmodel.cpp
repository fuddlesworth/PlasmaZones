// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "glslcompletionmodel.h"

#include <KTextEditor/Document>
#include <KTextEditor/View>

namespace PlasmaZones {

GlslCompletionModel::GlslCompletionModel(QObject* parent)
    : KTextEditor::CodeCompletionModel(parent)
{
    m_funcIcon = QIcon::fromTheme(QStringLiteral("code-function"));
    m_varIcon = QIcon::fromTheme(QStringLiteral("code-variable"));
    m_macroIcon = QIcon::fromTheme(QStringLiteral("code-typedef"));
    buildItems();
}

void GlslCompletionModel::buildItems()
{
    auto addFunc = [this](const QString& name, const QString& sig, const QString& ret) {
        m_allItems.append({name, sig, ret, static_cast<int>(GlobalScope | Function)});
    };
    auto addVar = [this](const QString& name, const QString& detail, const QString& type) {
        m_allItems.append({name, detail, type, static_cast<int>(GlobalScope | Variable)});
    };
    auto addConst = [this](const QString& name, const QString& detail) {
        m_allItems.append({name, detail, QStringLiteral("const"), static_cast<int>(GlobalScope | Variable)});
    };

    // ── UBO fields (ZoneUniforms, binding 0) ──
    addVar(QStringLiteral("qt_Matrix"), QStringLiteral("projection matrix"), QStringLiteral("mat4"));
    addVar(QStringLiteral("qt_Opacity"), QStringLiteral("global opacity"), QStringLiteral("float"));
    addVar(QStringLiteral("iTime"), QStringLiteral("seconds since start"), QStringLiteral("float"));
    addVar(QStringLiteral("iTimeDelta"), QStringLiteral("time since last frame"), QStringLiteral("float"));
    addVar(QStringLiteral("iFrame"), QStringLiteral("frame counter"), QStringLiteral("int"));
    addVar(QStringLiteral("iResolution"), QStringLiteral("viewport size in pixels"), QStringLiteral("vec2"));
    addVar(QStringLiteral("zoneCount"), QStringLiteral("number of active zones"), QStringLiteral("int"));
    addVar(QStringLiteral("highlightedCount"), QStringLiteral("number of highlighted zones"), QStringLiteral("int"));
    addVar(QStringLiteral("iMouse"), QStringLiteral("xy=pixels, zw=normalized"), QStringLiteral("vec4"));
    addVar(QStringLiteral("iDate"), QStringLiteral("year, month, day, seconds"), QStringLiteral("vec4"));
    addVar(QStringLiteral("customParams"), QStringLiteral("[8] user float params"), QStringLiteral("vec4"));
    addVar(QStringLiteral("customColors"), QStringLiteral("[16] user color params"), QStringLiteral("vec4"));
    addVar(QStringLiteral("zoneRects"), QStringLiteral("[64] zone position/size (normalized)"), QStringLiteral("vec4"));
    addVar(QStringLiteral("zoneFillColors"), QStringLiteral("[64] zone fill RGBA"), QStringLiteral("vec4"));
    addVar(QStringLiteral("zoneBorderColors"), QStringLiteral("[64] zone border RGBA"), QStringLiteral("vec4"));
    addVar(QStringLiteral("zoneParams"), QStringLiteral("[64] x=radius y=width z=highlight w=num"), QStringLiteral("vec4"));
    addVar(QStringLiteral("iChannelResolution"), QStringLiteral("[4] buffer pass sizes"), QStringLiteral("vec2"));
    addVar(QStringLiteral("iAudioSpectrumSize"), QStringLiteral("audio bar count (0=disabled)"), QStringLiteral("int"));
    addVar(QStringLiteral("iFlipBufferY"), QStringLiteral("1=OpenGL Y-flip needed"), QStringLiteral("int"));
    addVar(QStringLiteral("iTextureResolution"), QStringLiteral("[4] user texture sizes"), QStringLiteral("vec2"));

    // ── Samplers ──
    addVar(QStringLiteral("uZoneLabels"), QStringLiteral("binding 1 — zone label texture"), QStringLiteral("sampler2D"));
    addVar(QStringLiteral("uAudioSpectrum"), QStringLiteral("binding 6 — audio bars"), QStringLiteral("sampler2D"));
    addVar(QStringLiteral("uTexture0"), QStringLiteral("binding 7 — user texture 0"), QStringLiteral("sampler2D"));
    addVar(QStringLiteral("uTexture1"), QStringLiteral("binding 8 — user texture 1"), QStringLiteral("sampler2D"));
    addVar(QStringLiteral("uTexture2"), QStringLiteral("binding 9 — user texture 2"), QStringLiteral("sampler2D"));
    addVar(QStringLiteral("uTexture3"), QStringLiteral("binding 10 — user texture 3"), QStringLiteral("sampler2D"));
    addVar(QStringLiteral("uWallpaper"), QStringLiteral("binding 11 — desktop wallpaper"), QStringLiteral("sampler2D"));
    addVar(QStringLiteral("iChannel0"), QStringLiteral("binding 2 — buffer pass 0"), QStringLiteral("sampler2D"));
    addVar(QStringLiteral("iChannel1"), QStringLiteral("binding 3 — buffer pass 1"), QStringLiteral("sampler2D"));
    addVar(QStringLiteral("iChannel2"), QStringLiteral("binding 4 — buffer pass 2"), QStringLiteral("sampler2D"));
    addVar(QStringLiteral("iChannel3"), QStringLiteral("binding 5 — buffer pass 3"), QStringLiteral("sampler2D"));

    // ── Varyings ──
    addVar(QStringLiteral("vTexCoord"), QStringLiteral("UV coordinates (0-1)"), QStringLiteral("vec2"));
    addVar(QStringLiteral("vFragCoord"), QStringLiteral("pixel coordinates (Y=0 at top)"), QStringLiteral("vec2"));

    // ── Constants ──
    addConst(QStringLiteral("PI"), QStringLiteral("3.14159265359"));
    addConst(QStringLiteral("TAU"), QStringLiteral("6.28318530718"));

    // ── common.glsl helpers ──
    addFunc(QStringLiteral("clampFragColor"), QStringLiteral("(vec4 color)"), QStringLiteral("vec4"));
    addFunc(QStringLiteral("zoneRectPos"), QStringLiteral("(vec4 rect)"), QStringLiteral("vec2"));
    addFunc(QStringLiteral("zoneRectSize"), QStringLiteral("(vec4 rect)"), QStringLiteral("vec2"));
    addFunc(QStringLiteral("zoneLocalUV"), QStringLiteral("(vec2 fragCoord, vec2 pos, vec2 size)"), QStringLiteral("vec2"));
    addFunc(QStringLiteral("sdRoundedBox"), QStringLiteral("(vec2 p, vec2 b, float r)"), QStringLiteral("float"));
    addFunc(QStringLiteral("hash11"), QStringLiteral("(float n)"), QStringLiteral("float"));
    addFunc(QStringLiteral("hash21"), QStringLiteral("(vec2 p)"), QStringLiteral("float"));
    addFunc(QStringLiteral("hash22"), QStringLiteral("(vec2 p)"), QStringLiteral("vec2"));
    addFunc(QStringLiteral("compositeLabels"), QStringLiteral("(vec4 color, vec2 uv, sampler2D tex)"), QStringLiteral("vec4"));
    addFunc(QStringLiteral("compositeLabelsWithUv"), QStringLiteral("(vec4 color, vec2 fragCoord)"), QStringLiteral("vec4"));
    addFunc(QStringLiteral("labelsUv"), QStringLiteral("(vec2 fragCoord)"), QStringLiteral("vec2"));
    addFunc(QStringLiteral("blendOver"), QStringLiteral("(vec4 dst, vec4 src)"), QStringLiteral("vec4"));
    addFunc(QStringLiteral("softBorder"), QStringLiteral("(float d, float borderWidth)"), QStringLiteral("float"));
    addFunc(QStringLiteral("expGlow"), QStringLiteral("(float d, float falloff, float strength)"), QStringLiteral("float"));
    addFunc(QStringLiteral("colorWithFallback"), QStringLiteral("(vec3 color, vec3 fallback)"), QStringLiteral("vec3"));
    addFunc(QStringLiteral("luminance"), QStringLiteral("(vec3 c)"), QStringLiteral("float"));
    addFunc(QStringLiteral("noise1D"), QStringLiteral("(float x)"), QStringLiteral("float"));
    addFunc(QStringLiteral("noise2D"), QStringLiteral("(vec2 p)"), QStringLiteral("float"));
    addFunc(QStringLiteral("angularNoise"), QStringLiteral("(float angle, float freq, float seed)"), QStringLiteral("float"));
    addFunc(QStringLiteral("zoneVitality"), QStringLiteral("(bool isHighlighted)"), QStringLiteral("float"));
    addFunc(QStringLiteral("vitalityDesaturate"), QStringLiteral("(vec3 col, float vitality)"), QStringLiteral("vec3"));
    addFunc(QStringLiteral("vitalityScale"), QStringLiteral("(float dormant, float alive, float vitality)"), QStringLiteral("float"));

    // ── audio.glsl ──
    addFunc(QStringLiteral("audioBar"), QStringLiteral("(int barIndex)"), QStringLiteral("float"));
    addFunc(QStringLiteral("audioBarSmooth"), QStringLiteral("(float u)"), QStringLiteral("float"));
    addFunc(QStringLiteral("getBass"), QStringLiteral("()"), QStringLiteral("float"));
    addFunc(QStringLiteral("getMids"), QStringLiteral("()"), QStringLiteral("float"));
    addFunc(QStringLiteral("getTreble"), QStringLiteral("()"), QStringLiteral("float"));
    addFunc(QStringLiteral("getOverall"), QStringLiteral("()"), QStringLiteral("float"));
    addFunc(QStringLiteral("getBassSoft"), QStringLiteral("()"), QStringLiteral("float"));
    addFunc(QStringLiteral("getMidsSoft"), QStringLiteral("()"), QStringLiteral("float"));
    addFunc(QStringLiteral("getTrebleSoft"), QStringLiteral("()"), QStringLiteral("float"));
    addFunc(QStringLiteral("getOverallSoft"), QStringLiteral("()"), QStringLiteral("float"));

    // ── multipass.glsl ──
    addFunc(QStringLiteral("channelUv"), QStringLiteral("(int channelIndex, vec2 fragCoord)"), QStringLiteral("vec2"));

    // ── wallpaper.glsl ──
    addFunc(QStringLiteral("wallpaperUv"), QStringLiteral("(vec2 fragCoord, vec2 screenRes)"), QStringLiteral("vec2"));
}

KTextEditor::Range GlslCompletionModel::completionRange(KTextEditor::View* view, const KTextEditor::Cursor& position)
{
    const QString line = view->document()->line(position.line());
    int start = position.column();
    while (start > 0 && (line[start - 1].isLetterOrNumber() || line[start - 1] == QLatin1Char('_'))) {
        --start;
    }
    return KTextEditor::Range(position.line(), start, position.line(), position.column());
}

bool GlslCompletionModel::shouldAbortCompletion(KTextEditor::View* view, const KTextEditor::Range& range,
                                                  const QString& currentCompletion)
{
    Q_UNUSED(view)
    Q_UNUSED(range)
    return currentCompletion.isEmpty();
}

void GlslCompletionModel::completionInvoked(KTextEditor::View* view, const KTextEditor::Range& range,
                                             InvocationType invocationType)
{
    Q_UNUSED(invocationType)

    const QString prefix = view->document()->text(range).toLower();

    beginResetModel();
    m_filtered.clear();
    if (prefix.length() >= 2) {
        for (const auto& item : m_allItems) {
            if (item.name.toLower().startsWith(prefix)) {
                m_filtered.append(&item);
            }
        }
    }
    setRowCount(m_filtered.size());
    endResetModel();
}

QVariant GlslCompletionModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_filtered.size()) {
        return {};
    }

    const CompletionItem& item = *m_filtered[index.row()];

    switch (role) {
    case Qt::DisplayRole:
        switch (index.column()) {
        case Name:
            return item.name;
        case Prefix:
            return item.prefix;
        case Postfix:
            return item.detail;
        default:
            break;
        }
        break;

    case Qt::DecorationRole:
        if (index.column() == Icon) {
            if (item.properties & Function) return m_funcIcon;
            if (item.prefix == QLatin1String("const")) return m_macroIcon;
            return m_varIcon;
        }
        break;

    case CompletionRole:
        return item.properties;

    default:
        break;
    }

    return {};
}

} // namespace PlasmaZones
