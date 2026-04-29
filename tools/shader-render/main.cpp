// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// plasmazones-shader-render — headless offscreen shader renderer.
//
// Renders any bundled shader to a video file or PNG sequence
// without a compositor, display, or screen-capture tool.  Uses
// Qt RHI offscreen, so it works in CI with software Vulkan
// (lavapipe / llvmpipe).  Same shader pipeline the runtime uses,
// so the output is the same as what shows up under a real layer-
// shell overlay — just sized and framed however the docs need.
//
// Usage example:
//
//   plasmazones-shader-render --shader neon-city --layout master-stack
//                             --resolution 1920x1080 --frames 150 --fps 30
//                             --out /tmp/neon-city.webm
//
// See README.md for the full flag reference.

#include "renderer.h"
#include "layoutloader.h"
#include "metadataloader.h"
#include "audiomock.h"
#include "encoder.h"

#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QStandardPaths>
#include <QSize>

#include <iostream>

namespace {

QSize parseResolution(const QString& s)
{
    const QStringList parts = s.split(QLatin1Char('x'), Qt::SkipEmptyParts);
    if (parts.size() != 2)
        return QSize();
    bool okW = false, okH = false;
    const int w = parts[0].toInt(&okW);
    const int h = parts[1].toInt(&okH);
    if (!okW || !okH || w <= 0 || h <= 0)
        return QSize();
    return QSize(w, h);
}

// Resolve a shader-id-or-path argument:
//   "neon-city"          → ${shaderDir}/neon-city/metadata.json
//   "/abs/path/.../meta" → use as-is
QString resolveShaderMetadata(const QString& shaderArg, const QString& shaderDir)
{
    if (shaderArg.contains(QLatin1Char('/')) || shaderArg.endsWith(QLatin1String(".json"))) {
        return shaderArg;
    }
    return QDir(shaderDir).filePath(shaderArg + QLatin1String("/metadata.json"));
}

// Same logic for layouts — argument is either a layout id (basename
// of a JSON file under shaderDir) or a direct path.
QString resolveLayoutPath(const QString& layoutArg, const QString& layoutDir)
{
    if (layoutArg.contains(QLatin1Char('/')) || layoutArg.endsWith(QLatin1String(".json"))) {
        return layoutArg;
    }
    return QDir(layoutDir).filePath(layoutArg + QLatin1String(".json"));
}

QString defaultShaderDir()
{
    // Prefer in-tree data/shaders for development; fall back to
    // the installed location.
    const QString cwd = QDir(QStringLiteral("data/shaders")).absolutePath();
    if (QDir(cwd).exists())
        return cwd;
    const QString xdg = QStandardPaths::locate(QStandardPaths::AppDataLocation, QStringLiteral("shaders"),
                                               QStandardPaths::LocateDirectory);
    if (!xdg.isEmpty())
        return xdg;
    return QStringLiteral("/usr/share/plasmazones/shaders");
}

QString defaultLayoutDir()
{
    const QString cwd = QDir(QStringLiteral("data/layouts")).absolutePath();
    if (QDir(cwd).exists())
        return cwd;
    const QString xdg = QStandardPaths::locate(QStandardPaths::AppDataLocation, QStringLiteral("layouts"),
                                               QStandardPaths::LocateDirectory);
    if (!xdg.isEmpty())
        return xdg;
    return QStringLiteral("/usr/share/plasmazones/layouts");
}

} // namespace

int main(int argc, char* argv[])
{
    // No forced QT_QPA_PLATFORM — the offscreen platform plugin
    // doesn't provide an RHI-compatible surface, so
    // QQuickRenderControl::initialize() fails under it.  Run under
    // the ambient session instead (Wayland, X11, or a headless
    // platform like minimal/eglfs-kms that the user sets via
    // environment).  The QQuickWindow we create is never shown,
    // so an interactive session isn't bothered.  For CI support,
    // future work is to set up an offscreen QRhi texture target
    // explicitly via QQuickRenderTarget::fromRhiRenderTarget().

    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("plasmazones-shader-render"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Headless offscreen renderer for PlasmaZones shaders."));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption shaderOpt(QStringList() << QStringLiteral("s") << QStringLiteral("shader"),
                                 QStringLiteral("Shader id (e.g. \"neon-city\") or path to a metadata.json."),
                                 QStringLiteral("shader"));
    parser.addOption(shaderOpt);

    QCommandLineOption layoutOpt(QStringList() << QStringLiteral("l") << QStringLiteral("layout"),
                                 QStringLiteral("Layout id (e.g. \"master-stack\") or path to a layout JSON. "
                                                "The shader renders inside the layout's zones."),
                                 QStringLiteral("layout"), QStringLiteral("master-stack"));
    parser.addOption(layoutOpt);

    QCommandLineOption resolutionOpt(QStringList() << QStringLiteral("r") << QStringLiteral("resolution"),
                                     QStringLiteral("Render resolution as WxH pixels. Defaults to 1920x1080."),
                                     QStringLiteral("WxH"), QStringLiteral("1920x1080"));
    parser.addOption(resolutionOpt);

    QCommandLineOption framesOpt(QStringLiteral("frames"),
                                 QStringLiteral("Number of frames to render. Defaults to 150 (5 seconds at 30 fps)."),
                                 QStringLiteral("N"), QStringLiteral("150"));
    parser.addOption(framesOpt);

    QCommandLineOption fpsOpt(QStringLiteral("fps"),
                              QStringLiteral("Frame rate (also drives iTime advancement). Defaults to 30."),
                              QStringLiteral("FPS"), QStringLiteral("30"));
    parser.addOption(fpsOpt);

    QCommandLineOption outOpt(
        QStringList() << QStringLiteral("o") << QStringLiteral("out"),
        QStringLiteral("Output path. Extension picks the format: .webm/.mp4 = encoded video, "
                       ".png = numbered sequence (out_000001.png, ...). Defaults to <shader>.webm."),
        QStringLiteral("path"));
    parser.addOption(outOpt);

    QCommandLineOption outputSizeOpt(QStringLiteral("output-size"),
                                     QStringLiteral("Output dimensions if different from --resolution. WxH format."),
                                     QStringLiteral("WxH"));
    parser.addOption(outputSizeOpt);

    QCommandLineOption audioModeOpt(QStringLiteral("audio-mode"),
                                    QStringLiteral("Audio spectrum source for audio-reactive shaders. "
                                                   "One of: silent, sine, noise, sweep. Defaults to sine."),
                                    QStringLiteral("mode"), QStringLiteral("sine"));
    parser.addOption(audioModeOpt);

    QCommandLineOption shaderDirOpt(
        QStringLiteral("shader-dir"),
        QStringLiteral("Directory containing <id>/metadata.json. "
                       "Defaults to data/shaders/ in the cwd, then /usr/share/plasmazones/shaders/."),
        QStringLiteral("path"), defaultShaderDir());
    parser.addOption(shaderDirOpt);

    QCommandLineOption layoutDirOpt(
        QStringLiteral("layout-dir"),
        QStringLiteral("Directory containing <id>.json layout files. "
                       "Defaults to data/layouts/ in the cwd, then /usr/share/plasmazones/layouts/."),
        QStringLiteral("path"), defaultLayoutDir());
    parser.addOption(layoutDirOpt);

    parser.process(app);

    if (!parser.isSet(shaderOpt)) {
        std::cerr << "error: --shader is required\n";
        return 2;
    }

    const QSize resolution = parseResolution(parser.value(resolutionOpt));
    if (!resolution.isValid()) {
        std::cerr << "error: --resolution must be WxH (e.g. 1920x1080)\n";
        return 2;
    }
    QSize outputSize = resolution;
    if (parser.isSet(outputSizeOpt)) {
        outputSize = parseResolution(parser.value(outputSizeOpt));
        if (!outputSize.isValid()) {
            std::cerr << "error: --output-size must be WxH\n";
            return 2;
        }
    }

    // Aspect-ratio mismatch silently stretches the frame (encoder uses
    // IgnoreAspectRatio). For batch jobs writing the docs site this is a
    // footgun — warn at the boundary so authors notice before regenerating
    // dozens of clips.
    if (outputSize != resolution) {
        const double srcAspect = static_cast<double>(resolution.width()) / resolution.height();
        const double dstAspect = static_cast<double>(outputSize.width()) / outputSize.height();
        if (!qFuzzyCompare(srcAspect + 1.0, dstAspect + 1.0)) {
            std::cerr << "warning: --resolution " << resolution.width() << "x" << resolution.height() << " (aspect "
                      << srcAspect << ") differs from --output-size " << outputSize.width() << "x"
                      << outputSize.height() << " (aspect " << dstAspect << ") — frames will be stretched\n";
        }
    }

    bool framesOk = false, fpsOk = false;
    const int frameCount = parser.value(framesOpt).toInt(&framesOk);
    const int fps = parser.value(fpsOpt).toInt(&fpsOk);
    if (!framesOk || frameCount <= 0 || !fpsOk || fps <= 0) {
        std::cerr << "error: --frames and --fps must be positive integers\n";
        return 2;
    }

    // ── Resolve inputs ───────────────────────────────────────────
    const QString shaderArg = parser.value(shaderOpt);
    const QString layoutArg = parser.value(layoutOpt);
    const QString shaderDir = parser.value(shaderDirOpt);
    const QString layoutDir = parser.value(layoutDirOpt);

    const QString metadataPath = resolveShaderMetadata(shaderArg, shaderDir);
    if (!QFileInfo::exists(metadataPath)) {
        std::cerr << "error: shader metadata not found: " << metadataPath.toStdString() << "\n";
        return 1;
    }

    const QString layoutPath = resolveLayoutPath(layoutArg, layoutDir);
    if (!QFileInfo::exists(layoutPath)) {
        std::cerr << "error: layout not found: " << layoutPath.toStdString() << "\n";
        return 1;
    }

    // Default output: <shader-id>.webm in cwd.
    QString outPath = parser.value(outOpt);
    if (outPath.isEmpty()) {
        const QString id = QFileInfo(metadataPath).dir().dirName();
        outPath = id + QStringLiteral(".webm");
    }

    // ── Load metadata + layout ──────────────────────────────────
    PlasmaZones::ShaderRender::ShaderMetadata metadata;
    if (!PlasmaZones::ShaderRender::loadShaderMetadata(metadataPath, metadata)) {
        std::cerr << "error: failed to load shader metadata: " << metadataPath.toStdString() << "\n";
        return 1;
    }

    QVector<PlasmaZones::ShaderRender::Zone> zones;
    if (!PlasmaZones::ShaderRender::loadLayoutZones(layoutPath, resolution, zones)) {
        std::cerr << "error: failed to load layout: " << layoutPath.toStdString() << "\n";
        return 1;
    }

    // ── Set up audio spectrum source ────────────────────────────
    auto audio = PlasmaZones::ShaderRender::makeAudioMock(parser.value(audioModeOpt));
    if (!audio) {
        std::cerr << "error: --audio-mode must be one of: silent, sine, noise, sweep\n";
        return 2;
    }

    // ── Render ──────────────────────────────────────────────────
    PlasmaZones::ShaderRender::RenderOptions opts;
    opts.metadata = metadata;
    opts.zones = zones;
    opts.resolution = resolution;
    opts.frameCount = frameCount;
    opts.fps = fps;
    opts.audio = audio.get();

    auto sink = PlasmaZones::ShaderRender::makeFrameSink(outPath, outputSize, fps);
    if (!sink) {
        std::cerr << "error: couldn't create output sink for " << outPath.toStdString() << "\n";
        return 1;
    }
    opts.sink = sink.get();

    PlasmaZones::ShaderRender::Renderer renderer;
    const int rc = renderer.render(opts);
    if (rc != 0) {
        std::cerr << "error: render failed (code " << rc << ")\n";
        return rc;
    }

    std::cout << "wrote " << outPath.toStdString() << " (" << frameCount << " frames @ " << outputSize.width() << "x"
              << outputSize.height() << ")\n";
    return 0;
}
