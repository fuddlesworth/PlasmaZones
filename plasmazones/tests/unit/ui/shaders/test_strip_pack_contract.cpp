// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Draws every bundled strip pack through the KWin-path variant the strip
// pass links at runtime (StripTransitionManager: entry scaffold → include
// expansion → param preamble → the compositor source built by
// PhosphorShaders::kwinCompositorSource, plus the pass's own quad vertex
// stage) into a small framebuffer, with a synthetic uStrip / uBelow pair
// shaped exactly like the pass's capture: one half an opaque red column, the
// other a gap whose texels hold the below-strip colour with alpha 0 (what
// snapshotBelowCapture's alpha zero leaves behind), and uBelow the solid
// below-strip colour. The fixture is drawn once with a horizontal strip and
// once transposed with a vertical one, so the contracts below hold on both
// axes. They do not decide whether a pack displaces ALONG the axis at all: a
// pack that hardcodes vec2(amount, 0) leaves a vertical fixture looking
// undisturbed and passes, which is a property of what is drawn, not of the
// contracts these legs pin.
//
// test_animation_shader_kwin_bake only compiles, never draws, so a pack that
// links and then paints the wrong thing (a channel taken from a neighbouring
// tap with the wrong coverage, a forced alpha, a re-composite that never ran)
// shipped undetected. Three contracts are pinned here:
//
//   IDENTITY. At zero motion the pass must reproduce the capture: every
//   output texel equals the strip layer composited over the below content,
//   which for this fixture is the capture texel itself.
//
//   GAP. Under motion either way along the axis, a gap texel beyond every
//   pack's reach must show the below content alone: a pack cannot displace
//   the wallpaper.
//
//   EDGE. Under motion, the first gap texel beside the column may carry a
//   fringe, a smear or a glow, but never a hole: its channel sum must stay
//   above 0.4 of the below content's. A pack may legitimately take ONE
//   whole channel out of that texel (chromatic aberration pulls the
//   column's blue, which is zero, across the edge), so the fixture's below
//   colour keeps every channel under 0.45 of the sum and the threshold sits
//   below the worst single-channel loss with a margin. A pack that takes
//   red or blue from a tap inside the column and alpha from the union of
//   its taps, without compensating the channels it did not take,
//   composites near-black there and fails by a wide margin.
//
// Skips cleanly when no desktop OpenGL >= 4.5 offscreen context exists.

#include <PhosphorAnimation/AnimationShaderContract.h>
#include <PhosphorAnimation/AnimationShaderRegistry.h>
#include <PhosphorAnimation/ProfilePaths.h>
#include <PhosphorShaders/ShaderEntryPoint.h>
#include <PhosphorShaders/ShaderIncludeResolver.h>
#include <PhosphorShaders/ShaderParamPreamble.h>

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMatrix4x4>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QSurfaceFormat>
#include <QTest>
#include <QVector4D>

#include <array>
#include <vector>

using PhosphorAnimationShaders::AnimationShaderEffect;
using PhosphorAnimationShaders::AnimationShaderRegistry;
namespace ASC = PhosphorAnimationShaders::AnimationShaderContract;

namespace {

constexpr int kSize = 64;
// The below-strip colour, premultiplied opaque. No channel reaches 0.45 of
// the channel sum (160/360 = 0.44), see the EDGE contract above.
constexpr std::array<unsigned char, 4> kBelow = {80, 120, 160, 255};
// The column: opaque red on the first half along the strip's axis.
constexpr std::array<unsigned char, 4> kColumn = {255, 0, 0, 255};
constexpr int kColumnWidth = kSize / 2;

struct Texel
{
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
};

} // namespace

class TestStripPackContract : public QObject
{
    Q_OBJECT

    static AnimationShaderEffect loadEffect(const QString& dir)
    {
        QFile f(dir + QStringLiteral("/metadata.json"));
        if (!f.open(QIODevice::ReadOnly)) {
            return {};
        }
        AnimationShaderEffect eff = AnimationShaderEffect::fromJson(QJsonDocument::fromJson(f.readAll()).object());
        eff.sourceDir = dir;
        eff.fragmentShaderPath = dir + QStringLiteral("/effect.frag");
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
            return;
        }
        m_ctx = new QOpenGLContext;
        m_ctx->setFormat(fmt);
        if (!m_ctx->create() || !m_ctx->makeCurrent(m_surface)) {
            return;
        }
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

    void everyStripPackHonoursTheCaptureContract_data()
    {
        QTest::addColumn<QString>("dir");
        QTest::addColumn<bool>("vertical");
        const QString animationsDir = QStringLiteral(PLASMAZONES_SOURCE_DIR "/data/animations");
        QDir dir(animationsDir);
        // Hard failures, as in the pointer twin: a QSKIP here skips the whole
        // function and ctest still reports a pass, so a renamed strip token or
        // a moved tree would quietly drop every strip pack from the gate.
        QVERIFY2(dir.exists(), "data/animations not found");
        bool any = false;
        for (const QString& sub : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            const QString packDir = animationsDir + QLatin1Char('/') + sub;
            if (!QFileInfo::exists(packDir + QStringLiteral("/effect.frag"))) {
                continue;
            }
            const AnimationShaderEffect eff = loadEffect(packDir);
            if (eff.isValid() && eff.appliesTo.contains(PhosphorAnimation::ProfilePaths::EventClassStrip)) {
                QTest::newRow(qPrintable(sub + QStringLiteral("-horizontal"))) << packDir << false;
                QTest::newRow(qPrintable(sub + QStringLiteral("-vertical"))) << packDir << true;
                any = true;
            }
        }
        QVERIFY2(any, "no strip packs found");
    }

    void everyStripPackHonoursTheCaptureContract()
    {
        if (!m_glReady) {
            QSKIP("no desktop OpenGL >= 4.5 offscreen context available — cannot draw the KWin variant");
        }
        QFETCH(QString, dir);
        QFETCH(bool, vertical);
        const AnimationShaderEffect eff = loadEffect(dir);
        QVERIFY2(eff.isValid(), qPrintable(dir));
        // The strip pass abandons a pack that ships its own main() before
        // compiling it, so drawing one here would pin a contract the runtime
        // never runs.
        {
            QFile frag(eff.fragmentShaderPath);
            QVERIFY(frag.open(QIODevice::ReadOnly | QIODevice::Text));
            QVERIFY2(!PhosphorShaders::definesMain(QString::fromUtf8(frag.readAll())),
                     qPrintable(dir + QStringLiteral(": a strip pack defines main(), which the strip pass refuses")));
        }

        QOpenGLExtraFunctions* f = m_ctx->extraFunctions();
        const GLuint program = linkPack(eff);
        QVERIFY2(program != 0, qPrintable(QStringLiteral("link failed: ") + dir));

        const GLuint stripTex = uploadFixture(/*strip=*/true, vertical);
        const GLuint belowTex = uploadFixture(/*strip=*/false, vertical);
        GLuint fboTex = 0;
        GLuint fbo = 0;
        f->glGenTextures(1, &fboTex);
        f->glBindTexture(GL_TEXTURE_2D, fboTex);
        f->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kSize, kSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        f->glGenFramebuffers(1, &fbo);
        f->glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        f->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fboTex, 0);
        QCOMPARE(f->glCheckFramebufferStatus(GL_FRAMEBUFFER), GLenum(GL_FRAMEBUFFER_COMPLETE));
        f->glViewport(0, 0, kSize, kSize);
        f->glDisable(GL_BLEND);

        const Quad quad = quadVao();
        f->glUseProgram(program);
        bindPassUniforms(program, eff, stripTex, belowTex, vertical);

        // Framebuffer row r samples capture row r: the quad's top-down uv is
        // flipped back to the capture's bottom-up origin by stripSampleUv, so
        // every comparison below is in the capture's own texel space.
        // `along` is the coordinate on the strip's axis, `across` the other.
        const auto at = [vertical](const std::vector<Texel>& frame, int along, int across) {
            const int x = vertical ? across : along;
            const int y = vertical ? along : across;
            return frame[static_cast<size_t>(y * kSize + x)];
        };
        const auto describe = [](const Texel& t) {
            return QStringLiteral("(%1, %2, %3)").arg(t.r).arg(t.g).arg(t.b);
        };

        // IDENTITY: zero motion reproduces the capture texel by texel, over
        // the whole framebuffer.
        setMotion(program, 0.0f);
        const std::vector<Texel> still = draw(quad);
        for (int along = 0; along < kSize; ++along) {
            const Texel expected = along < kColumnWidth ? texel(kColumn) : texel(kBelow);
            for (int across = 0; across < kSize; ++across) {
                const Texel got = at(still, along, across);
                QVERIFY2(close(got, expected),
                         qPrintable(QStringLiteral("%1: identity broken at along=%2 across=%3: got %4 expected %5")
                                        .arg(dir, QString::number(along), QString::number(across), describe(got),
                                             describe(expected))));
            }
        }

        // GAP and EDGE under a hard fling either way along the axis (five
        // output extents per second, well past every bundled pack's
        // saturation).
        const Texel below = texel(kBelow);
        const float belowSum = below.r + below.g + below.b;
        for (const float velocity : {5.0f, -5.0f}) {
            setMotion(program, velocity);
            const std::vector<Texel> moving = draw(quad);
            for (int across = 0; across < kSize; ++across) {
                const Texel far = at(moving, kSize - 1, across);
                QVERIFY2(close(far, below),
                         qPrintable(QStringLiteral("%1: the far gap texel moved at velocity %2, across=%3: got %4 "
                                                   "expected below %5")
                                        .arg(dir, QString::number(velocity), QString::number(across), describe(far),
                                             describe(below))));
                const Texel edge = at(moving, kColumnWidth, across);
                QVERIFY2(edge.r + edge.g + edge.b >= 0.4f * belowSum,
                         qPrintable(QStringLiteral("%1: the gap texel beside the column went dark at velocity %2, "
                                                   "across=%3: %4 under below %5")
                                        .arg(dir, QString::number(velocity), QString::number(across), describe(edge),
                                             describe(below))));
            }
        }

        f->glBindFramebuffer(GL_FRAMEBUFFER, 0);
        f->glDeleteFramebuffers(1, &fbo);
        f->glDeleteTextures(1, &fboTex);
        f->glDeleteTextures(1, &stripTex);
        f->glDeleteTextures(1, &belowTex);
        f->glDeleteVertexArrays(1, &quad.vao);
        f->glDeleteBuffers(1, &quad.vbo);
        f->glDeleteProgram(program);
    }

private:
    static Texel texel(const std::array<unsigned char, 4>& px)
    {
        return {px[0] / 255.0f, px[1] / 255.0f, px[2] / 255.0f};
    }

    static bool close(const Texel& a, const Texel& b)
    {
        // Two 8-bit steps: one for the capture's quantisation, one for the
        // readback's.
        constexpr float kTol = 2.5f / 255.0f;
        return qAbs(a.r - b.r) <= kTol && qAbs(a.g - b.g) <= kTol && qAbs(a.b - b.b) <= kTol;
    }

    /// Assemble the fragment stage exactly as striptransitionshader.cpp does
    /// and link it against the pass's quad vertex stage.
    GLuint linkPack(const AnimationShaderEffect& eff)
    {
        QFile frag(eff.fragmentShaderPath);
        if (!frag.open(QIODevice::ReadOnly | QIODevice::Text)) {
            return 0;
        }
        const QString raw = QString::fromUtf8(frag.readAll());
        const QString assembled =
            PhosphorShaders::assembleEntryPoint(raw, AnimationShaderRegistry::animationEntryPrologue(),
                                                AnimationShaderRegistry::animationEntryCandidates());
        const QStringList includePaths = {QStringLiteral(PLASMAZONES_SOURCE_DIR "/data/animations/shared")};
        QString err;
        QString src = PhosphorShaders::ShaderIncludeResolver::expandIncludes(
            assembled, QFileInfo(eff.fragmentShaderPath).absolutePath(), includePaths, &err);
        if (src.isEmpty()) {
            qWarning("include expand failed: %s", qPrintable(err));
            return 0;
        }
        src = PhosphorShaders::spliceAfterVersion(src, AnimationShaderRegistry::paramPreamble(eff));
        src = PhosphorShaders::kwinCompositorSource(src, PhosphorShaders::ShaderStage::Fragment);
        const QString vert = PhosphorShaders::kwinCompositorSource(
            AnimationShaderRegistry::compositorScreenVertexSource(), PhosphorShaders::ShaderStage::Vertex);

        QOpenGLExtraFunctions* f = m_ctx->extraFunctions();
        const GLuint vs = compileStage(GL_VERTEX_SHADER, vert);
        const GLuint fs = compileStage(GL_FRAGMENT_SHADER, src);
        if (vs == 0 || fs == 0) {
            // compileStage deletes only the stage that failed.
            if (vs != 0) {
                f->glDeleteShader(vs);
            }
            if (fs != 0) {
                f->glDeleteShader(fs);
            }
            return 0;
        }
        const GLuint program = f->glCreateProgram();
        f->glAttachShader(program, vs);
        f->glAttachShader(program, fs);
        f->glLinkProgram(program);
        f->glDeleteShader(vs);
        f->glDeleteShader(fs);
        GLint status = 0;
        f->glGetProgramiv(program, GL_LINK_STATUS, &status);
        if (!status) {
            GLint logLen = 0;
            f->glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLen);
            QByteArray buf(logLen > 0 ? logLen : 1, '\0');
            f->glGetProgramInfoLog(program, logLen, nullptr, buf.data());
            qWarning("link log: %s", buf.constData());
            f->glDeleteProgram(program);
            return 0;
        }
        return program;
    }

    GLuint compileStage(GLenum stageType, const QString& source)
    {
        QOpenGLExtraFunctions* f = m_ctx->extraFunctions();
        const GLuint sh = f->glCreateShader(stageType);
        const QByteArray bytes = source.toUtf8();
        const char* srcPtr = bytes.constData();
        const GLint srcLen = bytes.size();
        f->glShaderSource(sh, 1, &srcPtr, &srcLen);
        f->glCompileShader(sh);
        GLint status = 0;
        f->glGetShaderiv(sh, GL_COMPILE_STATUS, &status);
        if (!status) {
            GLint logLen = 0;
            f->glGetShaderiv(sh, GL_INFO_LOG_LENGTH, &logLen);
            QByteArray buf(logLen > 0 ? logLen : 1, '\0');
            f->glGetShaderInfoLog(sh, logLen, nullptr, buf.data());
            qWarning("compile log: %s", buf.constData());
            f->glDeleteShader(sh);
            return 0;
        }
        return sh;
    }

    /// The capture pair. The column edge runs across the strip's axis, along
    /// x for a horizontal strip and along the capture's rows for a vertical
    /// one, and both are constant across it.
    GLuint uploadFixture(bool strip, bool vertical)
    {
        std::vector<unsigned char> px(static_cast<size_t>(kSize * kSize * 4));
        for (int y = 0; y < kSize; ++y) {
            for (int x = 0; x < kSize; ++x) {
                std::array<unsigned char, 4> value = kBelow;
                if (strip) {
                    // Column: the window painted with full coverage. Gap:
                    // the below colour with the alpha zeroed by the snapshot.
                    const int along = vertical ? y : x;
                    value = along < kColumnWidth ? kColumn
                                                 : std::array<unsigned char, 4>{kBelow[0], kBelow[1], kBelow[2], 0};
                }
                const size_t at = static_cast<size_t>((y * kSize + x) * 4);
                for (size_t c = 0; c < 4; ++c) {
                    px[at + c] = value[c];
                }
            }
        }
        QOpenGLExtraFunctions* f = m_ctx->extraFunctions();
        GLuint tex = 0;
        f->glGenTextures(1, &tex);
        f->glBindTexture(GL_TEXTURE_2D, tex);
        f->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kSize, kSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        // The pass's allocateOutputTexture settings: LINEAR, CLAMP_TO_EDGE.
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        return tex;
    }

    struct Quad
    {
        GLuint vao = 0;
        GLuint vbo = 0;
    };

    /// The same quad drawOutputQuad emits, in device coordinates, with the
    /// texcoords pinned to the screen corners (uv.y == 0 at the top).
    Quad quadVao()
    {
        QOpenGLExtraFunctions* f = m_ctx->extraFunctions();
        const float w = float(kSize);
        const float verts[] = {
            0.0f, w,    0.0f, 1.0f, // bottom-left
            w,    w,    1.0f, 1.0f, // bottom-right
            0.0f, 0.0f, 0.0f, 0.0f, // top-left
            w,    0.0f, 1.0f, 0.0f, // top-right
        };
        GLuint vao = 0;
        GLuint vbo = 0;
        f->glGenVertexArrays(1, &vao);
        f->glBindVertexArray(vao);
        f->glGenBuffers(1, &vbo);
        f->glBindBuffer(GL_ARRAY_BUFFER, vbo);
        f->glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
        f->glEnableVertexAttribArray(0);
        f->glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
        f->glEnableVertexAttribArray(1);
        f->glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                                 reinterpret_cast<const void*>(2 * sizeof(float)));
        return {vao, vbo};
    }

    /// Everything StripTransitionManager::paintOutput uploads, with the
    /// pack's metadata defaults in the param pools and no work-area rect.
    void bindPassUniforms(GLuint program, const AnimationShaderEffect& eff, GLuint stripTex, GLuint belowTex,
                          bool vertical)
    {
        QOpenGLExtraFunctions* f = m_ctx->extraFunctions();
        const auto loc = [&](const char* name) {
            return f->glGetUniformLocation(program, name);
        };
        QMatrix4x4 mvp;
        mvp.ortho(0.0f, float(kSize), float(kSize), 0.0f, -1.0f, 1.0f);
        f->glUniformMatrix4fv(loc("modelViewProjectionMatrix"), 1, GL_FALSE, mvp.constData());
        f->glUniform1i(loc("uStrip"), 0);
        f->glUniform1i(loc("uBelow"), 1);
        f->glActiveTexture(GL_TEXTURE1);
        f->glBindTexture(GL_TEXTURE_2D, belowTex);
        f->glActiveTexture(GL_TEXTURE0);
        f->glBindTexture(GL_TEXTURE_2D, stripTex);
        f->glUniform1f(loc("iTime"), 0.0f);
        f->glUniform2f(loc("iResolution"), float(kSize), float(kSize));
        f->glUniform1i(loc("iFrame"), 0);
        f->glUniform2f(loc("iStripAxis"), vertical ? 0.0f : 1.0f, vertical ? 1.0f : 0.0f);
        f->glUniform4f(loc("iStripRect"), 0.0f, 0.0f, 0.0f, 0.0f);

        const QVariantMap translated = AnimationShaderRegistry::translateAnimationParams(eff, QVariantMap());
        for (int slot = 0; slot < ASC::kMaxCustomParams; ++slot) {
            const auto pull = [&](char comp) -> float {
                const auto it = translated.constFind(ASC::slotKey(slot, comp));
                if (it == translated.constEnd()) {
                    return 0.0f;
                }
                bool ok = false;
                const float v = it->toFloat(&ok);
                return ok ? v : 0.0f;
            };
            const QByteArray name = QByteArrayLiteral("customParams[") + QByteArray::number(slot) + ']';
            f->glUniform4f(loc(name.constData()), pull('x'), pull('y'), pull('z'), pull('w'));
        }
        for (int slot = 0; slot < ASC::kMaxCustomColors; ++slot) {
            QVector4D color;
            const auto it = translated.constFind(ASC::colorKey(slot));
            if (it != translated.constEnd()) {
                const QColor c = it->value<QColor>();
                if (c.isValid()) {
                    color = QVector4D(c.redF(), c.greenF(), c.blueF(), c.alphaF());
                }
            }
            const QByteArray name = QByteArrayLiteral("customColors[") + QByteArray::number(slot) + ']';
            f->glUniform4f(loc(name.constData()), color.x(), color.y(), color.z(), color.w());
        }
    }

    /// iStripMotion for a velocity of @p extentsPerSecond along the axis
    /// (.y device px/s, .w extents/s; the offset lanes zero, as the bundled
    /// packs key off .w alone).
    void setMotion(GLuint program, float extentsPerSecond)
    {
        QOpenGLExtraFunctions* f = m_ctx->extraFunctions();
        f->glUniform4f(f->glGetUniformLocation(program, "iStripMotion"), 0.0f, extentsPerSecond * float(kSize), 0.0f,
                       extentsPerSecond);
    }

    /// Draw the quad and read back the whole framebuffer, indexed
    /// `row * kSize + x` with row 0 at GL's bottom.
    std::vector<Texel> draw(const Quad& quad)
    {
        QOpenGLExtraFunctions* f = m_ctx->extraFunctions();
        f->glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        f->glClear(GL_COLOR_BUFFER_BIT);
        f->glBindVertexArray(quad.vao);
        f->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        f->glFinish();
        std::vector<unsigned char> pixels(static_cast<size_t>(kSize * kSize * 4));
        f->glReadPixels(0, 0, kSize, kSize, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        std::vector<Texel> out(static_cast<size_t>(kSize * kSize));
        for (size_t i = 0; i < out.size(); ++i) {
            const size_t at = i * 4;
            out[i] = {pixels[at] / 255.0f, pixels[at + 1] / 255.0f, pixels[at + 2] / 255.0f};
        }
        return out;
    }
};

QTEST_MAIN(TestStripPackContract)
#include "test_strip_pack_contract.moc"
