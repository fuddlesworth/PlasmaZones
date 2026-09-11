// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shaderpreviewcontroller.h"

#include <QQuickItem>
#include <QQuickWindow>

#include "daemon/rendering/zonelabeltexturebuilder.h"
#include "phosphor_i18n.h"

#include <PhosphorAudio/CavaSpectrumProvider.h>
#include <PhosphorShaders/PixelUnits.h>
#include <PhosphorShaders/ShaderRegistry.h>
#include <PhosphorZones/Zone.h>
#include <PhosphorZones/ZoneDefaults.h>
#include <PhosphorZones/ZoneJsonKeys.h>

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QSet>
#include <QStandardPaths>

#include <algorithm>

namespace PlasmaZones {

namespace {
Q_LOGGING_CATEGORY(lcShaderPreview, "plasmazones.shaderpreview")

using ShaderInfo = PhosphorShaders::ShaderRegistry::ShaderInfo;
using ParameterInfo = PhosphorShaders::ShaderRegistry::ParameterInfo;

// Shader preset FILE format keys. These used to alias ZoneJsonKeys::ShaderId/
// ShaderParams; those layout keys are gone (assignments live in the config
// OverlayShaderTree now), but existing preset files on disk keep this shape,
// so the spelling is pinned here.
constexpr QLatin1String PresetShaderId{"shaderId"};
constexpr QLatin1String PresetShaderParams{"shaderParams"};

// Mirror ZoneManager::isFixedMode without depending on the editor service: a
// zone is fixed-geometry when its GeometryMode key equals ZoneGeometryMode::Fixed.
bool zoneIsFixedMode(const QVariantMap& zone)
{
    return zone.value(::PhosphorZones::ZoneJsonKeys::GeometryMode, 0).toInt()
        == static_cast<int>(::PhosphorZones::ZoneGeometryMode::Fixed);
}

// Single source of truth for the premultiplied fill/border appearance channels
// a ZoneShaderItem preview consumes — keeps the C++→QML key strings spelled once
// rather than duplicated across the fallback and per-zone branches below.
void writeZoneAppearance(QVariantMap& out, const QColor& fill, qreal fillAlpha, const QColor& border,
                         qreal borderRadius, qreal borderWidth)
{
    out[QLatin1String("fillR")] = fill.redF() * fillAlpha;
    out[QLatin1String("fillG")] = fill.greenF() * fillAlpha;
    out[QLatin1String("fillB")] = fill.blueF() * fillAlpha;
    out[QLatin1String("fillA")] = fillAlpha;
    out[QLatin1String("borderR")] = border.redF();
    out[QLatin1String("borderG")] = border.greenF();
    out[QLatin1String("borderB")] = border.blueF();
    out[QLatin1String("borderA")] = border.alphaF();
    out[QLatin1String("shaderBorderRadius")] = borderRadius;
    out[QLatin1String("shaderBorderWidth")] = borderWidth;
}
} // namespace

ShaderPreviewController::ShaderPreviewController(IShaderPreviewBackend* backend, QObject* parent)
    : QObject(parent)
    , m_backend(backend)
{
}

ShaderPreviewController::~ShaderPreviewController()
{
    // Symmetric teardown: stop CAVA capture explicitly rather than relying on
    // ~CavaSpectrumProvider (a QObject child destroyed after this body) to halt
    // the external process. Stop the provider directly — no signal emission
    // during destruction.
    if (m_audioProvider && m_audioProvider->isRunning()) {
        m_audioProvider->stop();
    }
}

QVariantList ShaderPreviewController::zonesForShaderPreview(int width, int height) const
{
    QVariantList result;
    if (width <= 0 || height <= 0) {
        return result;
    }
    // No backend → nothing to preview, and targetScreenSize() below would deref
    // it; bail symmetrically with translateShaderParams / getShaderInfo.
    if (!m_backend) {
        return result;
    }

    const qreal resW = static_cast<qreal>(width);
    const qreal resH = static_cast<qreal>(height);

    // Use the backend's zones so the preview matches what each backend wants:
    // the editor returns the live edited layout, the settings app the shipped
    // master-stack. A backend with no zones falls through to the single-zone
    // fallback below.
    const QVariantList zones = m_backend->previewZones();

    // How much this preview shrinks the screen, for the px-denominated
    // appearance values below. One definition of the policy, shared with the
    // shader's own px parameters via translateShaderParams. Computed before the
    // empty-zones branch so the fallback zone's border scales too — otherwise
    // its border would be raw while the shader's px params for the same frame
    // were scaled, which is the mismatch this scaling exists to remove.
    // m_backend is non-null here (bailed above), so previewPixelScale's own
    // backend guard is already satisfied.
    const qreal pixelScale = previewPixelScale(width);
    // The width has a floor and the radius does not: a border scaled below one
    // device pixel stops being a thin border and becomes an absent one, while a
    // corner radius rounding away to square is the honest miniature of a small
    // radius on a big screen. The floor keeps a thin border visible; it must
    // not invent one, so a zone that asked for no border still gets none.
    const auto scaledBorderWidth = [pixelScale](qreal borderWidth) {
        return borderWidth > 0.0 ? qMax(1.0, borderWidth * pixelScale) : 0.0;
    };

    if (zones.isEmpty()) {
        // Fallback: single zone filling the preview area
        QVariantMap out;
        out[QLatin1String(::PhosphorZones::ZoneJsonKeys::Id)] =
            QStringLiteral("{00000000-0000-0000-0000-000000000001}");
        out[QLatin1String(::PhosphorZones::ZoneJsonKeys::X)] = 4.0;
        out[QLatin1String(::PhosphorZones::ZoneJsonKeys::Y)] = 4.0;
        out[QLatin1String(::PhosphorZones::ZoneJsonKeys::Width)] = resW - 8.0;
        out[QLatin1String(::PhosphorZones::ZoneJsonKeys::Height)] = resH - 8.0;
        out[QLatin1String(::PhosphorZones::ZoneJsonKeys::ZoneNumber)] = 1;
        out[QLatin1String(::PhosphorZones::ZoneJsonKeys::IsHighlighted)] = false;
        writeZoneAppearance(out, ::PhosphorZones::ZoneDefaults::HighlightColor, ::PhosphorZones::ZoneDefaults::Opacity,
                            ::PhosphorZones::ZoneDefaults::BorderColor,
                            static_cast<qreal>(::PhosphorZones::ZoneDefaults::BorderRadius) * pixelScale,
                            scaledBorderWidth(static_cast<qreal>(::PhosphorZones::ZoneDefaults::BorderWidth)));
        result.append(out);
        return result;
    }

    // Screen size for converting fixed-geometry pixel coords to fractional
    const QSize screenSz = m_backend->targetScreenSize();
    const qreal screenW = qMax(1.0, static_cast<qreal>(screenSz.width()));
    const qreal screenH = qMax(1.0, static_cast<qreal>(screenSz.height()));

    // Scale zone coordinates to preview pixel dimensions.
    // Relative zones: fractional 0-1 * preview size.
    // Fixed zones: pixel coords / screen size * preview size.
    for (const QVariant& zoneVar : zones) {
        const QVariantMap zone = zoneVar.toMap();
        const bool isFixed = zoneIsFixedMode(zone);

        qreal px, py, pw, ph;
        if (isFixed) {
            // Fixed geometry: pixel coords relative to screen, scale to preview
            px = zone.value(::PhosphorZones::ZoneJsonKeys::FixedX, 0.0).toReal() / screenW * resW;
            py = zone.value(::PhosphorZones::ZoneJsonKeys::FixedY, 0.0).toReal() / screenH * resH;
            pw = zone.value(::PhosphorZones::ZoneJsonKeys::FixedWidth, 100.0).toReal() / screenW * resW;
            ph = zone.value(::PhosphorZones::ZoneJsonKeys::FixedHeight, 100.0).toReal() / screenH * resH;
        } else {
            // Relative geometry: fractional 0-1, scale to preview
            px = zone.value(::PhosphorZones::ZoneJsonKeys::X).toReal() * resW;
            py = zone.value(::PhosphorZones::ZoneJsonKeys::Y).toReal() * resH;
            pw = zone.value(::PhosphorZones::ZoneJsonKeys::Width).toReal() * resW;
            ph = zone.value(::PhosphorZones::ZoneJsonKeys::Height).toReal() * resH;
        }

        QVariantMap out;
        out[QLatin1String(::PhosphorZones::ZoneJsonKeys::Id)] = zone.value(::PhosphorZones::ZoneJsonKeys::Id);
        out[QLatin1String(::PhosphorZones::ZoneJsonKeys::X)] = px;
        out[QLatin1String(::PhosphorZones::ZoneJsonKeys::Y)] = py;
        out[QLatin1String(::PhosphorZones::ZoneJsonKeys::Width)] = pw;
        out[QLatin1String(::PhosphorZones::ZoneJsonKeys::Height)] = ph;
        out[QLatin1String(::PhosphorZones::ZoneJsonKeys::ZoneNumber)] =
            zone.value(::PhosphorZones::ZoneJsonKeys::ZoneNumber);
        out[QLatin1String(::PhosphorZones::ZoneJsonKeys::IsHighlighted)] =
            zone.value(::PhosphorZones::ZoneJsonKeys::IsHighlighted, false);

        // Fill color from zone appearance (or defaults)
        const bool useCustom = zone.value(::PhosphorZones::ZoneJsonKeys::UseCustomColors).toBool();
        QColor fillColor(zone.value(::PhosphorZones::ZoneJsonKeys::HighlightColor).toString());
        if (!useCustom || !fillColor.isValid())
            fillColor = ::PhosphorZones::ZoneDefaults::HighlightColor;
        const qreal alpha = useCustom
            ? zone.value(::PhosphorZones::ZoneJsonKeys::ActiveOpacity, ::PhosphorZones::ZoneDefaults::Opacity).toReal()
            : ::PhosphorZones::ZoneDefaults::Opacity;

        // Border color
        QColor borderColor(zone.value(::PhosphorZones::ZoneJsonKeys::BorderColor).toString());
        if (!useCustom || !borderColor.isValid())
            borderColor = ::PhosphorZones::ZoneDefaults::BorderColor;

        // Border dimensions
        const qreal borderRadius = useCustom
            ? zone.value(::PhosphorZones::ZoneJsonKeys::BorderRadius, ::PhosphorZones::ZoneDefaults::BorderRadius)
                  .toReal()
            : static_cast<qreal>(::PhosphorZones::ZoneDefaults::BorderRadius);
        const qreal borderWidth = useCustom
            ? zone.value(::PhosphorZones::ZoneJsonKeys::BorderWidth, ::PhosphorZones::ZoneDefaults::BorderWidth)
                  .toReal()
            : static_cast<qreal>(::PhosphorZones::ZoneDefaults::BorderWidth);

        // Border width and radius are px on the SCREEN, and the zone rects
        // above have just been scaled into a preview a fraction of its size.
        // Passed through raw they would draw a border several times too thick
        // and corners several times too round for the geometry they sit on —
        // the same size-mismatch the shader's own px parameters get from
        // translateShaderParams. `pixelScale` is that one factor.
        writeZoneAppearance(out, fillColor, alpha, borderColor, borderRadius * pixelScale,
                            scaledBorderWidth(borderWidth));

        result.append(out);
    }

    return result;
}

double ShaderPreviewController::previewPixelScale(int previewWidth) const
{
    if (!m_backend || previewWidth <= 0) {
        return 1.0;
    }
    const int screenW = m_backend->targetScreenSize().width();
    if (screenW <= 0) {
        return 1.0;
    }
    // Capped at 1 for the same reason the zone geometry above is never
    // magnified: a preview larger than the screen is still a preview OF that
    // screen, and inflating px values past what the shader declares would show
    // an effect the user cannot get.
    return std::min(1.0, static_cast<double>(previewWidth) / static_cast<double>(screenW));
}

QVariantMap ShaderPreviewController::translateShaderParams(const QString& shaderId, const QVariantMap& params,
                                                           int previewWidth) const
{
    if (!m_backend) {
        return QVariantMap();
    }
    const double scale = previewPixelScale(previewWidth);
    if (qFuzzyCompare(scale, 1.0)) {
        return m_backend->translateParams(shaderId, params);
    }
    // Scaled BEFORE translation, while the map is still keyed by parameter id:
    // after translateParams the keys are UBO lane names and the metadata that
    // says which of them are px no longer matches anything.
    QVariantMap scaled = params;
    const QVariantList paramInfos = parameterInfos(shaderId);
    if (paramInfos.isEmpty() && !PhosphorShaders::ShaderRegistry::isNoneShader(shaderId)) {
        // zonesForShaderPreview has already scaled the zone geometry for this
        // frame; leaving the shader's px params unscaled reproduces exactly the
        // geometry/params mismatch the scaling exists to remove. The backend
        // logs its own failure, but only on its side, so say it here too.
        qCWarning(lcShaderPreview) << "No parameter metadata for shader" << shaderId
                                   << "- px parameters left unscaled at preview scale" << scale;
    }
    PhosphorShaders::scalePixelParams(paramInfos, scaled, scale);
    return m_backend->translateParams(shaderId, scaled);
}

QVariantList ShaderPreviewController::parameterInfos(const QString& shaderId) const
{
    if (m_paramInfoShaderId == shaderId) {
        return m_paramInfoCache;
    }
    if (!m_backend) {
        return QVariantList();
    }
    const QVariantList infos = m_backend->shaderInfo(shaderId).value(QStringLiteral("parameters")).toList();
    // An empty answer is deliberately NOT cached: in the editor the backend is
    // a blocking D-Bus call to the daemon, and a timeout there returns an empty
    // map. Caching that would strand the shader unscaled for the rest of the
    // dialog's life, where retrying costs one round-trip on the next slider
    // move. A shader that genuinely declares no parameters re-queries too, and
    // for it the query is the cheap path anyway (nothing to scale).
    if (infos.isEmpty()) {
        return infos;
    }
    m_paramInfoShaderId = shaderId;
    m_paramInfoCache = infos;
    return m_paramInfoCache;
}

QVariantMap ShaderPreviewController::getShaderInfo(const QString& shaderId) const
{
    return m_backend ? m_backend->shaderInfo(shaderId) : QVariantMap();
}

QString ShaderPreviewController::shaderParamPreamble(const QString& shaderId) const
{
    if (PhosphorShaders::ShaderRegistry::isNoneShader(shaderId)) {
        return QString();
    }
    // Reconstruct the parameter declarations from the backend's shaderInfo (which
    // carries each param's id/type/slot as the daemon's registry resolved them)
    // and run the same generator the daemon overlay uses, so the preview's
    // `p_<id>` defines land on the exact lanes translateShaderParams uploads to.
    const QVariantMap info = getShaderInfo(shaderId);
    ShaderInfo si;
    const QVariantList params = info.value(QStringLiteral("parameters")).toList();
    for (const QVariant& pv : params) {
        const QVariantMap pm = pv.toMap();
        ParameterInfo pi;
        pi.id = pm.value(QStringLiteral("id")).toString();
        pi.type = pm.value(QStringLiteral("type")).toString();
        pi.slot = pm.value(QStringLiteral("slot"), -1).toInt();
        si.parameters.append(pi);
    }
    return PhosphorShaders::ShaderRegistry::paramPreamble(si);
}

QImage ShaderPreviewController::buildLabelsTexture(const QVariantList& zones, QQuickItem* target) const
{
    if (zones.isEmpty() || !target) {
        return QImage();
    }
    const QSize size(qMax(1, qRound(target->width())), qMax(1, qRound(target->height())));
    if (target->width() <= 0.0 || target->height() <= 0.0) {
        return QImage();
    }
    // The ratio the preview's shader pass samples this at. Window-derived, not
    // screen-derived: on Wayland QScreen::devicePixelRatio is the wl_output
    // integer buffer scale (2 on a 1.15 output), while the shader's
    // iResolution comes from QQuickWindow::effectiveDevicePixelRatio.
    const qreal dpr = target->window() ? target->window()->effectiveDevicePixelRatio() : 1.0;
    return ZoneLabelTextureBuilder::build(zones, size, dpr, Qt::white, true).toImage();
}

QImage ShaderPreviewController::loadWallpaperTexture() const
{
    return PhosphorShaders::ShaderRegistry::loadWallpaperImage();
}

QString ShaderPreviewController::wallpaperPath() const
{
    // The same resolver the decoration preview uses, so the two previews agree
    // on what "the desktop" is.
    return PhosphorShaders::ShaderRegistry::wallpaperPath();
}

QVariant ShaderPreviewController::audioSpectrumVariant() const
{
    return QVariant::fromValue(m_audioSpectrum);
}

void ShaderPreviewController::startAudioCapture()
{
    if (m_audioProvider && m_audioProvider->isRunning()) {
        return;
    }
    if (!m_backend || !m_backend->audioVisualizerEnabled()) {
        return;
    }
    if (!PhosphorAudio::CavaSpectrumProvider::isCavaInstalled()) {
        qCDebug(lcShaderPreview) << "Audio spectrum: CAVA not available, disabled";
        return;
    }
    if (!m_audioProvider) {
        m_audioProvider = new PhosphorAudio::CavaSpectrumProvider(this);
        connect(m_audioProvider, &PhosphorAudio::IAudioSpectrumProvider::spectrumUpdated, this,
                [this](const QVector<float>& spectrum) {
                    m_audioSpectrum = spectrum;
                    Q_EMIT audioSpectrumChanged();
                });
    }
    // Apply the user's full Shaders.Audio parameter set (via the backend, so
    // the settings app reads ISettings and the editor queries the daemon) —
    // the preview's bar motion matches the live daemon and effect output.
    m_audioProvider->setOptions(m_backend->audioOptions());
    m_audioProvider->start();
}

void ShaderPreviewController::stopAudioCapture()
{
    if (m_audioProvider && m_audioProvider->isRunning()) {
        m_audioProvider->stop();
    }
    if (!m_audioSpectrum.isEmpty()) {
        m_audioSpectrum.clear();
        Q_EMIT audioSpectrumChanged();
    }
}

} // namespace PlasmaZones
