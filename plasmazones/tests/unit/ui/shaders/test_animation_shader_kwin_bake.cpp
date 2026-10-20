// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Bakes every built-in animation pack's KWIN-PATH variant — the
// `#define PLASMAZONES_KWIN` branch — through a real offscreen OpenGL context,
// the way the kwin-effect compiles it at runtime (generateCustomShader →
// glCompileShader).
//
// Why a SEPARATE bake from test_animation_shader_preamble_bake: that test bakes
// the DAEMON variant via qsb (glslang → SPIR-V), whose Vulkan-GLSL front-end
// REJECTS the default-block uniforms and unbound samplers the KWin branch
// declares (that rejection is the whole reason animation_uniforms.glsl carries
// two branches). So the entire PLASMAZONES_KWIN region — surfaceColor's kwin
// Y-flip, desktop_transition.glsl's getFromColor / getToColor samplers, and any
// customColors[] / iFrame usage — went uncompiled by CI. A desktop pack's body
// lives ENTIRELY inside that branch, so a GLSL error there (a typo'd uniform, a
// customColors slot that isn't bound, a type mismatch) shipped undetected. This
// test compiles that branch through the driver, closing the gap.
//
// It assembles the variant the way the runtime does: entry scaffold → include
// expansion → param preamble → PhosphorShaders::kwinCompositorSource, the one
// function the compositor's injector, the offline validator and this test all
// build the compositor branch through (the KWin define block and the
// varying-location strip). The runtime's per-window path also splices KWin's
// colour-management block, which includes a KWin resource this test cannot
// resolve, so that block is the one step left out here. The validator splices
// a stand-in for it.
//
// SCOPE, so this is not read as more than it is. The bake compiles at the
// pack's own `#version 450` through a desktop driver, which proves the KWin
// branch is valid GLSL 450 on a real compiler. It does NOT reproduce the
// dialects KWin compiles in: desktop GLSL 140 on a 6.7 desktop context, and
// GLSL ES 300 on a GLES one. A varying location, for instance, is legal at 450
// and passes here. Those dialects are the shader_validate_* gates' job: their
// compositor arms compile and link every pack after KWin's own rewrites.
//
// It compiles both stages the runtime's KWin path touches — the fragment stage
// always, and, for packs that ship a custom `effect.vert`, the vertex stage too
// (a vertex-driven pack like wobble reads its `p_<id>` params in the VERT, whose
// PLASMAZONES_KWIN branch the daemon qsb bake never compiles either). Each pack
// is baked twice, with and without the top-down texcoord define KWin 6.8's
// per-window path adds, so both branches a pack keys off it compile.
//
// Skips when no desktop OpenGL >= 4.5 offscreen context is available.

#include <PhosphorAnimation/AnimationShaderRegistry.h>
#include <PhosphorShaders/ShaderEntryPoint.h>
#include <PhosphorShaders/ShaderIncludeResolver.h>
#include <PhosphorShaders/ShaderParamPreamble.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QSurfaceFormat>
#include <QTest>

using PhosphorAnimationShaders::AnimationShaderEffect;
using PhosphorAnimationShaders::AnimationShaderRegistry;

class TestAnimationShaderKwinBake : public QObject
{
    Q_OBJECT

    static AnimationShaderEffect loadEffect(const QString& dir)
    {
        QFile f(dir + QStringLiteral("/metadata.json"));
        if (!f.open(QIODevice::ReadOnly)) {
            return {};
        }
        const QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
        AnimationShaderEffect eff = AnimationShaderEffect::fromJson(obj);
        eff.sourceDir = dir;
        eff.fragmentShaderPath = dir + QStringLiteral("/effect.frag");
        const QString vertPath = dir + QStringLiteral("/effect.vert");
        if (QFileInfo::exists(vertPath)) {
            eff.vertexShaderPath = vertPath;
        }
        return eff;
    }

    QOffscreenSurface* m_surface = nullptr;
    QOpenGLContext* m_ctx = nullptr;
    bool m_glReady = false;

private Q_SLOTS:

    void initTestCase()
    {
        QSurfaceFormat fmt;
        fmt.setRenderableType(QSurfaceFormat::OpenGL);
        fmt.setProfile(QSurfaceFormat::CoreProfile);
        fmt.setVersion(4, 5);

        m_surface = new QOffscreenSurface;
        m_surface->setFormat(fmt);
        m_surface->create();
        if (!m_surface->isValid()) {
            return; // no windowing/GL surface — m_glReady stays false → tests QSKIP
        }

        m_ctx = new QOpenGLContext;
        m_ctx->setFormat(fmt);
        if (!m_ctx->create() || !m_ctx->makeCurrent(m_surface)) {
            return;
        }

        // The bundled packs declare `#version 450`, so the obtained context must
        // be desktop GL >= 4.5 to compile them as written. The dialects KWin
        // itself compiles in are the validator's to cover (see the header).
        const QPair<int, int> v = m_ctx->format().version();
        if (m_ctx->isOpenGLES() || v.first < 4 || (v.first == 4 && v.second < 5)) {
            m_ctx->doneCurrent();
            return;
        }
        m_glReady = true;
    }

    void cleanupTestCase()
    {
        if (m_ctx && m_ctx->isValid()) {
            m_ctx->doneCurrent();
        }
        delete m_ctx;
        m_ctx = nullptr;
        if (m_surface) {
            m_surface->destroy();
        }
        delete m_surface;
        m_surface = nullptr;
    }

    void testEveryAnimationShaderBakesOnKwinPath_data()
    {
        QTest::addColumn<QString>("dir");
        QTest::addColumn<bool>("topDownTexcoords");
        const QString animationsDir = QStringLiteral(PLASMAZONES_SOURCE_DIR "/data/animations");
        QDir dir(animationsDir);
        // Hard failures rather than QSKIP: a QSKIP in a _data function skips
        // the whole test and ctest still reports a pass, so a moved tree would
        // quietly drop every pack from the bake.
        QVERIFY2(dir.exists(), "data/animations not found");
        bool any = false;
        for (const QString& sub : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            if (sub == QLatin1String("shared")) {
                continue;
            }
            const QString packDir = animationsDir + QLatin1Char('/') + sub;
            if (QFileInfo::exists(packDir + QStringLiteral("/effect.frag"))
                && QFileInfo::exists(packDir + QStringLiteral("/metadata.json"))) {
                QTest::newRow(qPrintable(sub + QStringLiteral("-texcoords-bottomup"))) << packDir << false;
                QTest::newRow(qPrintable(sub + QStringLiteral("-texcoords-topdown"))) << packDir << true;
                any = true;
            }
        }
        QVERIFY2(any, "no animation shaders found");
    }

    void testEveryAnimationShaderBakesOnKwinPath()
    {
        if (!m_glReady) {
            QSKIP("no desktop OpenGL >= 4.5 offscreen context available — cannot compile the KWin variant");
        }
        // Current per slot rather than inherited from initTestCase, so the
        // slot does not depend on what ran before it.
        QVERIFY(m_ctx->makeCurrent(m_surface));
        QFETCH(QString, dir);
        QFETCH(bool, topDownTexcoords);
        const AnimationShaderEffect eff = loadEffect(dir);
        QVERIFY2(eff.isValid(), qPrintable(QStringLiteral("failed to load effect: ") + dir));
        const QString extraDefines = topDownTexcoords ? PhosphorShaders::kwinTopDownTexcoordsDefine() : QString();

        // Assemble the KWin variant exactly as the runtime does: entry scaffold
        // → include expansion → param preamble → kwinCompositorSource (the
        // define block after #version, then the varying-location strip).
        QFile frag(eff.fragmentShaderPath);
        QVERIFY2(frag.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(eff.fragmentShaderPath));
        const QString raw = QString::fromUtf8(frag.readAll());
        const QString assembled =
            PhosphorShaders::assembleEntryPoint(raw, AnimationShaderRegistry::animationEntryPrologue(),
                                                AnimationShaderRegistry::animationEntryCandidates());

        const QStringList includePaths = {QStringLiteral(PLASMAZONES_SOURCE_DIR "/data/animations/shared")};
        QString err;
        QString src = PhosphorShaders::ShaderIncludeResolver::expandIncludes(
            assembled, QFileInfo(eff.fragmentShaderPath).absolutePath(), includePaths, &err);
        QVERIFY2(!src.isEmpty(),
                 qPrintable(QStringLiteral("include expand failed: ") + dir + QStringLiteral(" — ") + err));

        src = PhosphorShaders::spliceAfterVersion(src, AnimationShaderRegistry::paramPreamble(eff));
        src = PhosphorShaders::kwinCompositorSource(src, PhosphorShaders::ShaderStage::Fragment, extraDefines);

        QString log;
        const bool ok = compileStage(GL_FRAGMENT_SHADER, src, &log);
        QVERIFY2(ok, qPrintable(QStringLiteral("KWin-path frag bake failed: ") + dir + QStringLiteral("\n") + log));

        // Packs that ship a custom effect.vert have a PLASMAZONES_KWIN vertex
        // branch the daemon qsb bake never compiles either — assemble and compile
        // it the same way the runtime custom-vertex path does (include expansion
        // → param preamble → kwinCompositorSource), so a vertex-side KWin GLSL
        // error can't ship undetected.
        if (!eff.vertexShaderPath.isEmpty()) {
            QFile vert(eff.vertexShaderPath);
            QVERIFY2(vert.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(eff.vertexShaderPath));
            const QString rawVert = QString::fromUtf8(vert.readAll());
            QString vertErr;
            QString vsrc = PhosphorShaders::ShaderIncludeResolver::expandIncludes(
                rawVert, QFileInfo(eff.vertexShaderPath).absolutePath(), includePaths, &vertErr);
            QVERIFY2(
                !vsrc.isEmpty(),
                qPrintable(QStringLiteral("vertex include expand failed: ") + dir + QStringLiteral(" — ") + vertErr));
            vsrc = PhosphorShaders::spliceAfterVersion(vsrc, AnimationShaderRegistry::paramPreamble(eff));
            vsrc = PhosphorShaders::kwinCompositorSource(vsrc, PhosphorShaders::ShaderStage::Vertex, extraDefines);
            QString vlog;
            const bool vok = compileStage(GL_VERTEX_SHADER, vsrc, &vlog);
            QVERIFY2(vok,
                     qPrintable(QStringLiteral("KWin-path vert bake failed: ") + dir + QStringLiteral("\n") + vlog));
        }
    }

private:
    // Compile @p source as a @p stageType (GL_FRAGMENT_SHADER / GL_VERTEX_SHADER)
    // shader in the current context. Returns the GL_COMPILE_STATUS; on failure
    // @p outLog carries the driver info log.
    bool compileStage(GLenum stageType, const QString& source, QString* outLog)
    {
        QOpenGLFunctions* f = m_ctx->functions();
        const GLuint sh = f->glCreateShader(stageType);
        const QByteArray bytes = source.toUtf8();
        const char* srcPtr = bytes.constData();
        const GLint srcLen = bytes.size();
        f->glShaderSource(sh, 1, &srcPtr, &srcLen);
        f->glCompileShader(sh);

        GLint status = 0;
        f->glGetShaderiv(sh, GL_COMPILE_STATUS, &status);
        if (!status && outLog) {
            GLint logLen = 0;
            f->glGetShaderiv(sh, GL_INFO_LOG_LENGTH, &logLen);
            if (logLen > 0) {
                QByteArray buf(logLen, '\0');
                f->glGetShaderInfoLog(sh, logLen, nullptr, buf.data());
                *outLog = QString::fromUtf8(buf).trimmed();
            }
        }
        f->glDeleteShader(sh);
        return status != 0;
    }
};

QTEST_MAIN(TestAnimationShaderKwinBake)
#include "test_animation_shader_kwin_bake.moc"
