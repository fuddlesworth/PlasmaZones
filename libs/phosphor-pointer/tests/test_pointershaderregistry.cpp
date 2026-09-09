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
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <algorithm>

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
    void testLayerTokenRoundTripsThroughFromJson();
    void testLayerDefaultsToBelowAndRejectsUnknownTokens();
    void testReachIsClampedAndResolvedFromItsParameter();
    void testTrailSecondsDefaultsWhenAbsent();
    void testBufferPassesAreCappedAtTheContractBudget();
    void testInvalidPackIsRejected();
    void testUserPathWinsOnIdCollision();
    void testRegistryKeyedTranslationResolvesTheRegisteredEffect();
    void testFragmentPathEscapingThePackIsRefused();
    void testRuntimeTextureOverrideEscapingThePackIsCleared();
    void testDuplicateParameterIdsKeepTheFirstDeclaration();
    void testTexturesAreCappedAndEmptyEntriesDropped();
    void testMissingBufferShaderOnDiskDisablesMultipass();
    void testParamTranslationAllocatesSlotsInDeclarationOrder();
    void testParamPreambleMapsEveryDeclaredId();
    void testInvalidParameterIdConsumesNoLane();
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
    QCOMPARE(e.parameters.at(0).defaultValue.toDouble(), 6.0);
}

void TestPointerShaderRegistry::testLayerTokenRoundTripsThroughFromJson()
{
    // The parser and the spelling accessor are the two halves of one mapping,
    // and a consumer that shows the value to a user reads the accessor. If
    // either drifts the settings page shows a word the pack never wrote.
    QJsonObject above = makeMetadata(QStringLiteral("a"));
    above.insert(QLatin1String("layer"), QStringLiteral("above"));
    QCOMPARE(PointerShaderEffect::layerToken(PointerShaderEffect::fromJson(above).layer), QStringLiteral("above"));

    QJsonObject below = makeMetadata(QStringLiteral("b"));
    below.insert(QLatin1String("layer"), QStringLiteral("below"));
    QCOMPARE(PointerShaderEffect::layerToken(PointerShaderEffect::fromJson(below).layer), QStringLiteral("below"));

    // And the other direction: the token the accessor emits parses back to
    // the same enumerator.
    for (const auto layer : {PointerShaderEffect::Layer::Above, PointerShaderEffect::Layer::Below}) {
        QJsonObject m = makeMetadata(QStringLiteral("rt"));
        m.insert(QLatin1String("layer"), PointerShaderEffect::layerToken(layer));
        QCOMPARE(PointerShaderEffect::fromJson(m).layer, layer);
    }
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

    // The declared field may read 0, but the RESOLVED reach never does: at 0
    // a single-event burst has a damage rect with no area, so the pass would
    // stay live for trailSeconds painting nothing. One logical px is the
    // floor that still turns a point into a region.
    QCOMPARE(PointerShaderEffect::fromJson(negative).resolvedReach({}), PointerShaderEffect::kMinReach);
    QJsonObject zero = makeMetadata(QStringLiteral("zero"));
    zero.insert(QLatin1String("reach"), 0.0);
    QCOMPARE(PointerShaderEffect::fromJson(zero).resolvedReach({}), PointerShaderEffect::kMinReach);

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
    // A user dragging the width to 0 gets the floor, not a pack that
    // silently stops painting.
    QVariantMap zeroed;
    zeroed.insert(QStringLiteral("width"), 0.0);
    QCOMPARE(e.resolvedReach(zeroed), PointerShaderEffect::kMinReach);

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
    const QList<PointerShaderEffect> all = registry.availableEffects();
    QCOMPARE(std::count_if(all.cbegin(), all.cend(),
                           [](const PointerShaderEffect& e) {
                               return e.id == QLatin1String("halo");
                           }),
             qsizetype{1});
}

void TestPointerShaderRegistry::testRegistryKeyedTranslationResolvesTheRegisteredEffect()
{
    // The id-keyed overload is what a host calls on every parameter change,
    // so it has to reach the same effect the registry holds and, for an id
    // the registry does not hold, answer with nothing rather than a map of
    // defaults for an effect that does not exist.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QJsonObject m = makeMetadata(QStringLiteral("keyed"));
    m.insert(QLatin1String("parameters"),
             QJsonArray{
                 makeParameter(QStringLiteral("width"), QStringLiteral("float"), 6.0),
                 makeParameter(QStringLiteral("tint"), QStringLiteral("color"), QStringLiteral("#ff22d3ee")),
             });
    QVERIFY(writePack(dir.path(), QStringLiteral("keyed"), m, {QStringLiteral("effect.frag")}));

    PointerShaderRegistry registry;
    registry.addSearchPaths({dir.path()}, PhosphorFsLoader::LiveReload::Off);
    registry.refresh();
    QVERIFY(registry.hasEffect(QStringLiteral("keyed")));

    QVariantMap friendly;
    friendly.insert(QStringLiteral("width"), 12.0);
    const QVariantMap byId = registry.translatePointerParams(QStringLiteral("keyed"), friendly);
    const QVariantMap byEffect =
        PointerShaderRegistry::translatePointerParams(registry.effect(QStringLiteral("keyed")), friendly);
    QCOMPARE(byId, byEffect);
    QCOMPARE(byId.value(PointerShaderContract::paramKey(0)).toDouble(), 12.0);
    QCOMPARE(byId.value(PointerShaderContract::colorKey(0)).value<QColor>(), QColor(QStringLiteral("#22d3ee")));

    QVERIFY(registry.translatePointerParams(QStringLiteral("nosuch"), friendly).isEmpty());
    QVERIFY(registry.translatePointerParams(QString(), friendly).isEmpty());
}

void TestPointerShaderRegistry::testFragmentPathEscapingThePackIsRefused()
{
    // Pack metadata is untrusted input. A fragment path that walks out of the
    // pack dir must fail the pack closed (an empty fragment path makes the
    // effect invalid) rather than compile whatever file it pointed at.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QJsonObject m = makeMetadata(QStringLiteral("escape"));
    m.insert(QLatin1String("fragmentShader"), QStringLiteral("../../etc/passwd"));
    m.insert(QLatin1String("vertexShader"), QStringLiteral("../sibling/pointer.vert"));
    m.insert(QLatin1String("preview"), QStringLiteral("/etc/hostname"));
    QVERIFY(writePack(dir.path(), QStringLiteral("escape"), m, {}));
    const QString packDir = dir.path() + QStringLiteral("/escape");

    const PointerShaderEffect e = PointerShaderEffect::fromJson(m, packDir, false);
    QVERIFY(e.fragmentShaderPath.isEmpty());
    QVERIFY(e.vertexShaderPath.isEmpty());
    // An absolute path outside the pack is the same escape (Reject policy).
    QVERIFY(e.previewPath.isEmpty());
    QVERIFY(!e.isValid());

    // And the loader agrees: the pack never reaches the registry.
    PointerShaderRegistry registry;
    registry.addSearchPaths({dir.path()}, PhosphorFsLoader::LiveReload::Off);
    registry.refresh();
    QVERIFY(!registry.hasEffect(QStringLiteral("escape")));

    // A well-formed relative path in the same pack resolves to an absolute
    // path inside it, which is the shape both runtimes compile from.
    QJsonObject ok = makeMetadata(QStringLiteral("inside"));
    QVERIFY(writePack(dir.path(), QStringLiteral("inside"), ok, {QStringLiteral("effect.frag")}));
    const PointerShaderEffect inside = PointerShaderEffect::fromJson(ok, dir.path() + QStringLiteral("/inside"), false);
    QVERIFY(inside.isValid());
    QVERIFY(inside.fragmentShaderPath.startsWith(QDir(dir.path()).absolutePath()));
}

void TestPointerShaderRegistry::testRuntimeTextureOverrideEscapingThePackIsCleared()
{
    // The override keys come from the user's parameter map, which is untrusted
    // in the same way. A traversal there clears the slot: neither the escaping
    // path nor the pack's own default may survive it, because a runtime that
    // then bound the default would be lying about what the user asked for.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QJsonObject m = makeMetadata(QStringLiteral("tex"));
    QJsonObject slot;
    slot.insert(QLatin1String("path"), QStringLiteral("noise.png"));
    slot.insert(QLatin1String("wrap"), QStringLiteral("repeat"));
    m.insert(QLatin1String("textures"), QJsonArray{slot});
    QVERIFY(
        writePack(dir.path(), QStringLiteral("tex"), m, {QStringLiteral("effect.frag"), QStringLiteral("noise.png")}));
    const QString packDir = dir.path() + QStringLiteral("/tex");
    const PointerShaderEffect e = PointerShaderEffect::fromJson(m, packDir, false);
    QCOMPARE(e.textures.size(), 1);

    const QString pathKey = QStringLiteral("uTexture1");
    const QString wrapKey = QStringLiteral("uTexture1_wrap");

    // No override: the pack default is bound with its wrap.
    const QVariantMap defaults = PointerShaderRegistry::translatePointerParams(e, {});
    QVERIFY(defaults.value(pathKey).toString().endsWith(QStringLiteral("/noise.png")));
    QCOMPARE(defaults.value(wrapKey).toString(), QStringLiteral("repeat"));

    // A traversal override clears the slot entirely.
    QVariantMap traversal;
    traversal.insert(pathKey, QStringLiteral("../../etc/passwd"));
    const QVariantMap cleared = PointerShaderRegistry::translatePointerParams(e, traversal);
    QVERIFY(!cleared.contains(pathKey));
    QVERIFY(!cleared.contains(wrapKey));

    // The in-memory arm (no source dir) runs the same guard lexically.
    const PointerShaderEffect inMemory = PointerShaderEffect::fromJson(m);
    const QVariantMap clearedInMemory = PointerShaderRegistry::translatePointerParams(inMemory, traversal);
    QVERIFY(!clearedInMemory.contains(pathKey));
    QVERIFY(!clearedInMemory.contains(wrapKey));
}

void TestPointerShaderRegistry::testDuplicateParameterIdsKeepTheFirstDeclaration()
{
    // Two parameters with one id would define the same p_ macro twice and
    // fail the compile, so the later one is dropped and the first keeps its
    // lane. The parameter after the duplicate must still land in the NEXT
    // lane, or every id after it reads the wrong slot.
    QJsonObject m = makeMetadata(QStringLiteral("dupes"));
    m.insert(QLatin1String("parameters"),
             QJsonArray{
                 makeParameter(QStringLiteral("width"), QStringLiteral("float"), 6.0),
                 makeParameter(QStringLiteral("width"), QStringLiteral("float"), 99.0),
                 makeParameter(QStringLiteral("glow"), QStringLiteral("float"), 2.0),
             });
    const PointerShaderEffect e = PointerShaderEffect::fromJson(m);
    QCOMPARE(e.parameters.size(), 2);
    QCOMPARE(e.parameters.at(0).id, QStringLiteral("width"));
    QCOMPARE(e.parameters.at(0).defaultValue.toDouble(), 6.0);
    QCOMPARE(e.parameters.at(1).id, QStringLiteral("glow"));

    const QVariantMap translated = PointerShaderRegistry::translatePointerParams(e, {});
    QCOMPARE(translated.value(PointerShaderContract::paramKey(0)).toDouble(), 6.0);
    QCOMPARE(translated.value(PointerShaderContract::paramKey(1)).toDouble(), 2.0);
    QVERIFY(!translated.contains(PointerShaderContract::paramKey(2)));
}

void TestPointerShaderRegistry::testTexturesAreCappedAndEmptyEntriesDropped()
{
    // The contract has three sampler slots. Surplus entries are dropped from
    // the end, and an entry with no path is dropped before the cap is
    // applied so it neither occupies a slot nor counts against the budget.
    QJsonArray textures;
    QJsonObject empty;
    empty.insert(QLatin1String("path"), QString());
    textures.append(empty);
    for (int i = 0; i < PointerShaderContract::kMaxUserTextureSlots + 2; ++i) {
        QJsonObject t;
        t.insert(QLatin1String("path"), QStringLiteral("tex%1.png").arg(i));
        textures.append(t);
    }
    QJsonObject m = makeMetadata(QStringLiteral("many"));
    m.insert(QLatin1String("textures"), textures);

    const PointerShaderEffect e = PointerShaderEffect::fromJson(m);
    QCOMPARE(e.textures.size(), PointerShaderContract::kMaxUserTextureSlots);
    // The empty entry took no slot: slot 0 is the first real texture.
    QCOMPARE(e.textures.at(0).path, QStringLiteral("tex0.png"));
    QCOMPARE(e.textures.last().path, QStringLiteral("tex%1.png").arg(PointerShaderContract::kMaxUserTextureSlots - 1));

    // Only an empty entry, which leaves no textures at all.
    QJsonObject onlyEmpty = makeMetadata(QStringLiteral("empty"));
    onlyEmpty.insert(QLatin1String("textures"), QJsonArray{empty});
    QVERIFY(PointerShaderEffect::fromJson(onlyEmpty).textures.isEmpty());
}

void TestPointerShaderRegistry::testMissingBufferShaderOnDiskDisablesMultipass()
{
    // The in-memory parse keeps whatever buffers are named; the on-disk parse
    // is where existence is checked, and one missing buffer fails multipass
    // closed for the whole pack rather than running a partial chain.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QJsonObject m = makeMetadata(QStringLiteral("mp"));
    m.insert(QLatin1String("multipass"), true);
    m.insert(QLatin1String("bufferFeedback"), true);
    m.insert(QLatin1String("bufferScale"), 0.5);
    m.insert(QLatin1String("bufferShaders"),
             QJsonArray{QStringLiteral("bufferA.frag"), QStringLiteral("bufferB.frag")});
    // Only bufferA exists on disk.
    QVERIFY(writePack(dir.path(), QStringLiteral("mp"), m,
                      {QStringLiteral("effect.frag"), QStringLiteral("bufferA.frag")}));
    const QString packDir = dir.path() + QStringLiteral("/mp");

    const PointerShaderEffect broken = PointerShaderEffect::fromJson(m, packDir, false);
    QVERIFY(broken.isValid());
    QVERIFY(!broken.isMultipass);
    // Everything that only means something under multipass is normalised
    // away with it, so no consumer allocates a buffer for a chain that will
    // never run one.
    QVERIFY(broken.bufferShaderPaths.isEmpty());
    QVERIFY(!broken.bufferFeedback);
    QCOMPARE(broken.bufferScale, 1.0);

    // With every buffer present the same metadata is multipass, and the
    // buffer paths come back absolute inside the pack.
    QVERIFY(writeFile(packDir + QStringLiteral("/bufferB.frag"), QByteArrayLiteral("// stub\n")));
    const PointerShaderEffect whole = PointerShaderEffect::fromJson(m, packDir, false);
    QVERIFY(whole.isMultipass);
    QCOMPARE(whole.bufferShaderPaths.size(), 2);
    for (const QString& p : whole.bufferShaderPaths) {
        QVERIFY(p.startsWith(QDir(packDir).absolutePath()));
    }
    QVERIFY(whole.bufferFeedback);
    QCOMPARE(whole.bufferScale, 0.5);
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
    // A pack body writes p_width and p_colorA, and each has to land on the
    // exact lane translatePointerParams fills, or the pack compiles and
    // silently reads the wrong slot. The accessor spellings are the ones
    // PhosphorShaders::CustomParams / CustomColors::glslAccessor emit.
    QVERIFY(preamble.contains(QStringLiteral("#define p_width customParams[0].x\n")));
    QVERIFY(preamble.contains(QStringLiteral("#define p_colorA customColors[0]\n")));
}

void TestPointerShaderRegistry::testInvalidParameterIdConsumesNoLane()
{
    // An id the preamble cannot turn into a macro is skipped on BOTH sides
    // without consuming a lane, so the parameters after it still line up. A
    // preamble that skipped it while the translator allocated for it would
    // shift every later value by one slot.
    QJsonObject m = makeMetadata(QStringLiteral("gappy"));
    m.insert(QLatin1String("parameters"),
             QJsonArray{
                 makeParameter(QStringLiteral("width"), QStringLiteral("float"), 6.0),
                 makeParameter(QStringLiteral("bad id"), QStringLiteral("float"), 99.0),
                 makeParameter(QStringLiteral("glow"), QStringLiteral("float"), 2.0),
                 makeParameter(QStringLiteral("colorA"), QStringLiteral("color"), QStringLiteral("#ff22d3ee")),
                 makeParameter(QStringLiteral("no colour"), QStringLiteral("color"), QStringLiteral("#ffffffff")),
                 makeParameter(QStringLiteral("colorB"), QStringLiteral("color"), QStringLiteral("#fff43f5e")),
             });
    const PointerShaderEffect e = PointerShaderEffect::fromJson(m);
    // The parser keeps the entry (it is the preamble's job to reject it), so
    // the skip below is a real skip and not an absent parameter.
    QCOMPARE(e.parameters.size(), 6);

    const QString preamble = PointerShaderRegistry::paramPreamble(e);
    QVERIFY(preamble.contains(QStringLiteral("#define p_width customParams[0].x\n")));
    QVERIFY(!preamble.contains(QStringLiteral("p_bad")));
    QVERIFY(preamble.contains(QStringLiteral("#define p_glow customParams[0].y\n")));
    QVERIFY(preamble.contains(QStringLiteral("#define p_colorA customColors[0]\n")));
    QVERIFY(!preamble.contains(QStringLiteral("p_no")));
    QVERIFY(preamble.contains(QStringLiteral("#define p_colorB customColors[1]\n")));

    const QVariantMap translated = PointerShaderRegistry::translatePointerParams(e, {});
    QCOMPARE(translated.value(PointerShaderContract::paramKey(0)).toDouble(), 6.0);
    QCOMPARE(translated.value(PointerShaderContract::paramKey(1)).toDouble(), 2.0);
    QVERIFY(!translated.contains(PointerShaderContract::paramKey(2)));
    QCOMPARE(translated.value(PointerShaderContract::colorKey(0)).value<QColor>(), QColor(QStringLiteral("#22d3ee")));
    QCOMPARE(translated.value(PointerShaderContract::colorKey(1)).value<QColor>(), QColor(QStringLiteral("#f43f5e")));
    QVERIFY(!translated.contains(PointerShaderContract::colorKey(2)));
}

void TestPointerShaderRegistry::testEntryPointScaffoldMatchesTheSharedContract()
{
    // Both hosts and the validator assemble a pack the same way, and the
    // pieces have to agree with data/pointer/shared/pointer_lib.glsl: the
    // prologue names an include that really ships, and the generated main
    // calls the entry point the pack defines and writes through the colour
    // hook the compositor overrides.
    const QString prologue = PointerShaderRegistry::pointerEntryPrologue();
    QVERIFY(prologue.startsWith(QStringLiteral("#version 450")));
    QVERIFY(prologue.contains(QStringLiteral("in vec2 vTexCoord")));
    QVERIFY(prologue.contains(QStringLiteral("out vec4 fragColor")));

    // The include directive resolves against the shipped shared dir.
    static const QRegularExpression includeRe(QStringLiteral("#include <([^>]+)>"));
    const QRegularExpressionMatch include = includeRe.match(prologue);
    QVERIFY2(include.hasMatch(), "the prologue no longer includes the shared helper");
    const QString sharedDir = QStringLiteral(P_SOURCE_DIR "/data/pointer/shared");
    const QString included = sharedDir + QLatin1Char('/') + include.captured(1);
    QVERIFY2(QFile::exists(included),
             qPrintable(QStringLiteral("prologue includes %1 which does not ship").arg(included)));

    // That helper is where the entry point's contract lives, and the pack,
    // not the helper, defines pPointer: a helper that defined it would clash
    // with every pack.
    QFile lib(included);
    QVERIFY(lib.open(QIODevice::ReadOnly));
    const QString libSource = QString::fromUtf8(lib.readAll());
    QVERIFY(!libSource.contains(QStringLiteral("vec4 pPointer(vec2")));
    // The helper also pulls in the uniform contract the generated main's
    // finalize hook is declared by.
    QVERIFY(libSource.contains(QStringLiteral("#include <pointer_uniforms.glsl>")));
    QFile uniforms(sharedDir + QStringLiteral("/pointer_uniforms.glsl"));
    QVERIFY(uniforms.open(QIODevice::ReadOnly));
    QVERIFY(QString::fromUtf8(uniforms.readAll()).contains(QStringLiteral("PZ_FINALIZE_COLOR")));

    const auto candidates = PointerShaderRegistry::pointerEntryCandidates();
    QCOMPARE(candidates.size(), 1);
    QCOMPARE(candidates.at(0).functionName, QStringLiteral("pPointer"));
    QVERIFY(candidates.at(0).generatedMain.contains(QStringLiteral("void main()")));
    QVERIFY(
        candidates.at(0).generatedMain.contains(QStringLiteral("fragColor = PZ_FINALIZE_COLOR(pPointer(vTexCoord))")));
}

void TestPointerShaderRegistry::testIncludePathsResolveTheSharedDirectory()
{
    // `#include <pointer_uniforms.glsl>` resolves against the pack root's
    // shared/ dir, one level above the pack itself, and that pair comes FIRST
    // so a pack shipping its own shared/ is served from it.
    const QStringList paths = PointerShaderRegistry::includePathsFor(QStringLiteral("/data/pointer/halo"));
    QVERIFY(paths.size() >= 2);
    QCOMPARE(paths.at(0), QStringLiteral("/data/pointer/shared"));
    QCOMPARE(paths.at(1), QStringLiteral("/data/pointer"));

    // The installed shared dir is appended too, which is the only way a pack
    // outside the bundled tree can resolve the helpers the entry prologue
    // includes unconditionally. Without it every user pack fails include
    // expansion in the preview and the validator while rendering fine in the
    // compositor, which builds its list from the registry's roots instead.
    const QStringList dataDirs = QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation);
    QVERIFY(!dataDirs.isEmpty());
    bool sawInstalledShared = false;
    for (const QString& dir : dataDirs) {
        if (paths.contains(dir + QStringLiteral("/plasmazones/pointer/shared"))) {
            sawInstalledShared = true;
            break;
        }
    }
    QVERIFY2(sawInstalledShared, "includePathsFor drops the installed shared dir, so no user pack can be previewed");

    QVERIFY(PointerShaderRegistry::includePathsFor(QString()).isEmpty());
}

QTEST_MAIN(TestPointerShaderRegistry)
#include "test_pointershaderregistry.moc"
