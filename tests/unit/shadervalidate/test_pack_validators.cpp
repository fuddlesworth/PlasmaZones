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
// Only metadata lints are exercised. The stage compile needs glslang and is
// covered by the bundled gate.

#include <QtTest>

#include <QDir>
#include <QFile>
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

    /// A bare string instead of an array is ignored wholesale at load, which
    /// silently makes the pack universal.
    void nonArrayAppliesToIsLinted()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

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

    /// geometryGrid clamps to 0 at load, so a negative value disables the
    /// grid indistinguishably from never declaring it.
    void negativeGeometryGridIsLinted()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        QJsonObject obj = basePack(QStringLiteral("neg-grid"));
        obj.insert(QStringLiteral("geometryGrid"), -4);
        const PackResult r = validate(tmp, QStringLiteral("neg-grid"), obj);

        QVERIFY(r.errors > 0);
        QVERIFY(r.report.contains(QStringLiteral("geometryGrid is negative")));
    }
};

QTEST_MAIN(TestPackValidators)
#include "test_pack_validators.moc"
