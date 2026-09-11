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
// of a JSON file under layoutDir) or a direct path.
QString resolveLayoutPath(const QString& layoutArg, const QString& layoutDir)
{
    if (layoutArg.contains(QLatin1Char('/')) || layoutArg.endsWith(QLatin1String(".json"))) {
        return layoutArg;
    }
    return QDir(layoutDir).filePath(layoutArg + QLatin1String(".json"));
}

// First existing <xdg-data-dir>/plasmazones/<subdir>, or an empty string.
//
// GenericDataLocation, NOT AppDataLocation: AppDataLocation appends the
// APPLICATION name, so this probed <xdg>/plasmazones-shader-render/<subdir>,
// a directory that never exists, and the whole user tier silently resolved
// nothing. The daemon's trustedShaderRoots() uses the generic tier, and a
// preview that cannot see a pack the daemon loads is the divergence this tool
// exists to avoid.
QString xdgPlasmaZonesDir(const QString& subdir)
{
    const QString suffix = QStringLiteral("/plasmazones/") + subdir;
    const QStringList roots = QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation);
    for (const QString& root : roots) {
        const QString candidate = root + suffix;
        if (QDir(candidate).exists())
            return candidate;
    }
    return {};
}

QString defaultShaderDir()
{
    // Prefer in-tree data/overlays for development; fall back to
    // the installed location.
    const QString cwd = QDir(QStringLiteral("data/overlays")).absolutePath();
    if (QDir(cwd).exists())
        return cwd;
    // No hardcoded /usr/share tier: GenericDataLocation already ends with the
    // XDG_DATA_DIRS list, which contains it. A second spelling of a path the
    // walk above already covers is just a second thing to keep in sync.
    return xdgPlasmaZonesDir(QStringLiteral("overlays"));
}

// The pointer family's equivalent of defaultShaderDir(). Used when --pointer is
// given and --shader-dir was not, so `--pointer -s skid` finds the pack without
// the caller having to spell out a directory that the flag already implies.
QString defaultPointerDir()
{
    const QString cwd = QDir(QStringLiteral("data/pointer")).absolutePath();
    if (QDir(cwd).exists())
        return cwd;
    return xdgPlasmaZonesDir(QStringLiteral("pointer"));
}

QString defaultLayoutDir()
{
    const QString cwd = QDir(QStringLiteral("data/layouts")).absolutePath();
    if (QDir(cwd).exists())
        return cwd;
    // No hardcoded /usr/share tier: GenericDataLocation already ends with the
    // XDG_DATA_DIRS list, which contains it. A second spelling of a path the
    // walk above already covers is just a second thing to keep in sync.
    return xdgPlasmaZonesDir(QStringLiteral("layouts"));
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

    QCommandLineOption shaderDirOpt(QStringLiteral("shader-dir"),
                                    QStringLiteral("Directory containing <id>/metadata.json. "
                                                   "Defaults to data/overlays/ in the cwd, then the XDG data dirs. "
                                                   "With --pointer the default becomes data/pointer/ instead."),
                                    QStringLiteral("path"), defaultShaderDir());
    parser.addOption(shaderDirOpt);

    QCommandLineOption layoutDirOpt(QStringLiteral("layout-dir"),
                                    QStringLiteral("Directory containing <id>.json layout files. "
                                                   "Defaults to data/layouts/ in the cwd, then the XDG data dirs."),
                                    QStringLiteral("path"), defaultLayoutDir());
    parser.addOption(layoutDirOpt);

    QCommandLineOption stillHighlightOpt(
        QStringLiteral("still-highlight"),
        QStringLiteral("Pin one zone, selected by the zone number shown on it in the editor, as "
                       "the only highlighted zone for every frame. Disables the cycling demo "
                       "schedule. Useful for thumbnail captures that want a deterministic hero "
                       "zone. Defaults to 0 (cycling)."),
        QStringLiteral("ZONE_NUMBER"), QStringLiteral("0"));
    parser.addOption(stillHighlightOpt);

    // ── Pointer-pack options ─────────────────────────────────────
    // A pointer pack is driven by a synthetic pointer rather than by the zone
    // schedule. Without --pointer its whole uniform tail reads zero, so a
    // click-led pack paints nothing and the render is legitimately empty.
    QCommandLineOption pointerOpt(
        QStringLiteral("pointer"),
        QStringLiteral("Render a POINTER pack (data/pointer): drive a synthetic pointer and bind the "
                       "pointer uniform tail. Without this a pointer pack sees an all-zero tail and "
                       "paints nothing."));
    parser.addOption(pointerOpt);

    QCommandLineOption pointerHeadingOpt(
        QStringLiteral("pointer-heading"),
        QStringLiteral("Direction the synthetic pointer travels, degrees clockwise from screen right "
                       "(canvas y runs down, so 90 is downward). Defaults to 0."),
        QStringLiteral("DEGREES"), QStringLiteral("0"));
    parser.addOption(pointerHeadingOpt);

    QCommandLineOption pointerSpeedOpt(
        QStringLiteral("pointer-speed"),
        QStringLiteral("Speed of the synthetic pointer in logical px per second. Defaults to 900."),
        QStringLiteral("PX_PER_SEC"), QStringLiteral("900"));
    parser.addOption(pointerSpeedOpt);

    QCommandLineOption pointerStillOpt(
        QStringLiteral("pointer-still"),
        QStringLiteral("Hold the pointer still at the canvas centre instead of sweeping, to check "
                       "that a pack still has an axis with no motion to take one from."));
    parser.addOption(pointerStillOpt);

    QCommandLineOption pressAtOpt(
        QStringLiteral("press-at"),
        QStringLiteral("Seconds at which the button goes down, or negative for no click at all. The "
                       "pointer passes through the canvas centre at this moment. Defaults to 0.4."),
        QStringLiteral("SECONDS"), QStringLiteral("0.4"));
    parser.addOption(pressAtOpt);

    QCommandLineOption releaseAtOpt(
        QStringLiteral("release-at"),
        QStringLiteral("Seconds at which the button comes back up. Negative leaves it DOWN for the "
                       "rest of the render, which is how a hold-reading pack's window is exercised. "
                       "Defaults to 0.55."),
        QStringLiteral("SECONDS"), QStringLiteral("0.55"));
    parser.addOption(releaseAtOpt);

    QCommandLineOption pointerButtonOpt(
        QStringLiteral("pointer-button"),
        QStringLiteral("Which button the synthetic click uses: 1 left, 2 right, 3 middle. Defaults to 1."),
        QStringLiteral("N"), QStringLiteral("1"));
    parser.addOption(pointerButtonOpt);

    QCommandLineOption noCursorSpriteOpt(
        QStringLiteral("no-cursor-sprite"),
        QStringLiteral("Do not bind the synthetic cursor sprite, so a needsCursor pack renders its "
                       "no-sprite fallback instead. That fallback is what the settings preview shows."));
    parser.addOption(noCursorSpriteOpt);

    QCommandLineOption cursorSizeOpt(
        QStringLiteral("cursor-size"),
        QStringLiteral("Drawn size of the synthetic cursor sprite, logical px. Defaults to 24."), QStringLiteral("PX"),
        QStringLiteral("24"));
    parser.addOption(cursorSizeOpt);

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

    bool stillHighlightOk = false;
    const int stillHighlightZone = parser.value(stillHighlightOpt).toInt(&stillHighlightOk);
    if (!stillHighlightOk || stillHighlightZone < 0) {
        std::cerr << "error: --still-highlight must be a non-negative integer (0 = disabled)\n";
        return 2;
    }

    PlasmaZones::ShaderRender::PointerDriveOptions pointerOptions;
    pointerOptions.enabled = parser.isSet(pointerOpt);
    if (pointerOptions.enabled) {
        bool headingOk = false;
        bool speedOk = false;
        bool pressOk = false;
        bool releaseOk = false;
        bool buttonOk = false;
        bool cursorOk = false;
        pointerOptions.headingDegrees = parser.value(pointerHeadingOpt).toDouble(&headingOk);
        pointerOptions.speedPxPerSec = parser.value(pointerSpeedOpt).toDouble(&speedOk);
        pointerOptions.pressAt = parser.value(pressAtOpt).toDouble(&pressOk);
        pointerOptions.releaseAt = parser.value(releaseAtOpt).toDouble(&releaseOk);
        pointerOptions.button = parser.value(pointerButtonOpt).toInt(&buttonOk);
        pointerOptions.cursorSize = parser.value(cursorSizeOpt).toDouble(&cursorOk);
        pointerOptions.still = parser.isSet(pointerStillOpt);
        pointerOptions.cursorSprite = !parser.isSet(noCursorSpriteOpt);
        if (!headingOk || !speedOk || !pressOk || !releaseOk || !cursorOk) {
            std::cerr << "error: --pointer-heading, --pointer-speed, --press-at, --release-at and "
                         "--cursor-size must be numbers\n";
            return 2;
        }
        if (pointerOptions.speedPxPerSec < 0.0 || pointerOptions.cursorSize <= 0.0) {
            std::cerr << "error: --pointer-speed must not be negative and --cursor-size must be positive\n";
            return 2;
        }
        if (!buttonOk || pointerOptions.button < 1 || pointerOptions.button > 3) {
            std::cerr << "error: --pointer-button must be 1 (left), 2 (right) or 3 (middle)\n";
            return 2;
        }
        // A release before the press would hand the history a button-up edge it
        // never saw go down, and the pack would read a release older than its
        // press. Caught here rather than producing a quietly wrong render.
        if (pointerOptions.releaseAt >= 0.0 && pointerOptions.pressAt >= 0.0
            && pointerOptions.releaseAt < pointerOptions.pressAt) {
            std::cerr << "error: --release-at must not be before --press-at\n";
            return 2;
        }
    }

    // ── Resolve inputs ───────────────────────────────────────────
    const QString shaderArg = parser.value(shaderOpt);
    const QString layoutArg = parser.value(layoutOpt);
    // --pointer implies the pointer pack tree, unless the caller named a
    // directory themselves. The option's own default is computed before the
    // command line is parsed, so the substitution has to happen here.
    const QString shaderDir =
        (pointerOptions.enabled && !parser.isSet(shaderDirOpt)) ? defaultPointerDir() : parser.value(shaderDirOpt);
    const QString layoutDir = parser.value(layoutDirOpt);

    const QString metadataPath = resolveShaderMetadata(shaderArg, shaderDir);
    if (!QFileInfo::exists(metadataPath)) {
        std::cerr << "error: shader metadata not found: " << metadataPath.toStdString() << "\n";
        return 1;
    }
    // The pack layout contract requires the exact name metadata.json: both
    // the parameter-defaults loader and the renderer's p_<id> preamble
    // re-read <dir>/metadata.json by that name, so accepting another
    // basename would seed top-level fields from one file and parameters
    // from another. Reject at the boundary with a clear message instead.
    if (QFileInfo(metadataPath).fileName() != QLatin1String("metadata.json")) {
        std::cerr << "error: --shader path must point at a file named metadata.json, got " << metadataPath.toStdString()
                  << "\n";
        return 2;
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
    opts.metadataPath = QFileInfo(metadataPath).absoluteFilePath();
    opts.zones = zones;
    opts.resolution = resolution;
    opts.frameCount = frameCount;
    opts.fps = fps;
    opts.audio = audio.get();
    opts.stillHighlightZone = stillHighlightZone;
    opts.pointer = pointerOptions;

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
