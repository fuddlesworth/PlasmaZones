// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The offline animation-pack validator's metadata lints. The bundled-pack CI
// gate (shader_validate_animations) only proves the shipped packs are clean —
// it cannot show that a BROKEN pack is actually caught, which is how an
// appliesTo token that the parser accepted and the lint rejected shipped
// undetected. These tests build deliberately-broken packs in a temp dir and
// assert the diagnostic.
//
// Mostly metadata lints, plus the multipass buffer-pass bake. That bake has
// no other coverage at all: no bundled animation pack is multipass, so the
// CI gate walks straight past it and the stage would rot unnoticed.

#include <QtTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTextStream>

#include <PhosphorAnimation/ProfilePaths.h>

#include "shadervalidate/packvalidators.h"

namespace {

struct PackResult
{
    int errors = 0;
    QString report;
};

/// The validator derives its include path from the pack's PARENT directory
/// (`<packs-root>/shared`), matching the animation runtime. A temp packs-root
/// has no such directory, so every fragment stage would fail include
/// expansion for reasons that have nothing to do with the pack under test.
/// Link the real bundled one in once per root.
///
/// Returns false when the source tree is not available, which is the caller's
/// cue to skip rather than fail.
bool linkSharedIncludes(const QTemporaryDir& tmp)
{
    const QString target = QStringLiteral(P_SOURCE_DIR "/data/animations/shared");
    if (!QDir(target).exists()) {
        return false;
    }
    const QString link = tmp.filePath(QStringLiteral("shared"));
    if (QFileInfo::exists(link)) {
        return true;
    }
    return QFile::link(target, link);
}

/// Write a pack directory from @p metadata (plus a trivial fragment shader
/// unless the caller declared its own) and run the animation validator over
/// it, capturing the report.
PackResult validate(const QTemporaryDir& tmp, const QString& name, const QJsonObject& metadata,
                    bool writeFragment = true)
{
    const QString dir = tmp.filePath(name);
    QDir().mkpath(dir);
    QFile meta(dir + QStringLiteral("/metadata.json"));
    if (!meta.open(QIODevice::WriteOnly)) {
        return {};
    }
    meta.write(QJsonDocument(metadata).toJson());
    meta.close();
    if (writeFragment) {
        QFile frag(dir + QStringLiteral("/effect.frag"));
        if (!frag.open(QIODevice::WriteOnly)) {
            return {};
        }
        frag.write("vec4 pTransition(vec2 uv, float t) { return vec4(0.0); }\n");
        frag.close();
    }

    PackResult result;
    QTextStream stream(&result.report);
    result.errors = PlasmaZones::ShaderValidate::validateAnimationPack(dir, stream);
    stream.flush();
    return result;
}

QJsonObject basePack(const QString& id)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("id"), id);
    obj.insert(QStringLiteral("name"), QStringLiteral("Test Pack"));
    obj.insert(QStringLiteral("fragmentShader"), QStringLiteral("effect.frag"));
    return obj;
}

QJsonArray toArray(const QStringList& values)
{
    QJsonArray arr;
    for (const QString& v : values) {
        arr.append(v);
    }
    return arr;
}

} // namespace

class TestPackValidators : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    /// Every token the parser accepts must pass the lint. These two lists
    /// drifted apart once already: the parser learned "strip" while the lint
    /// still named four tokens, so a correct pack drew a spurious diagnostic.
    /// Both now read ProfilePaths::allEventClassTokens(), and this asserts it
    /// end to end rather than trusting that they still do.
    void everyAcceptedTokenPassesTheLint()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        if (!linkSharedIncludes(tmp))
            QSKIP("data/animations/shared not found — running outside source tree");

        const QStringList tokens = PhosphorAnimation::ProfilePaths::allEventClassTokens();
        QVERIFY(!tokens.isEmpty());

        QStringList rejected;
        for (const QString& token : tokens) {
            QJsonObject obj = basePack(QStringLiteral("tok-") + token);
            obj.insert(QStringLiteral("appliesTo"), toArray({token}));
            const PackResult r = validate(tmp, QStringLiteral("tok-") + token, obj);
            if (r.report.contains(QStringLiteral("unknown appliesTo token"))) {
                rejected << token;
            }
        }
        QVERIFY2(rejected.isEmpty(),
                 qPrintable(QStringLiteral("the lint rejects token(s) the parser accepts: ")
                            + rejected.join(QLatin1String(", "))));
    }

    /// The complement: an unknown token IS linted, and the message names the
    /// live vocabulary rather than a stale hand-written list.
    void unknownTokenIsLintedAndMessageNamesTheVocabulary()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        if (!linkSharedIncludes(tmp))
            QSKIP("data/animations/shared not found — running outside source tree");

        QJsonObject obj = basePack(QStringLiteral("bad-token"));
        obj.insert(QStringLiteral("appliesTo"), toArray({QStringLiteral("teleport")}));
        const PackResult r = validate(tmp, QStringLiteral("bad-token"), obj);

        QVERIFY(r.errors > 0);
        QVERIFY(r.report.contains(QStringLiteral("unknown appliesTo token 'teleport'")));
        for (const QString& token : PhosphorAnimation::ProfilePaths::allEventClassTokens()) {
            QVERIFY2(r.report.contains(token),
                     qPrintable(QStringLiteral("diagnostic omits the valid token '") + token + QLatin1Char('\'')));
        }
    }

    /// An explicit `"appliesTo": []` is a legal spelling of the universal
    /// default and must stay lint-free, while the two degenerate spellings
    /// that LOOK like a constraint and silently become universal are linted.
    /// The difference is author intent, not effect: `[""]` and `["teleport"]`
    /// are typos, `[]` is a statement. This pins the asymmetry so nobody
    /// "evens it up" and starts failing every universal pack that spells its
    /// default out.
    void explicitEmptyAppliesToIsNotLinted()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        if (!linkSharedIncludes(tmp))
            QSKIP("data/animations/shared not found — running outside source tree");

        QJsonObject empty = basePack(QStringLiteral("empty-applies"));
        empty.insert(QStringLiteral("appliesTo"), QJsonArray());
        const PackResult r = validate(tmp, QStringLiteral("empty-applies"), empty);
        QVERIFY2(!r.report.contains(QStringLiteral("appliesTo")),
                 qPrintable(QStringLiteral("an explicit empty appliesTo must draw no diagnostic:\n") + r.report));
        QCOMPARE(r.errors, 0);

        // The degenerate cousins DO lint, which is what makes the silence
        // above a decision rather than a gap.
        QJsonObject blank = basePack(QStringLiteral("blank-token"));
        blank.insert(QStringLiteral("appliesTo"), toArray({QString()}));
        QVERIFY(validate(tmp, QStringLiteral("blank-token"), blank)
                    .report.contains(QStringLiteral("empty appliesTo token")));
    }

    /// A bare string instead of an array is ignored wholesale at load, which
    /// silently makes the pack universal.
    void nonArrayAppliesToIsLinted()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        if (!linkSharedIncludes(tmp))
            QSKIP("data/animations/shared not found — running outside source tree");

        QJsonObject obj = basePack(QStringLiteral("bad-shape"));
        obj.insert(QStringLiteral("appliesTo"), QStringLiteral("strip"));
        const PackResult r = validate(tmp, QStringLiteral("bad-shape"), obj);

        QVERIFY(r.errors > 0);
        QVERIFY(r.report.contains(QStringLiteral("appliesTo must be an array")));
    }

    /// The screen-level passes draw their own full-screen quad, so a vertex
    /// stage or a geometry grid declared by a desktop or strip pack is loaded
    /// and then never used.
    void screenLevelPacksAreToldTheirVertexStageIsIgnored()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        if (!linkSharedIncludes(tmp))
            QSKIP("data/animations/shared not found — running outside source tree");

        QJsonObject obj = basePack(QStringLiteral("strip-vert"));
        obj.insert(QStringLiteral("appliesTo"), toArray({QStringLiteral("strip")}));
        obj.insert(QStringLiteral("vertexShader"), QStringLiteral("effect.vert"));
        obj.insert(QStringLiteral("geometryGrid"), 8);

        const QString dir = tmp.filePath(QStringLiteral("strip-vert"));
        QDir().mkpath(dir);
        QFile vert(dir + QStringLiteral("/effect.vert"));
        QVERIFY(vert.open(QIODevice::WriteOnly));
        vert.write("void main() {}\n");
        vert.close();

        const PackResult r = validate(tmp, QStringLiteral("strip-vert"), obj);
        QVERIFY(r.report.contains(QStringLiteral("vertexShader is ignored for desktop/strip packs")));
        QVERIFY(r.report.contains(QStringLiteral("geometryGrid is ignored for desktop/strip packs")));

        // A single-surface pack keeps both without complaint.
        QJsonObject surface = basePack(QStringLiteral("surface-vert"));
        surface.insert(QStringLiteral("appliesTo"), toArray({QStringLiteral("appearance")}));
        surface.insert(QStringLiteral("geometryGrid"), 8);
        const PackResult ok = validate(tmp, QStringLiteral("surface-vert"), surface);
        QVERIFY(!ok.report.contains(QStringLiteral("geometryGrid is ignored")));
    }

    /// A multipass pack's BUFFER shaders are compiled, not just existence
    /// checked. No bundled animation pack is multipass today, so the
    /// bundled-pack gate cannot exercise this path at all — without this test
    /// the bake would be dead code that silently stops working.
    ///
    /// This one really does invoke glslang, unlike the metadata-only tests
    /// above.
    void multipassBufferShadersAreCompiled()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        if (!linkSharedIncludes(tmp))
            QSKIP("data/animations/shared not found — running outside source tree");

        const auto writeBuffer = [&tmp](const QString& pack, const QString& body) {
            const QString dir = tmp.filePath(pack);
            QDir().mkpath(dir);
            QFile buf(dir + QStringLiteral("/buffer0.frag"));
            QVERIFY(buf.open(QIODevice::WriteOnly));
            buf.write(body.toUtf8());
            buf.close();
        };

        QJsonObject obj = basePack(QStringLiteral("mp-good"));
        obj.insert(QStringLiteral("multipass"), true);
        obj.insert(QStringLiteral("bufferShaders"), toArray({QStringLiteral("buffer0.frag")}));

        // A buffer pass ships its own main() and no entry scaffold.
        writeBuffer(QStringLiteral("mp-good"),
                    QStringLiteral("#version 440\n"
                                   "layout(location = 0) out vec4 fragColor;\n"
                                   "void main() { fragColor = vec4(1.0); }\n"));
        const PackResult good = validate(tmp, QStringLiteral("mp-good"), obj);
        QVERIFY2(!good.report.contains(QStringLiteral("buffer0.frag    ERROR")),
                 qPrintable(QStringLiteral("a valid buffer pass must bake clean:\n") + good.report));

        // The same pack with a syntax error in the buffer must be caught
        // HERE, not at the live daemon.
        QJsonObject bad = basePack(QStringLiteral("mp-bad"));
        bad.insert(QStringLiteral("multipass"), true);
        bad.insert(QStringLiteral("bufferShaders"), toArray({QStringLiteral("buffer0.frag")}));
        writeBuffer(QStringLiteral("mp-bad"),
                    QStringLiteral("#version 440\n"
                                   "layout(location = 0) out vec4 fragColor;\n"
                                   "void main() { fragColor = notADeclaredThing; }\n"));
        const PackResult r = validate(tmp, QStringLiteral("mp-bad"), bad);
        QVERIFY2(r.errors > 0, qPrintable(QStringLiteral("a broken buffer pass must fail the gate:\n") + r.report));
        QVERIFY(r.report.contains(QStringLiteral("buffer0.frag")));
    }

    /// A buffer pass gets NO p_<id> preamble, because
    /// ShaderNodeRhi::bakeBufferShaders does not splice one — it loads,
    /// expands includes and compiles. The gate must reproduce that exactly:
    /// splicing a preamble the runtime withholds would pass sources that fail
    /// live, and withholding one the runtime splices would fail sources that
    /// work. Pin the direction so a future "helpful" splice is caught.
    void multipassBufferShadersGetNoParamPreamble()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        if (!linkSharedIncludes(tmp))
            QSKIP("data/animations/shared not found — running outside source tree");

        QJsonObject param;
        param.insert(QStringLiteral("id"), QStringLiteral("strength"));
        param.insert(QStringLiteral("type"), QStringLiteral("float"));
        param.insert(QStringLiteral("default"), 0.5);
        QJsonArray params;
        params.append(param);

        QJsonObject obj = basePack(QStringLiteral("mp-param"));
        obj.insert(QStringLiteral("multipass"), true);
        obj.insert(QStringLiteral("bufferShaders"), toArray({QStringLiteral("buffer0.frag")}));
        obj.insert(QStringLiteral("parameters"), params);

        const QString dir = tmp.filePath(QStringLiteral("mp-param"));
        QDir().mkpath(dir);
        QFile buf(dir + QStringLiteral("/buffer0.frag"));
        QVERIFY(buf.open(QIODevice::WriteOnly));
        buf.write(
            "#version 440\n"
            "layout(location = 0) out vec4 fragColor;\n"
            "void main() { fragColor = vec4(p_strength); }\n");
        buf.close();

        const PackResult r = validate(tmp, QStringLiteral("mp-param"), obj);
        QVERIFY2(r.errors > 0,
                 qPrintable(QStringLiteral("p_<id> in a buffer pass must NOT resolve — the runtime splices no "
                                           "preamble there, so the gate must not either:\n")
                            + r.report));
    }

    /// geometryGrid clamps to 0 at load, so a negative value disables the
    /// grid indistinguishably from never declaring it.
    void negativeGeometryGridIsLinted()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        if (!linkSharedIncludes(tmp))
            QSKIP("data/animations/shared not found — running outside source tree");

        QJsonObject obj = basePack(QStringLiteral("neg-grid"));
        obj.insert(QStringLiteral("geometryGrid"), -4);
        const PackResult r = validate(tmp, QStringLiteral("neg-grid"), obj);

        QVERIFY(r.errors > 0);
        QVERIFY(r.report.contains(QStringLiteral("geometryGrid is negative")));
    }
};

QTEST_MAIN(TestPackValidators)
#include "test_pack_validators.moc"
