// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Bakes every built-in pointer pack's KWIN-PATH variant — the
// `#define PLASMAZONES_KWIN` branch — through a real offscreen OpenGL context,
// the way the kwin-effect compiles it at runtime (pointerdecorationshader.cpp
// → generateCustomShader → glCompileShader).
//
// Why a SEPARATE bake from the validator's preview compile: the offline
// validator's QShaderBaker stage bakes the Qt-RHI (#else) branch, which is the
// settings PREVIEW, while every shipping pointer pack compiles through the
// compositor branch. The two dialects declare different identifiers (the
// preview UBO carries qt_Matrix, qt_Opacity and the rest of BaseUniforms), so
// a pack could bake clean for the preview and fail on the path that ships.
// The validator now also bakes the compositor dialect through glslang; this
// test compiles that same branch through the driver, closing the gap the
// animation family's twin closes for its packs.
//
// It assembles the variant the way the runtime does: entry scaffold → include
// expansion → param preamble → the KWin `#extension`/`#define` block. That
// final block comes from PhosphorShaders::kwinDefineBlock(), the same function
// the compositor's injector and the offline validator splice, so this gate
// cannot drift into accepting a dialect the compositor rejects.
//
// Three stages per pack: the main fragment (scaffolded, with the preamble),
// every buffer pass (no scaffold and NO preamble, the asymmetry the
// compositor applies at pointerdecorationshader.cpp: buffer sources address
// parameters by raw slot), and the compositor's own vertex stage from
// PointerShaderRegistry::compositorVertexSource(). The shared
// data/pointer/shared/pointer.vert is deliberately NOT baked: it is the
// preview's stage, reads qt_Matrix, and never compiles on the compositor.
//
// Skips cleanly when no desktop OpenGL >= 4.5 offscreen context is available
// (headless CI without a GL driver): a skip is not a pass, but it never blocks
// a machine that legitimately can't create the context. An EMPTY pack
// discovery is a hard failure, not a skip: the bundled tree always has packs,
// so finding none means the tree moved and the gate would otherwise pass
// while covering nothing.

#include <PhosphorPointer/PointerShaderEffect.h>
#include <PhosphorPointer/PointerShaderRegistry.h>
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

using PhosphorPointerShaders::PointerShaderEffect;
using PhosphorPointerShaders::PointerShaderRegistry;

class TestPointerShaderKwinBake : public QObject
{
    Q_OBJECT

    // The pack parsed WITH its source dir, so fromJson resolves and confines
    // every declared path the way the registry does at load.
    static PointerShaderEffect loadEffect(const QString& dir)
    {
        QFile f(dir + QStringLiteral("/metadata.json"));
        if (!f.open(QIODevice::ReadOnly)) {
            return {};
        }
        const QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
        return PointerShaderEffect::fromJson(obj, dir);
    }

    // The block ShaderInternal::injectKwinDefineAfterVersion splices in after
    // #version on the KWin path. Taken from PhosphorShaders rather than spelled
    // out here: the compositor's injector and the offline validator's
    // compositor bake splice the SAME function, so this test cannot drift into
    // accepting a dialect the compositor rejects.
    static QString kwinDefineBlock()
    {
        return PhosphorShaders::kwinDefineBlock();
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

    void testEveryPointerShaderBakesOnKwinPath_data()
    {
        QTest::addColumn<QString>("dir");
        const QString pointerDir = QStringLiteral(PLASMAZONES_SOURCE_DIR "/data/pointer");
        QDir dir(pointerDir);
        if (!dir.exists()) {
            QSKIP("data/pointer not found — running outside source tree");
        }
        bool any = false;
        for (const QString& sub : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            if (sub == QLatin1String("shared")) {
                continue;
            }
            const QString packDir = pointerDir + QLatin1Char('/') + sub;
            if (QFileInfo::exists(packDir + QStringLiteral("/effect.frag"))
                && QFileInfo::exists(packDir + QStringLiteral("/metadata.json"))) {
                QTest::newRow(qPrintable(sub)) << packDir;
                any = true;
            }
        }
        // A hard failure, not a skip: the tree exists, so an empty discovery
        // means the packs moved out from under this gate.
        QVERIFY2(any, "data/pointer exists but holds no pack (effect.frag + metadata.json)");
    }

    void testEveryPointerShaderBakesOnKwinPath()
    {
        if (!m_glReady) {
            QSKIP("no desktop OpenGL >= 4.5 offscreen context available — cannot compile the KWin variant");
        }
        QFETCH(QString, dir);
        const PointerShaderEffect eff = loadEffect(dir);
        QVERIFY2(eff.isValid(), qPrintable(QStringLiteral("failed to load effect: ") + dir));

        // The runtime's include roots for this pack, not a hardcoded shared/
        // dir: the registry's list is what both hosts resolve against.
        const QStringList includePaths = PointerShaderRegistry::includePathsFor(dir);

        // Main fragment: entry scaffold → include expansion → param preamble →
        // KWin define block after #version (each spliceAfterVersion lands
        // right below #version, so the define block ends up first, matching
        // the runtime's preamble-then-injectKwinDefine order).
        QFile frag(eff.fragmentShaderPath);
        QVERIFY2(frag.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(eff.fragmentShaderPath));
        const QString raw = QString::fromUtf8(frag.readAll());
        const QString assembled = PhosphorShaders::assembleEntryPoint(
            raw, PointerShaderRegistry::pointerEntryPrologue(), PointerShaderRegistry::pointerEntryCandidates());

        QString err;
        QString src = PhosphorShaders::ShaderIncludeResolver::expandIncludes(
            assembled, QFileInfo(eff.fragmentShaderPath).absolutePath(), includePaths, &err);
        QVERIFY2(!src.isEmpty(),
                 qPrintable(QStringLiteral("include expand failed: ") + dir + QStringLiteral(" — ") + err));

        src = PhosphorShaders::spliceAfterVersion(src, PointerShaderRegistry::paramPreamble(eff));
        src = PhosphorShaders::spliceAfterVersion(src, kwinDefineBlock());

        QString log;
        const bool ok = compileStage(GL_FRAGMENT_SHADER, src, &log);
        QVERIFY2(ok, qPrintable(QStringLiteral("KWin-path frag bake failed: ") + dir + QStringLiteral("\n") + log));

        // Buffer passes: include expansion and the define block only. The
        // compositor splices NO param preamble into a buffer pass, so neither
        // does this; splicing one would pass a buffer that fails live.
        for (const QString& bufPath : eff.bufferShaderPaths) {
            QFile buf(bufPath);
            QVERIFY2(buf.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(bufPath));
            const QString rawBuf = QString::fromUtf8(buf.readAll());
            QString bufErr;
            QString bsrc = PhosphorShaders::ShaderIncludeResolver::expandIncludes(
                rawBuf, QFileInfo(bufPath).absolutePath(), includePaths, &bufErr);
            QVERIFY2(!bsrc.isEmpty(),
                     qPrintable(QStringLiteral("buffer include expand failed: ") + bufPath + QStringLiteral(" — ")
                                + bufErr));
            bsrc = PhosphorShaders::spliceAfterVersion(bsrc, kwinDefineBlock());
            QString blog;
            const bool bok = compileStage(GL_FRAGMENT_SHADER, bsrc, &blog);
            QVERIFY2(
                bok,
                qPrintable(QStringLiteral("KWin-path buffer bake failed: ") + bufPath + QStringLiteral("\n") + blog));
        }

        // The compositor's vertex stage. TU-local to the effect until it was
        // hoisted onto the registry; baked here per pack so a pack that ships
        // its own vertexShader and one that takes the default both cover the
        // stage the compositor links against.
        {
            QString vsrc;
            if (!eff.vertexShaderPath.isEmpty()) {
                QFile vert(eff.vertexShaderPath);
                QVERIFY2(vert.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(eff.vertexShaderPath));
                QString vertErr;
                vsrc = PhosphorShaders::ShaderIncludeResolver::expandIncludes(
                    QString::fromUtf8(vert.readAll()), QFileInfo(eff.vertexShaderPath).absolutePath(), includePaths,
                    &vertErr);
                QVERIFY2(!vsrc.isEmpty(),
                         qPrintable(QStringLiteral("vertex include expand failed: ") + dir + QStringLiteral(" — ")
                                    + vertErr));
            } else {
                vsrc = PointerShaderRegistry::compositorVertexSource();
            }
            vsrc = PhosphorShaders::spliceAfterVersion(vsrc, kwinDefineBlock());
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

QTEST_MAIN(TestPointerShaderKwinBake)
#include "test_pointer_shader_kwin_bake.moc"
