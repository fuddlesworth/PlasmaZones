// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "pointerdriver.h"

#include <PhosphorPointer/PointerShaderRegistry.h>
#include <PhosphorRendering/ShaderEffect.h>

#include <QFile>
#include <QFileInfo>
#include <QUrl>
#include <QVariantMap>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace PlasmaZones::ShaderRender {

namespace {

Q_LOGGING_CATEGORY(lcPointerDriver, "plasmazones.shaderrender.pointer")

// 250 Hz, inside the range a real mouse reports at. The same figure the settings
// preview feeds at, and for the same reason: the history's sampler places a slot
// from the gap since the last event, so feeding one event per rendered frame
// would round the ring's spacing up to the frame interval and the trail would be
// longer than the one the compositor produces.
constexpr double kEventIntervalMs = 4.0;

Qt::MouseButtons buttonsFor(int code)
{
    switch (code) {
    case 2:
        return Qt::MouseButtons(Qt::RightButton);
    case 3:
        return Qt::MouseButtons(Qt::MiddleButton);
    default:
        return Qt::MouseButtons(Qt::LeftButton);
    }
}

} // namespace

bool installPointerPack(PhosphorRendering::ShaderEffect& effect, const QString& metadataPath,
                        PhosphorPointerShaders::PointerShaderEffect& parsedOut)
{
    using Registry = PhosphorPointerShaders::PointerShaderRegistry;

    QFile file(metadataPath);
    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(lcPointerDriver) << "cannot read pointer pack" << metadataPath;
        return false;
    }
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        qCWarning(lcPointerDriver) << metadataPath << "is not a JSON object —" << err.errorString();
        return false;
    }

    const QFileInfo info(metadataPath);
    const PhosphorPointerShaders::PointerShaderEffect eff =
        PhosphorPointerShaders::PointerShaderEffect::fromJson(doc.object(), info.absolutePath(), /*isUser=*/false);
    if (!eff.isValid()) {
        qCWarning(lcPointerDriver) << "pointer pack at" << metadataPath << "did not parse into a valid effect";
        return false;
    }

    // Exactly what PointerPreviewController installs, in the same order, so the
    // tool bakes byte-for-byte what the settings preview bakes for this pack.
    effect.setShaderIncludePaths(Registry::includePathsFor(eff.sourceDir));
    effect.setShaderSource(QUrl::fromLocalFile(eff.fragmentShaderPath));
    effect.setParamPreamble(Registry::paramPreamble(eff));
    effect.setEntryScaffold(Registry::pointerEntryPrologue(), Registry::pointerEntryCandidates());
    if (!eff.vertexShaderPath.isEmpty()) {
        effect.setVertexShaderUrl(QUrl::fromLocalFile(eff.vertexShaderPath));
    }
    if (eff.isMultipass && !eff.bufferShaderPaths.isEmpty()) {
        effect.setBufferShaderPaths(eff.bufferShaderPaths);
        effect.setBufferFeedback(eff.bufferFeedback);
        effect.setBufferScale(eff.bufferScale);
    } else {
        effect.setBufferShaderPaths({});
        effect.setBufferFeedback(false);
        effect.setBufferScale(1.0);
    }

    // Parameter DEFAULTS through the pointer registry's own slot allocation. The
    // overlay seeding path allocates slots differently, and paramPreamble above
    // mirrors this allocation, so seeding the other way would point every p_<id>
    // at a lane nothing was written to and the pack would render with zeros.
    QVariantMap friendly;
    for (const auto& p : eff.parameters) {
        friendly.insert(p.id, p.defaultValue);
    }
    effect.setShaderParams(Registry::translatePointerParams(eff, friendly));

    parsedOut = eff;
    return true;
}

PointerDriver::PointerDriver(const PointerDriveOptions& options, const QSize& canvasDevicePx, double scale)
    : m_opts(options)
    , m_canvas(canvasDevicePx)
    , m_scale(scale > 0.0 ? scale : 1.0)
{
}

void PointerDriver::applyPackContract(const PhosphorPointerShaders::PointerShaderEffect& effect)
{
    m_needsCursor = effect.needsCursor;

    // An EMPTY parameter map, deliberately. resolvedReach / resolvedTrailWindow
    // fall back to each named parameter's own declared default when the map has
    // no entry for it, and the declared defaults are exactly what
    // installPointerPack seeds the shader with, so the figures below are the
    // ones this render actually draws with.
    //
    // Both go through the pack's own resolvers rather than being re-derived
    // here. They carry bounds a hand-rolled copy does not: the reach is capped
    // at kMaxReach, and the read window is clamped to the pack's trailSeconds
    // (a pack cannot read further back than the host keeps it alive) and is 0
    // for a pack that does not sample the trail at all.
    m_reachLogicalPx = effect.resolvedReach({});
    m_history.setTrailSeconds(effect.resolvedTrailWindow({}));
}

QImage PointerDriver::cursorSprite() const
{
    if (!m_opts.cursorSprite) {
        return {};
    }
    const int side = std::max(4, static_cast<int>(std::lround(m_opts.cursorSize * m_scale)));
    QImage img(side, side, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);

    // A plain left-pointing arrow with its hotspot at the top-left, which is
    // where the cursor rect's origin is. The shape only has to be cursor-like:
    // what a pack reads from it is the silhouette and its alpha gradient.
    QPainterPath path;
    const double s = static_cast<double>(side);
    path.moveTo(0.04 * s, 0.02 * s);
    path.lineTo(0.10 * s, 0.94 * s);
    path.lineTo(0.38 * s, 0.66 * s);
    path.lineTo(0.74 * s, 0.62 * s);
    path.closeSubpath();

    QPainter painter(&img);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setBrush(Qt::white);
    QPen pen(QColor(30, 30, 30));
    pen.setWidthF(std::max(1.0, s * 0.05));
    painter.setPen(pen);
    painter.drawPath(path);
    painter.end();

    return img.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
}

QPointF PointerDriver::positionAt(double seconds) const
{
    const QPointF centre(m_canvas.width() * 0.5, m_canvas.height() * 0.5);
    if (m_opts.still) {
        return centre;
    }
    const double rad = m_opts.headingDegrees * M_PI / 180.0;
    const QPointF dir(std::cos(rad), std::sin(rad));
    // The pointer passes through the centre at the moment of the press, so the
    // click lands in the middle of the canvas whatever the heading is and the
    // shape it throws is never half off the edge.
    const double anchor = m_opts.pressAt >= 0.0 ? m_opts.pressAt : 0.0;
    const double travel = (seconds - anchor) * m_opts.speedPxPerSec * m_scale;
    return centre + dir * travel;
}

void PointerDriver::applyFrame(int frame, double fps, PhosphorPointerShaders::PointerUniformExtension& ext,
                               QPointF& iMouseLogical)
{
    const double now = fps > 0.0 ? static_cast<double>(frame) / fps : 0.0;

    if (!m_seeded) {
        // A ring with no samples would make the first frame's press a
        // buttons-only event with no position history behind it.
        m_history.seedPosition(positionAt(now), static_cast<qint64>(std::llround(now * 1000.0)));
        m_lastSeconds = now;
        m_seeded = true;
    }

    // Walk the interval since the previous frame as an event stream, and fire
    // the button edges at the instant they fall rather than on a frame boundary.
    const double span = std::max(0.0, now - m_lastSeconds);
    const int steps = std::clamp(static_cast<int>(std::ceil(span * 1000.0 / kEventIntervalMs)), 1, 256);
    for (int i = 1; i <= steps; ++i) {
        const double t = m_lastSeconds + span * (static_cast<double>(i) / static_cast<double>(steps));
        const auto ms = static_cast<qint64>(std::llround(t * 1000.0));
        const QPointF pos = positionAt(t);

        if (!m_opts.still) {
            m_history.notePointer(pos, ms);
        }

        const bool wantPressed =
            m_opts.pressAt >= 0.0 && t >= m_opts.pressAt && (m_opts.releaseAt < 0.0 || t < m_opts.releaseAt);
        if (wantPressed != m_pressed) {
            const Qt::MouseButtons before = m_pressed ? buttonsFor(m_opts.button) : Qt::MouseButtons(Qt::NoButton);
            const Qt::MouseButtons after = wantPressed ? buttonsFor(m_opts.button) : Qt::MouseButtons(Qt::NoButton);
            m_history.noteButtons(after, before, pos, ms);
            m_pressed = wantPressed;
        }
    }
    m_lastSeconds = now;

    const auto nowMs = static_cast<qint64>(std::llround(now * 1000.0));
    PhosphorPointerShaders::PointerFrameState state = m_history.frameState(nowMs, m_scale);

    // The sprite rect is the host's to fill in, because only the host knows
    // where its cursor is drawn. Here the hotspot is the arrow's top-left, so
    // the rect starts at the pointer and spans the drawn size, in device px like
    // every other canvas position.
    const QPointF pos = positionAt(now);
    const double side = m_opts.cursorSize * m_scale;
    // The rect spans downward from the hotspot, as the contract has it.
    state.cursorRect = QRectF(pos.x(), pos.y(), side, side);
    // Honest about the binding: true only when the caller actually bound the
    // sprite, which it does only for a pack that asked for it.
    state.hasSprite = m_opts.cursorSprite && m_needsCursor;

    ext.setReachLogicalPx(m_reachLogicalPx);
    ext.apply(state);

    // The same top-down position the trail and the press were pushed in, so
    // iMouse agrees with both without a second conversion.
    iMouseLogical = QPointF(pos.x() / m_scale, pos.y() / m_scale);
}

} // namespace PlasmaZones::ShaderRender
