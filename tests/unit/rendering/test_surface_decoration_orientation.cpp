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
 * GPU-gated: OpenGL context creation is probed BEFORE any QQuickView exists
 * (a failed scene-graph init is fatal inside Qt, not catchable). Headless
 * environments exit with the ctest SKIP_RETURN_CODE before QGuiApplication;
 * in-test gates (no GL context, wrong backend, no grab) QSKIP, which qExec
 * reports as a green run — either way CI without a GPU stays green without
 * pretending to cover this.
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

        // Upright gradient: red dominates the top of the card, blue the
        // bottom. Channel-dominance beats exact colors — the border pack
        // rounds corners and the sample points sit well inside the card.
        const auto upright = [](const QImage& frame) {
            if (frame.isNull())
                return false;
            const QColor top = frame.pixelColor(frame.width() / 2, frame.height() / 4);
            const QColor bottom = frame.pixelColor(frame.width() / 2, frame.height() * 3 / 4);
            return top.red() > 150 && top.blue() < 100 && bottom.blue() > 150 && bottom.red() < 100;
        };

        // Poll-grab until the async pack load and the two live capture folds
        // settle, instead of one grab after a magic fixed wait — a slow or
        // loaded GPU just takes more iterations. A genuinely flipped chain
        // never satisfies the predicate, so the regression still times out
        // into the failure assert below carrying the last frame's samples.
        QImage frame;
        bool sawUpright = false;
        QElapsedTimer settle;
        settle.start();
        while (settle.elapsed() < 5000) {
            QTest::qWait(150);
            frame = view.grabWindow();
            if (upright(frame)) {
                sawUpright = true;
                break;
            }
        }
        if (frame.isNull())
            QSKIP("grabWindow returned no image in this environment");
        const QColor top = frame.pixelColor(frame.width() / 2, frame.height() / 4);
        const QColor bottom = frame.pixelColor(frame.width() / 2, frame.height() * 3 / 4);
        QVERIFY2(sawUpright,
                 qPrintable(QStringLiteral("card never rendered upright (top %1, bottom %2) — a flipped chain "
                                           "renders blue on top")
                                .arg(top.name(), bottom.name())));
    }
};

int main(int argc, char** argv)
{
    // A GL scene graph needs a real display server: under the offscreen QPA
    // (which the shared ctest environment forces for isolation) the window
    // renders nothing and grabWindow returns a uniform placeholder — the
    // asserts would fail without testing anything. Exit with the ctest
    // SKIP_RETURN_CODE (see the target's test property) before
    // QGuiApplication when headless; otherwise drop the forced offscreen
    // platform so the scene graph presents on the session's compositor.
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
