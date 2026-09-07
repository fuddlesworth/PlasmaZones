// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorPointer/PointerShaderContract.h>
#include <PhosphorPointer/PointerShaderEffect.h>
#include <PhosphorPointer/PointerShaderRegistry.h>

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest/QtTest>

using namespace PhosphorPointerShaders;

namespace {

/// Write @p contents to @p path, creating parent directories.
bool writeFile(const QString& path, const QByteArray& contents)
{
    const QFileInfo fi(path);
    if (!QDir().mkpath(fi.absolutePath()))
        return false;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return f.write(contents) == contents.size();
}

/// Author `<root>/<subdir>/metadata.json` plus stubs for the files it names,
/// so on-disk existence checks in the loader are satisfied.
bool writePack(const QString& root, const QString& subdir, const QJsonObject& metadata, const QStringList& extraFiles)
{
    const QString packDir = root + QLatin1Char('/') + subdir;
    if (!writeFile(packDir + QStringLiteral("/metadata.json"), QJsonDocument(metadata).toJson()))
        return false;
    for (const QString& rel : extraFiles) {
        if (!writeFile(packDir + QLatin1Char('/') + rel, QByteArrayLiteral("// stub\n")))
            return false;
    }
    return true;
}

QJsonObject makeParameter(const QString& id, const QString& type, const QJsonValue& defaultValue)
{
    QJsonObject p;
    p.insert(QLatin1String("id"), id);
    p.insert(QLatin1String("name"), id);
    p.insert(QLatin1String("type"), type);
    p.insert(QLatin1String("default"), defaultValue);
    return p;
}

/// The smallest metadata object the registry accepts.
QJsonObject makeMetadata(const QString& id)
{
    QJsonObject m;
    m.insert(QLatin1String("id"), id);
    m.insert(QLatin1String("name"), id);
    m.insert(QLatin1String("fragmentShader"), QStringLiteral("effect.frag"));
    m.insert(QLatin1String("parameters"), QJsonArray{});
    return m;
}

} // namespace

/// The registry is the boundary between pack authors and both runtimes. It
/// parses untrusted on-disk metadata, so the tests here cover the parse
/// contract, the pointer-specific fields, the search-path priority the
/// compositor and the settings app both rely on, and the parameter slot
/// allocation the generated GLSL preamble is written against.
class TestPointerShaderRegistry : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testParsesPointerSpecificFields();
    void testLayerDefaultsToBelowAndRejectsUnknownTokens();
    void testReachIsClampedAndResolvedFromItsParameter();
    void testTrailSecondsDefaultsWhenAbsent();
    void testBufferPassesAreCappedAtTheContractBudget();
    void testInvalidPackIsRejected();
    void testUserPathWinsOnIdCollision();
    void testParamTranslationAllocatesSlotsInDeclarationOrder();
    void testParamPreambleMapsEveryDeclaredId();
    void testEntryPointScaffoldMatchesTheSharedContract();
    void testIncludePathsResolveTheSharedDirectory();
};

void TestPointerShaderRegistry::testParsesPointerSpecificFields()
{
    QJsonObject m = makeMetadata(QStringLiteral("trail"));
    m.insert(QLatin1String("description"), QStringLiteral("A ribbon behind the pointer."));
    m.insert(QLatin1String("author"), QStringLiteral("PlasmaZones"));
    m.insert(QLatin1String("version"), QStringLiteral("1.0"));
    m.insert(QLatin1String("category"), QStringLiteral("Trail"));
    m.insert(QLatin1String("layer"), QStringLiteral("above"));
    m.insert(QLatin1String("reach"), 120.0);
    m.insert(QLatin1String("reachParam"), QStringLiteral("width"));
    m.insert(QLatin1String("trailSeconds"), 1.5);
    m.insert(QLatin1String("needsCursor"), true);
    m.insert(QLatin1String("parameters"),
             QJsonArray{makeParameter(QStringLiteral("width"), QStringLiteral("float"), 6.0)});

    const PointerShaderEffect e = PointerShaderEffect::fromJson(m, QStringLiteral("/packs/trail"), false);
    QVERIFY(e.isValid());
    QCOMPARE(e.id, QStringLiteral("trail"));
    QCOMPARE(e.category, QStringLiteral("Trail"));
    QCOMPARE(e.layer, PointerShaderEffect::Layer::Above);
    QCOMPARE(e.reach, 120.0);
    QCOMPARE(e.reachParam, QStringLiteral("width"));
    QCOMPARE(e.trailSeconds, 1.5);
    QVERIFY(e.needsCursor);
    QCOMPARE(e.parameters.size(), 1);
    QCOMPARE(e.defaultParams().value(QStringLiteral("width")).toDouble(), 6.0);
}

void TestPointerShaderRegistry::testLayerDefaultsToBelowAndRejectsUnknownTokens()
{
    // Below is the cheap path: it never touches cursor visibility. An
    // unreadable token must fall back to it rather than to the costly one.
    QCOMPARE(PointerShaderEffect::fromJson(makeMetadata(QStringLiteral("a"))).layer, PointerShaderEffect::Layer::Below);

    QJsonObject explicitBelow = makeMetadata(QStringLiteral("b"));
    explicitBelow.insert(QLatin1String("layer"), QStringLiteral("below"));
    QCOMPARE(PointerShaderEffect::fromJson(explicitBelow).layer, PointerShaderEffect::Layer::Below);

    QJsonObject nonsense = makeMetadata(QStringLiteral("c"));
    nonsense.insert(QLatin1String("layer"), QStringLiteral("sideways"));
    QCOMPARE(PointerShaderEffect::fromJson(nonsense).layer, PointerShaderEffect::Layer::Below);
}

void TestPointerShaderRegistry::testReachIsClampedAndResolvedFromItsParameter()
{
    // Reach drives the damage rect, so an unbounded value would repaint the
    // whole screen every frame the pointer moves.
    QJsonObject huge = makeMetadata(QStringLiteral("huge"));
    huge.insert(QLatin1String("reach"), 999999.0);
    QCOMPARE(PointerShaderEffect::fromJson(huge).reach, PointerShaderEffect::kMaxReach);

    QJsonObject negative = makeMetadata(QStringLiteral("negative"));
    negative.insert(QLatin1String("reach"), -50.0);
    QCOMPARE(PointerShaderEffect::fromJson(negative).reach, 0.0);

    // A pack whose reach follows a user-tunable width has to track the
    // resolved override, not the declared fallback.
    QJsonObject tunable = makeMetadata(QStringLiteral("tunable"));
    tunable.insert(QLatin1String("reach"), 64.0);
    tunable.insert(QLatin1String("reachParam"), QStringLiteral("width"));
    tunable.insert(QLatin1String("parameters"),
                   QJsonArray{makeParameter(QStringLiteral("width"), QStringLiteral("float"), 6.0)});
    const PointerShaderEffect e = PointerShaderEffect::fromJson(tunable);

    QVariantMap overridden;
    overridden.insert(QStringLiteral("width"), 30.0);
    QCOMPARE(e.resolvedReach(overridden), 30.0);
    // With no override the parameter's own default stands in.
    QCOMPARE(e.resolvedReach({}), 6.0);

    // A reachParam naming a parameter that does not exist falls back to the
    // declared reach rather than to zero, which would clip the pack away.
    QJsonObject dangling = makeMetadata(QStringLiteral("dangling"));
    dangling.insert(QLatin1String("reach"), 70.0);
    dangling.insert(QLatin1String("reachParam"), QStringLiteral("nosuch"));
    QCOMPARE(PointerShaderEffect::fromJson(dangling).resolvedReach({}), 70.0);
}

void TestPointerShaderRegistry::testTrailSecondsDefaultsWhenAbsent()
{
    // trailSeconds is how long the pass keeps requesting frames after the
    // last event, so a missing value must not read as zero (never draws) or
    // as unbounded (never stops).
    const PointerShaderEffect e = PointerShaderEffect::fromJson(makeMetadata(QStringLiteral("plain")));
    QCOMPARE(e.trailSeconds, 1.0);
}

void TestPointerShaderRegistry::testBufferPassesAreCappedAtTheContractBudget()
{
    QJsonObject m = makeMetadata(QStringLiteral("multi"));
    m.insert(QLatin1String("multipass"), true);
    QJsonArray buffers;
    for (int i = 0; i < PointerShaderContract::kMaxBufferPasses + 3; ++i) {
        buffers.append(QStringLiteral("buffer%1.frag").arg(i));
    }
    m.insert(QLatin1String("bufferShaders"), buffers);

    const PointerShaderEffect e = PointerShaderEffect::fromJson(m);
    QVERIFY(e.isMultipass);
    QCOMPARE(e.bufferShaderPaths.size(), PointerShaderContract::kMaxBufferPasses);
}

void TestPointerShaderRegistry::testInvalidPackIsRejected()
{
    // Identity and a fragment shader are the two things every runtime needs.
    QJsonObject noId = makeMetadata(QStringLiteral("x"));
    noId.remove(QLatin1String("id"));
    QVERIFY(!PointerShaderEffect::fromJson(noId).isValid());

    QJsonObject noFragment = makeMetadata(QStringLiteral("x"));
    noFragment.remove(QLatin1String("fragmentShader"));
    QVERIFY(!PointerShaderEffect::fromJson(noFragment).isValid());
}

void TestPointerShaderRegistry::testUserPathWinsOnIdCollision()
{
    // A user pack overriding a bundled id must win in every host. The
    // compositor's surface registry once shadowed the user copy by
    // registering the search paths in the wrong order, so this is pinned.
    QTemporaryDir systemDir;
    QTemporaryDir userDir;
    QVERIFY(systemDir.isValid());
    QVERIFY(userDir.isValid());

    QJsonObject bundled = makeMetadata(QStringLiteral("halo"));
    bundled.insert(QLatin1String("name"), QStringLiteral("Bundled Halo"));
    QVERIFY(writePack(systemDir.path(), QStringLiteral("halo"), bundled, {QStringLiteral("effect.frag")}));

    QJsonObject overridden = makeMetadata(QStringLiteral("halo"));
    overridden.insert(QLatin1String("name"), QStringLiteral("My Halo"));
    QVERIFY(writePack(userDir.path(), QStringLiteral("halo"), overridden, {QStringLiteral("effect.frag")}));

    PointerShaderRegistry registry;
    // setUserPath only classifies packs found under that root as user-owned.
    // The directory still has to be a search path in its own right, and it is
    // registered LAST because the scan reverse-iterates the registered paths
    // with first-wins on a colliding id. This is the same order the hosts use,
    // where the user dir arrives as the last entry of the XDG sweep.
    registry.addSearchPaths({systemDir.path(), userDir.path()});
    registry.setUserPath(userDir.path());
    registry.refresh();

    QVERIFY(registry.hasEffect(QStringLiteral("halo")));
    QCOMPARE(registry.effect(QStringLiteral("halo")).name, QStringLiteral("My Halo"));
    QVERIFY(registry.effect(QStringLiteral("halo")).isUserEffect);
    // One id, one entry: the shadowed copy must not also be listed.
    QCOMPARE(registry.effectIds().count(QStringLiteral("halo")), 1);
}

void TestPointerShaderRegistry::testParamTranslationAllocatesSlotsInDeclarationOrder()
{
    // The generated preamble maps p_<id> onto a slot by position, so the
    // allocator's order is what makes a pack's names line up with its values.
    // Scalars and colours use independent budgets.
    QJsonObject m = makeMetadata(QStringLiteral("mixed"));
    m.insert(QLatin1String("parameters"),
             QJsonArray{
                 makeParameter(QStringLiteral("width"), QStringLiteral("float"), 6.0),
                 makeParameter(QStringLiteral("colorA"), QStringLiteral("color"), QStringLiteral("#ff22d3ee")),
                 makeParameter(QStringLiteral("count"), QStringLiteral("int"), 8),
                 makeParameter(QStringLiteral("colorB"), QStringLiteral("color"), QStringLiteral("#fff43f5e")),
                 makeParameter(QStringLiteral("soft"), QStringLiteral("bool"), true),
             });
    const PointerShaderEffect e = PointerShaderEffect::fromJson(m);

    QVariantMap friendly;
    friendly.insert(QStringLiteral("width"), 12.0);

    const QVariantMap translated = PointerShaderRegistry::translatePointerParams(e, friendly);
    // Overridden value in the first scalar slot, declared defaults after it.
    QCOMPARE(translated.value(PointerShaderContract::paramKey(0)).toDouble(), 12.0);
    QCOMPARE(translated.value(PointerShaderContract::paramKey(1)).toInt(), 8);
    // A bool reaches the shader as a float, since GLSL has no bool uniform here.
    QCOMPARE(translated.value(PointerShaderContract::paramKey(2)).toFloat(), 1.0f);

    QCOMPARE(translated.value(PointerShaderContract::colorKey(0)).value<QColor>(), QColor(QStringLiteral("#22d3ee")));
    QCOMPARE(translated.value(PointerShaderContract::colorKey(1)).value<QColor>(), QColor(QStringLiteral("#f43f5e")));
}

void TestPointerShaderRegistry::testParamPreambleMapsEveryDeclaredId()
{
    QJsonObject m = makeMetadata(QStringLiteral("named"));
    m.insert(QLatin1String("parameters"),
             QJsonArray{
                 makeParameter(QStringLiteral("width"), QStringLiteral("float"), 6.0),
                 makeParameter(QStringLiteral("colorA"), QStringLiteral("color"), QStringLiteral("#ff22d3ee")),
             });
    const PointerShaderEffect e = PointerShaderEffect::fromJson(m);

    const QString preamble = PointerShaderRegistry::paramPreamble(e);
    // A pack body writes p_width and p_colorA; without these defines the
    // shader fails to compile rather than silently reading the wrong slot.
    QVERIFY(preamble.contains(QStringLiteral("#define p_width")));
    QVERIFY(preamble.contains(QStringLiteral("#define p_colorA")));
    QVERIFY(preamble.contains(QStringLiteral("customParams")));
    QVERIFY(preamble.contains(QStringLiteral("customColors")));
}

void TestPointerShaderRegistry::testEntryPointScaffoldMatchesTheSharedContract()
{
    // Both hosts and the validator assemble a pack the same way, and the
    // pieces have to agree with data/pointer/shared/pointer_lib.glsl.
    const QString prologue = PointerShaderRegistry::pointerEntryPrologue();
    QVERIFY(prologue.startsWith(QStringLiteral("#version 450")));
    QVERIFY(prologue.contains(QStringLiteral("#include <pointer_lib.glsl>")));
    QVERIFY(prologue.contains(QStringLiteral("in vec2 vTexCoord")));
    QVERIFY(prologue.contains(QStringLiteral("out vec4 fragColor")));

    const auto candidates = PointerShaderRegistry::pointerEntryCandidates();
    QCOMPARE(candidates.size(), 1);
    QCOMPARE(candidates.at(0).functionName, QStringLiteral("pPointer"));
    QVERIFY(candidates.at(0).generatedMain.contains(QStringLiteral("pPointer(vTexCoord)")));
}

void TestPointerShaderRegistry::testIncludePathsResolveTheSharedDirectory()
{
    // `#include <pointer_uniforms.glsl>` resolves against the pack root's
    // shared/ dir, one level above the pack itself.
    const QStringList paths = PointerShaderRegistry::includePathsFor(QStringLiteral("/data/pointer/halo"));
    QCOMPARE(paths.size(), 2);
    QCOMPARE(paths.at(0), QStringLiteral("/data/pointer/shared"));
    QCOMPARE(paths.at(1), QStringLiteral("/data/pointer"));

    QVERIFY(PointerShaderRegistry::includePathsFor(QString()).isEmpty());
}

QTEST_MAIN(TestPointerShaderRegistry)
#include "test_pointershaderregistry.moc"
