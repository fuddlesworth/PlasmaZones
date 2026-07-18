// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_surface_decoration_orientation.cpp
 * @brief Surface-decoration chain renders upright on OpenGL (GPU-gated).
 *
 * Regression guard for the per-render-target NDC Y-flip: a multi-stage
 * decoration chain folds through ShaderEffectSource taps (stage k renders
 * INTO a texture target that stage k+1 samples), and applying the
 * direct-to-window Y-flip on those passes rendered every 2+ pack chain
 * upside down on OpenGL. The test forces QSG_RHI_BACKEND=opengl, replicates
 * SurfaceDecoration.qml's capture/stage fold over a red-top/blue-bottom
 * gradient card with the bundled border+shadow packs, grabs the window, and
 * asserts the gradient is upright: a wrong per-target flip on either pass
 * (the tap's texture render or the final direct draw) lands blue on top.
 *
 * OPT-IN: this test connects to the LIVE session (it must — an offscreen GL
 * grab returns a uniform placeholder and proves nothing), and mapping a real
 * window has side effects on a developer machine: the installed KWin effect
 * reacts to the new window with D-Bus calls to org.plasmazones, whose
 * activation service file spawns the installed plasmazonesd (and from there
 * the daemon can launch plasmazones-settings) — processes a test run must
 * never leave behind. CI containers have the inverse problem: a display and
 * a GL context exist, so every environment gate passes, but the shader
 * pipeline never becomes ready and the grab stays a uniform placeholder,
 * failing the assert without testing anything. So the live run requires
 * PLASMAZONES_GPU_TESTS=1; without it the test exits with the ctest
 * SKIP_RETURN_CODE before QGuiApplication. When opted in, in-test gates
 * (no GL context, wrong backend, no grab, chain never renders in either
 * orientation) QSKIP, which qExec reports as a green run — a regression is
 * only declared on the flip's actual signature (blue on top).
 */

#include <QElapsedTimer>
#include <QGuiApplication>
#include <QImage>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QQmlContext>
#include <QQuickView>
#include <QtQml/qqml.h>
#include <QSGRendererInterface>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

#include <utility>

#include <PhosphorSurface/SurfaceShaderRegistry.h>

#include "daemon/rendering/surfaceshaderitem.h"

using PhosphorSurfaceShaders::SurfaceShaderRegistry;

namespace {

// Minimal replica of SurfaceDecoration.qml's capture + stage fold: gradient
// card -> hideSource snapshot -> stage 0 (border) -> tap -> stage 1 (shadow).
// Kept in-source so the test is self-contained; the structural contract it
// mirrors is documented in src/ui/SurfaceDecoration.qml.
constexpr auto kSceneQml = R"(
import PlasmaZones 1.0
import QtQuick

Item {
    id: root
    width: 320
    height: 320

    Rectangle {
        id: card
        x: 40; y: 40; width: 240; height: 240
        gradient: Gradient {
            GradientStop { position: 0.0; color: "red" }
            GradientStop { position: 1.0; color: "blue" }
        }
    }

    ShaderEffectSource {
        id: cardSnapshot
        sourceItem: card
        live: true
        hideSource: true
        width: card.width; height: card.height
        x: -1000000; y: -1000000
        visible: true
    }

    Repeater {
        id: stageRepeater
        model: gChain.length

        delegate: Item {
            id: stage
            required property int index
            readonly property var stageData: gChain[stage.index]
            readonly property bool isLast: stage.index === gChain.length - 1
            readonly property Item outputTap: tap
            anchors.fill: parent

            SurfaceShaderItem {
                id: stageItem
                x: card.x; y: card.y; width: card.width; height: card.height
                visible: true
                sourceItem: {
                    if (stage.index === 0)
                        return cardSnapshot;
                    var populated = stageRepeater.count;
                    var prev = populated > stage.index ? stageRepeater.itemAt(stage.index - 1) : null;
                    return prev ? prev.outputTap : null;
                }
                surfaceScale: 1
                surfaceFocused: true
                surfaceSize: Qt.size(card.width, card.height)
                surfaceFrameTopLeft: Qt.point(0, 0)
                surfaceFrameSize: Qt.size(card.width, card.height)
                paramPreamble: stage.stageData.preamble
                shaderParams: stage.stageData.params
                shaderSource: stage.stageData.source
                playing: false
            }

            ShaderEffectSource {
                id: tap
                sourceItem: stage.isLast ? null : stageItem
                live: true
                hideSource: !stage.isLast
                width: stageItem.width
                height: stageItem.height
                x: -1000000; y: -1000000
                visible: true
            }
        }
    }
}
)";

} // namespace

class TestSurfaceDecorationOrientation : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void testTwoStageChainRendersUprightOnOpenGL()
    {
        // GPU gate: probe a real GL context before any QQuickView exists — a
        // scene-graph init failure is fatal inside Qt, so this is the only
        // safe skip point for headless/software environments.
        {
            QOpenGLContext probe;
            if (!probe.create())
                QSKIP("No OpenGL context available in this environment");
            QOffscreenSurface surface;
            surface.setFormat(probe.format());
            surface.create();
            if (!surface.isValid() || !probe.makeCurrent(&surface))
                QSKIP("OpenGL context cannot be made current in this environment");
            probe.doneCurrent();
        }

        // The bundled packs, loaded from the source tree exactly as the
        // daemon loads them from the install tree.
        SurfaceShaderRegistry registry;
        registry.addSearchPath(QStringLiteral(PLASMAZONES_SOURCE_ROOT "/data/surface"),
                               PhosphorFsLoader::LiveReload::Off);
        QTRY_VERIFY_WITH_TIMEOUT(
            registry.hasEffect(QStringLiteral("border")) && registry.hasEffect(QStringLiteral("shadow")), 5000);

        const auto makeStage = [&registry](const QString& id, const QVariantMap& friendly) {
            const auto effect = registry.effect(id);
            QVariantMap stage;
            stage.insert(QStringLiteral("source"), QUrl::fromLocalFile(effect.fragmentShaderPath));
            stage.insert(QStringLiteral("preamble"), SurfaceShaderRegistry::paramPreamble(effect));
            stage.insert(QStringLiteral("params"), SurfaceShaderRegistry::translateSurfaceParams(effect, friendly));
            return stage;
        };
        QVariantMap borderParams;
        borderParams.insert(QStringLiteral("borderWidth"), 4);
        borderParams.insert(QStringLiteral("cornerRadius"), 24);
        QVariantMap shadowParams;
        shadowParams.insert(QStringLiteral("shadowSize"), 20);
        shadowParams.insert(QStringLiteral("offsetY"), 10);
        shadowParams.insert(QStringLiteral("cornerRadius"), 24);
        const QVariantList chain{makeStage(QStringLiteral("border"), borderParams),
                                 makeStage(QStringLiteral("shadow"), shadowParams)};

        // QQuickView loads from a URL only; stage the scene in a tmpdir.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString qmlPath = dir.filePath(QStringLiteral("scene.qml"));
        {
            QFile f(qmlPath);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(kSceneQml);
        }

        QQuickView view;
        view.rootContext()->setContextProperty(QStringLiteral("gChain"), chain);
        view.setColor(Qt::gray);
        view.setSource(QUrl::fromLocalFile(qmlPath));
        QVERIFY2(view.status() != QQuickView::Error, "scene.qml failed to load");
        view.resize(320, 320);
        view.show();
        if (!QTest::qWaitForWindowExposed(&view, 5000))
            QSKIP("Window never exposed (no compositor reachable)");

        // The whole point is the OpenGL path; if Qt fell back to another
        // backend despite QSG_RHI_BACKEND=opengl, the run proves nothing.
        // A never-materializing renderer interface is an environment gate
        // like the others on this init path, so it skips rather than fails.
        for (int waited = 0; view.rendererInterface() == nullptr && waited < 5000; waited += 100)
            QTest::qWait(100);
        if (view.rendererInterface() == nullptr)
            QSKIP("Scene graph renderer interface never materialized");
        if (view.rendererInterface()->graphicsApi() != QSGRendererInterface::OpenGLRhi)
            QSKIP("Scene graph is not running on the OpenGL RHI backend");

        // Channel-dominance beats exact colors — the border pack rounds
        // corners and the sample points sit well inside the card. Upright:
        // red on top, blue on the bottom. Flipped (the regression's exact
        // signature): blue on top, red on the bottom. A frame matching
        // NEITHER means the chain hasn't rendered — still settling, or an
        // environment whose shader pipeline never becomes ready and leaves
        // the placeholder gray in place (observed in CI containers, where a
        // display and GL context exist but shader compilation never
        // completes). Only the flipped signature is a verdict.
        const auto samples = [](const QImage& frame) {
            return std::pair{frame.pixelColor(frame.width() / 2, frame.height() / 4),
                             frame.pixelColor(frame.width() / 2, frame.height() * 3 / 4)};
        };
        const auto redDominant = [](const QColor& c) {
            return c.red() > 150 && c.blue() < 100;
        };
        const auto blueDominant = [](const QColor& c) {
            return c.blue() > 150 && c.red() < 100;
        };

        // Poll-grab until the async pack load and the two live capture folds
        // settle, instead of one grab after a magic fixed wait — a slow or
        // loaded GPU just takes more iterations. A genuinely flipped chain
        // (mutation-verified: reverting the per-target flip fix) presents the
        // flipped signature within a frame or two of the upright timing, so
        // the regression is caught as a hard failure, while a chain that
        // never renders at all skips as an environment gate.
        QImage frame;
        bool sawUpright = false;
        bool sawFlipped = false;
        QElapsedTimer settle;
        settle.start();
        while (settle.elapsed() < 5000) {
            QTest::qWait(150);
            frame = view.grabWindow();
            if (frame.isNull())
                continue;
            const auto [top, bottom] = samples(frame);
            if (redDominant(top) && blueDominant(bottom)) {
                sawUpright = true;
                break;
            }
            if (blueDominant(top) && redDominant(bottom)) {
                sawFlipped = true;
                break;
            }
        }
        if (frame.isNull())
            QSKIP("grabWindow returned no image in this environment");
        const auto [top, bottom] = samples(frame);
        QVERIFY2(!sawFlipped,
                 qPrintable(QStringLiteral("card rendered UPSIDE DOWN (top %1, bottom %2) — the per-target "
                                           "NDC Y-flip regressed")
                                .arg(top.name(), bottom.name())));
        if (!sawUpright)
            QSKIP(qPrintable(QStringLiteral("chain never rendered in either orientation (top %1, bottom %2) — "
                                            "shader pipeline unavailable in this environment")
                                 .arg(top.name(), bottom.name())));
    }
};

int main(int argc, char** argv)
{
    // Live-session runs are OPT-IN (see the header comment): mapping a real
    // window makes the installed KWin effect D-Bus-activate the installed
    // plasmazonesd on a developer machine, and CI containers pass every
    // display/GL gate yet never compile the shaders. Exit with the ctest
    // SKIP_RETURN_CODE (see the target's test property) unless the developer
    // explicitly asked for the live run.
    if (!qEnvironmentVariableIsSet("PLASMAZONES_GPU_TESTS")) {
        fprintf(stderr, "SKIP: live-session GPU test — set PLASMAZONES_GPU_TESTS=1 to run it\n");
        return 77;
    }
    // A GL scene graph needs a real display server: under the offscreen QPA
    // (which the shared ctest environment forces for isolation) the window
    // renders nothing and grabWindow returns a uniform placeholder — the
    // asserts would fail without testing anything. Exit with the ctest
    // SKIP_RETURN_CODE before QGuiApplication when headless; otherwise drop
    // the forced offscreen platform so the scene graph presents on the
    // session's compositor.
    // Assumption: a NON-empty display variable points at a live server — a
    // stale value would abort inside the QGuiApplication constructor (a QPA
    // connection failure is fatal, not catchable), which only a dead-socket
    // environment can trigger.
    if (qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY") && qEnvironmentVariableIsEmpty("DISPLAY")) {
        fprintf(stderr, "SKIP: no display server available — GPU-gated test not run\n");
        return 77;
    }
    qunsetenv("QT_QPA_PLATFORM");
    // Must precede QGuiApplication: the flip regression is OpenGL-specific
    // (Vulkan never flips), so the scene graph is pinned to GL for this test.
    qputenv("QSG_RHI_BACKEND", QByteArrayLiteral("opengl"));
    QGuiApplication app(argc, argv);
    // Same manual registration the daemon performs in main.cpp — the type
    // lives in plasmazones_rendering, no qt_add_qml_module target exists.
    qmlRegisterType<PlasmaZones::SurfaceShaderItem>("PlasmaZones", 1, 0, "SurfaceShaderItem");
    TestSurfaceDecorationOrientation tc;
    QTEST_SET_MAIN_SOURCE_PATH
    return QTest::qExec(&tc, argc, argv);
}

#include "test_surface_decoration_orientation.moc"
