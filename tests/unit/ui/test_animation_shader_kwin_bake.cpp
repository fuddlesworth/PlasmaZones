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
// It assembles the variant exactly as the runtime does: entry scaffold → include
// expansion → param preamble → the KWin `#extension`/`#define` block that
// ShaderInternal::injectKwinDefineAfterVersion splices in. It compiles the
// fragment stage only (no link) — enough to surface every compile-time error in
// the branch without needing the matching vertex attributes.
//
// Skips cleanly when no desktop OpenGL >= 4.5 offscreen context is available
// (headless CI without a GL driver): a skip is not a pass, but it never blocks
// a machine that legitimately can't create the context. On a real Plasma /
// Wayland session (where the effect actually runs) the context is available and
// the bake executes.

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
        return eff;
    }

    // The exact header ShaderInternal::injectKwinDefineAfterVersion splices in
    // after #version on the KWin path (kwin-effect/plasmazoneseffect/
    // shader_transitions.cpp is the source of truth). The two ARB `: enable`
    // directives are no-ops on the fragment stage but must match so this bake
    // stays byte-faithful to the runtime assembly.
    static QString kwinDefineBlock()
    {
        return QStringLiteral(
            "#extension GL_ARB_explicit_attrib_location : enable\n"
            "#extension GL_ARB_separate_shader_objects : enable\n"
            "#define PLASMAZONES_KWIN\n");
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
        // be desktop GL >= 4.5. A GLES or lower-core context can't compile them
        // (and KWin wouldn't be running on it either) — skip rather than emit a
        // misleading failure.
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
        const QString animationsDir = QStringLiteral(PLASMAZONES_SOURCE_DIR "/data/animations");
        QDir dir(animationsDir);
        if (!dir.exists()) {
            QSKIP("data/animations not found — running outside source tree");
        }
        bool any = false;
        for (const QString& sub : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            if (sub == QLatin1String("shared")) {
                continue;
            }
            const QString packDir = animationsDir + QLatin1Char('/') + sub;
            if (QFileInfo::exists(packDir + QStringLiteral("/effect.frag"))
                && QFileInfo::exists(packDir + QStringLiteral("/metadata.json"))) {
                QTest::newRow(qPrintable(sub)) << packDir;
                any = true;
            }
        }
        if (!any) {
            QSKIP("no animation shaders found");
        }
    }

    void testEveryAnimationShaderBakesOnKwinPath()
    {
        if (!m_glReady) {
            QSKIP("no desktop OpenGL >= 4.5 offscreen context available — cannot compile the KWin variant");
        }
        QFETCH(QString, dir);
        const AnimationShaderEffect eff = loadEffect(dir);
        QVERIFY2(eff.isValid(), qPrintable(QStringLiteral("failed to load effect: ") + dir));

        // Assemble the KWin variant exactly as the runtime does: entry scaffold
        // → include expansion → param preamble → KWin define block after
        // #version (each spliceAfterVersion lands right below #version, so the
        // define block ends up first, matching the runtime's preamble-then-
        // injectKwinDefine order).
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
        src = PhosphorShaders::spliceAfterVersion(src, kwinDefineBlock());

        QString log;
        const bool ok = compileFragment(src, &log);
        QVERIFY2(ok, qPrintable(QStringLiteral("KWin-path bake failed: ") + dir + QStringLiteral("\n") + log));
    }

private:
    // Compile @p source as a fragment shader in the current context. Returns the
    // GL_COMPILE_STATUS; on failure @p outLog carries the driver info log.
    bool compileFragment(const QString& source, QString* outLog)
    {
        QOpenGLFunctions* f = m_ctx->functions();
        const GLuint sh = f->glCreateShader(GL_FRAGMENT_SHADER);
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
