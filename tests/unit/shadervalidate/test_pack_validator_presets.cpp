// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The offline pack validator's PRESET lints, split out of
// test_pack_validators.cpp when that file crossed the size ceiling. The seam is
// the one that file already drew with its own `Preset lints` banner, not one
// invented to satisfy a line count: a preset is a partial tuning stored against
// a pack, so the only things decidable offline are that its keys name declared
// parameters and that its values fit those parameters' declared types and
// ranges. Everything here asserts exactly one of those, plus the cases that must
// NOT draw a diagnostic.
//
// The pack, parameter, token and buffer lints stayed behind in
// test_pack_validators.cpp. Both files compile all four validator arms, so which
// arm a slot is about is worth stating and each one says so.
#include <QtTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTextStream>

#include <PhosphorAnimation/AnimationShaderContract.h>
#include <PhosphorAnimation/ProfilePaths.h>

#include <PhosphorShaders/ShaderPreset.h>

#include "packvalidatortesthelpers.h"

using namespace PackValidatorTest;

namespace {

/// The overlay twin of `validate`. Writes the pack plus a trivial zone
/// fragment and every buffer pass it declares, so the metadata lints under
/// test are the only thing that can fail. The buffer stages are written from
/// the DECLARED names, empty ones skipped, which is what lets the
/// empty-entry case exercise the lint rather than a missing file.
PackResult validateOverlay(const QTemporaryDir& tmp, const QString& name, const QJsonObject& metadata)
{
    const QString dir = tmp.filePath(name);
    if (!writePackFile(dir, QStringLiteral("metadata.json"), QJsonDocument(metadata).toJson())) {
        return fixtureFailure(QStringLiteral("failed to write metadata.json under ") + dir);
    }

    // Returns bool like the multipass writeBuffer sibling: a silently
    // dropped stage write would surface later as a misleading
    // "buffer shader missing" validator diagnostic instead of a fixture
    // failure.
    const auto writeStage = [&dir](const QString& file) {
        return writePackFile(dir, file, "vec4 pZone(vec2 uv) { return vec4(0.0); }\n");
    };
    bool stagesOk = writeStage(QStringLiteral("zone.frag"));
    for (const QJsonValue& v : metadata.value(QLatin1String("bufferShaders")).toArray()) {
        if (!v.toString().isEmpty()) {
            stagesOk = writeStage(v.toString()) && stagesOk;
        }
    }
    if (!stagesOk) {
        return fixtureFailure(QStringLiteral("failed to write a stage file under ") + dir);
    }

    PackResult result;
    QTextStream stream(&result.report);
    result.errors = PlasmaZones::ShaderValidate::validatePack(dir, stream);
    stream.flush();
    return result;
}

/// `multipass` is set because the buffer lints gate on it, matching the
/// runtime: parseShaderMetadata takes isMultipass from this key alone, so a
/// pack that lists bufferShaders without it is inert and its buffer list is
/// never resolved. A fixture that omitted it would be asserting on a
/// configuration nothing acts on.
QJsonObject overlayPack(const QString& id)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("id"), id);
    obj.insert(QStringLiteral("name"), QStringLiteral("Test Overlay"));
    obj.insert(QStringLiteral("fragmentShader"), QStringLiteral("zone.frag"));
    obj.insert(QStringLiteral("multipass"), true);
    return obj;
}

} // namespace

class TestPackValidatorPresets : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // ── Preset lints ────────────────────────────────────────────────────
    //
    // A preset is a partial tuning stored against a pack, so the only things
    // decidable offline are that its keys name declared parameters and that
    // its values fit those parameters' declared types and ranges. Everything
    // below asserts exactly one of those, plus the two cases that must NOT
    // draw a diagnostic.

    void presetNamingAnUndeclaredParameterIsRejected()
    {
        QTemporaryDir tmp;
        REQUIRE_ANIMATION_FIXTURE(tmp);

        QJsonObject obj = basePack(QStringLiteral("preset-unknown"));
        obj.insert(QStringLiteral("parameters"),
                   QJsonArray{animationParam(QStringLiteral("speed"), QStringLiteral("float"), 1.0)});
        QJsonObject presets;
        presets.insert(QStringLiteral("Odd"), QJsonObject{{QStringLiteral("noSuchThing"), 1.0}});
        obj.insert(QStringLiteral("presets"), presets);

        const PackResult r = validate(tmp, QStringLiteral("preset-unknown"), obj);
        QVERIFY2(r.report.contains(QStringLiteral("which the pack does not declare")), qPrintable(r.report));
        QVERIFY(r.errors > 0);
    }

    void presetValueOutsideTheDeclaredRangeIsRejected()
    {
        QTemporaryDir tmp;
        REQUIRE_ANIMATION_FIXTURE(tmp);

        QJsonObject param = animationParam(QStringLiteral("speed"), QStringLiteral("float"), 1.0);
        param.insert(QStringLiteral("min"), 0.0);
        param.insert(QStringLiteral("max"), 2.0);

        QJsonObject obj = basePack(QStringLiteral("preset-range"));
        obj.insert(QStringLiteral("parameters"), QJsonArray{param});
        QJsonObject presets;
        presets.insert(QStringLiteral("TooFast"), QJsonObject{{QStringLiteral("speed"), 9.0}});
        presets.insert(QStringLiteral("TooSlow"), QJsonObject{{QStringLiteral("speed"), -1.0}});
        obj.insert(QStringLiteral("presets"), presets);

        const PackResult r = validate(tmp, QStringLiteral("preset-range"), obj);
        QVERIFY2(r.report.contains(QStringLiteral("above its declared maximum")), qPrintable(r.report));
        QVERIFY2(r.report.contains(QStringLiteral("below its declared minimum")), qPrintable(r.report));
    }

    void presetValueOfTheWrongTypeIsRejected()
    {
        QTemporaryDir tmp;
        REQUIRE_ANIMATION_FIXTURE(tmp);

        QJsonObject obj = basePack(QStringLiteral("preset-type"));
        obj.insert(QStringLiteral("parameters"),
                   QJsonArray{animationParam(QStringLiteral("count"), QStringLiteral("int"), 1),
                              animationParam(QStringLiteral("on"), QStringLiteral("bool"), true)});
        QJsonObject presets;
        presets.insert(QStringLiteral("Bad"),
                       QJsonObject{{QStringLiteral("count"), QStringLiteral("lots")},
                                   {QStringLiteral("on"), QStringLiteral("yes")}});
        obj.insert(QStringLiteral("presets"), presets);

        const PackResult r = validate(tmp, QStringLiteral("preset-type"), obj);
        QVERIFY2(r.report.contains(QStringLiteral("non-numeric value")), qPrintable(r.report));
        QVERIFY2(r.report.contains(QStringLiteral("a bool parameter wants true or false")), qPrintable(r.report));
    }

    void aValidPresetDrawsNoDiagnostic()
    {
        QTemporaryDir tmp;
        REQUIRE_ANIMATION_FIXTURE(tmp);

        QJsonObject param = animationParam(QStringLiteral("speed"), QStringLiteral("float"), 1.0);
        param.insert(QStringLiteral("min"), 0.0);
        param.insert(QStringLiteral("max"), 2.0);

        QJsonObject obj = basePack(QStringLiteral("preset-good"));
        obj.insert(QStringLiteral("parameters"), QJsonArray{param});
        QJsonObject presets;
        presets.insert(QStringLiteral("Gentle"), QJsonObject{{QStringLiteral("speed"), 0.5}});
        obj.insert(QStringLiteral("presets"), presets);

        const PackResult r = validate(tmp, QStringLiteral("preset-good"), obj);
        // "preset '" is the diagnostic prefix; a bare "preset" would also match
        // the pack name in the report header.
        QVERIFY2(!r.report.contains(QStringLiteral("preset '")), qPrintable(r.report));
        // And a POSITIVE assertion alongside it, because the negative above is
        // satisfied just as well by the lint never running at all, which is how the
        // surface arm stayed unexercised without anyone noticing. A clean pack
        // reports no errors and reaches metadata OK.
        QCOMPARE(r.errors, 0);
        QVERIFY2(r.report.contains(QStringLiteral("metadata       OK")), qPrintable(r.report));
    }

    void aPresetMayOmitParameters()
    {
        // A preset is a PARTIAL tuning by design: what it says nothing about
        // falls back to the parameter's default. Demanding completeness would
        // make the common case (retune one slider, save) impossible to express.
        QTemporaryDir tmp;
        REQUIRE_ANIMATION_FIXTURE(tmp);

        QJsonObject obj = basePack(QStringLiteral("preset-partial"));
        obj.insert(QStringLiteral("parameters"),
                   QJsonArray{animationParam(QStringLiteral("speed"), QStringLiteral("float"), 1.0),
                              animationParam(QStringLiteral("glow"), QStringLiteral("float"), 0.5)});
        QJsonObject presets;
        presets.insert(QStringLiteral("OnlySpeed"), QJsonObject{{QStringLiteral("speed"), 1.5}});
        obj.insert(QStringLiteral("presets"), presets);

        const PackResult r = validate(tmp, QStringLiteral("preset-partial"), obj);
        // "preset '" is the diagnostic prefix; a bare "preset" would also match
        // the pack name in the report header.
        QVERIFY2(!r.report.contains(QStringLiteral("preset '")), qPrintable(r.report));
        QCOMPARE(r.errors, 0);
    }

    void aPresetIdThatIsNotAPathComponentIsLinted()
    {
        // A pack-declared preset's key IS its id, and the settings app builds a
        // filename from an id when the user duplicates one into an editable
        // preset. The parse deliberately KEEPS such a key so this lint can report
        // it: dropping it there would make the validator blind, and the pack would
        // ship green with nothing but a log line no author reads.
        //
        // This lived in the metadata schema as a `propertyNames` rule, where it
        // worked on no family: the vendored JSON-schema validator rejects every
        // name under that keyword, so on the one arm that applies it the rule
        // refused legitimate packs, and the animation gate never consulted it at
        // all — a `"../escape"` preset id shipped clean.
        QTemporaryDir tmp;
        REQUIRE_ANIMATION_FIXTURE(tmp);

        QJsonObject obj = basePack(QStringLiteral("preset-badid"));
        obj.insert(QStringLiteral("parameters"),
                   QJsonArray{animationParam(QStringLiteral("speed"), QStringLiteral("float"), 1.0)});
        QJsonObject presets;
        presets.insert(QStringLiteral("../escape"), QJsonObject{{QStringLiteral("speed"), 1.5}});
        presets.insert(QStringLiteral(".."), QJsonObject{{QStringLiteral("speed"), 1.5}});
        presets.insert(QStringLiteral("Fine"), QJsonObject{{QStringLiteral("speed"), 1.5}});
        obj.insert(QStringLiteral("presets"), presets);

        const PackResult r = validate(tmp, QStringLiteral("preset-badid"), obj);
        QVERIFY2(r.report.contains(QStringLiteral("has an unusable id")), qPrintable(r.report));
        QVERIFY(r.errors >= 2);
        // The usable one beside them is not implicated: one bad id does not
        // condemn the pack's other presets.
        QVERIFY2(!r.report.contains(QStringLiteral("preset 'Fine'")), qPrintable(r.report));
    }

    void anIntPresetValueMustBeIntegralAndAColourMustParse()
    {
        // An int-typed parameter reaches the shader through a cast that
        // TRUNCATES, and an unparseable colour becomes an INVALID QColor, which
        // the uniform receives as transparent black. Both read as "the preset did
        // nothing" rather than "the preset has a typo", and both were invisible:
        // the lint checked an int value was numeric and a colour value was a
        // string, which each bad value here already is.
        QTemporaryDir tmp;
        REQUIRE_ANIMATION_FIXTURE(tmp);

        QJsonObject obj = basePack(QStringLiteral("preset-types"));
        obj.insert(
            QStringLiteral("parameters"),
            QJsonArray{animationParam(QStringLiteral("count"), QStringLiteral("int"), 4),
                       animationParam(QStringLiteral("tint"), QStringLiteral("color"), QStringLiteral("#112233"))});
        QJsonObject presets;
        presets.insert(QStringLiteral("Sloppy"),
                       QJsonObject{{QStringLiteral("count"), 1.5}, {QStringLiteral("tint"), QStringLiteral("ochre")}});
        presets.insert(QStringLiteral("Tidy"),
                       QJsonObject{{QStringLiteral("count"), 3}, {QStringLiteral("tint"), QStringLiteral("#445566")}});
        obj.insert(QStringLiteral("presets"), presets);

        const PackResult r = validate(tmp, QStringLiteral("preset-types"), obj);
        QVERIFY2(r.report.contains(QStringLiteral("truncates to 1")), qPrintable(r.report));
        QVERIFY2(r.report.contains(QStringLiteral("not a colour QColor can parse")), qPrintable(r.report));
        QVERIFY(r.errors >= 2);
        // The well-formed preset beside them reports nothing, so neither check is
        // merely firing on every value it sees.
        QVERIFY2(!r.report.contains(QStringLiteral("preset 'Tidy'")), qPrintable(r.report));
    }

    void theRemainingPresetLintBranchesFire()
    {
        // Three branches of the shared preset lint that no slot reached, so each
        // could be deleted with the suite green.
        //
        // 1. The id lint's LENGTH half. Every id case above is refused by
        //    `isUsableId` for its characters, so `|| size > MaxNameChars` never
        //    decided anything. A 200-character key is a real authoring mistake:
        //    the key is also the picker's label, and MaxNameChars truncation runs
        //    only for USER presets, so it renders in full and mangles the row.
        // 2. The int parameter's OWN range, independent of any declared one. The
        //    declared bounds are optional, so `1e18` under an int parameter with
        //    no min/max linted clean and then hit a static_cast<int>, which is
        //    undefined behaviour rather than a clamp.
        // 3. The non-string branch for colour and image values. A number under a
        //    colour parameter cannot be parsed as a colour name at all, and the
        //    `isValidColorName` check above it only ever sees strings.
        QTemporaryDir tmp;
        REQUIRE_ANIMATION_FIXTURE(tmp);

        const QString longId = QString(200, QLatin1Char('n'));
        QJsonObject obj = basePack(QStringLiteral("preset-branches"));
        obj.insert(
            QStringLiteral("parameters"),
            QJsonArray{animationParam(QStringLiteral("count"), QStringLiteral("int"), 4),
                       animationParam(QStringLiteral("tint"), QStringLiteral("color"), QStringLiteral("#112233"))});
        QJsonObject presets;
        presets.insert(longId, QJsonObject{{QStringLiteral("count"), 2}});
        presets.insert(QStringLiteral("Huge"), QJsonObject{{QStringLiteral("count"), 1e18}});
        presets.insert(QStringLiteral("NotAColour"), QJsonObject{{QStringLiteral("tint"), 7}});
        // A non-finite value is deliberately NOT among these: Qt's JSON parser refuses
        // one (`1e400` fails the whole document with "illegal number", verified with a
        // probe), so such a pack never loads and there is nothing for a lint to catch.
        presets.insert(QStringLiteral("Tidy"), QJsonObject{{QStringLiteral("count"), 3}});
        obj.insert(QStringLiteral("presets"), presets);

        const PackResult r = validate(tmp, QStringLiteral("preset-branches"), obj);
        // The 200-character key trips the LENGTH arm, which is a separate message
        // from the unusable-id one: the registry truncates the rendered name but
        // keeps the full key as the id, so the fault is a collision risk rather
        // than an unreadable row.
        QVERIFY2(r.report.contains(QStringLiteral("has a 200-character id")), qPrintable(r.report));
        QVERIFY2(r.report.contains(QStringLiteral("does not fit in an int parameter")), qPrintable(r.report));
        QVERIFY2(r.report.contains(QStringLiteral("to a non-color value")), qPrintable(r.report));
        // EXACTLY three, not "at least": the count is knowable, and `>=` is satisfied
        // by a lint that fires on everything.
        QCOMPARE(r.errors, 3);
        // And the clean preset beside them is untouched, so none of the three is
        // firing on everything it sees.
        QVERIFY2(!r.report.contains(QStringLiteral("preset 'Tidy'")), qPrintable(r.report));
    }

    void thePresetIdLengthBoundIsExact()
    {
        // The id lint's length half at its boundary. The slot above drives it with a
        // 200-character key, which `> MaxNameChars` and `>= MaxNameChars` both refuse,
        // so nothing stopped the comparison from drifting by one.
        QTemporaryDir tmp;
        REQUIRE_ANIMATION_FIXTURE(tmp);

        const qsizetype cap = PhosphorShaders::ShaderPreset::MaxNameChars;
        QJsonObject obj = basePack(QStringLiteral("preset-idcap"));
        obj.insert(QStringLiteral("parameters"),
                   QJsonArray{animationParam(QStringLiteral("speed"), QStringLiteral("float"), 1.0)});
        QJsonObject presets;
        presets.insert(QString(cap, QLatin1Char('n')), QJsonObject{{QStringLiteral("speed"), 1.5}});
        obj.insert(QStringLiteral("presets"), presets);
        const PackResult atCap = validate(tmp, QStringLiteral("preset-idcap"), obj);
        QVERIFY2(!atCap.report.contains(QStringLiteral("-character id")), qPrintable(atCap.report));
        QVERIFY2(!atCap.report.contains(QStringLiteral("has an unusable id")), qPrintable(atCap.report));

        QJsonObject over;
        over.insert(QString(cap + 1, QLatin1Char('n')), QJsonObject{{QStringLiteral("speed"), 1.5}});
        obj.insert(QStringLiteral("presets"), over);
        const PackResult overCap = validate(tmp, QStringLiteral("preset-idcap"), obj);
        QVERIFY2(overCap.report.contains(QStringLiteral("-character id")), qPrintable(overCap.report));
    }

    void anUnusableIdStillGetsItsValuesChecked()
    {
        // The id branch reports and carries on rather than skipping the preset's
        // values: they are independent of the key, so an author who fixes the name
        // should not then get a fresh round of value errors.
        QTemporaryDir tmp;
        REQUIRE_ANIMATION_FIXTURE(tmp);

        QJsonObject obj = basePack(QStringLiteral("preset-both"));
        obj.insert(QStringLiteral("parameters"),
                   QJsonArray{animationParam(QStringLiteral("count"), QStringLiteral("int"), 4)});
        QJsonObject presets;
        presets.insert(QString(200, QLatin1Char('n')), QJsonObject{{QStringLiteral("count"), 1.5}});
        obj.insert(QStringLiteral("presets"), presets);

        const PackResult r = validate(tmp, QStringLiteral("preset-both"), obj);
        QVERIFY2(r.report.contains(QStringLiteral("has a 200-character id")), qPrintable(r.report));
        QVERIFY2(r.report.contains(QStringLiteral("truncates to 1")), qPrintable(r.report));
        QCOMPARE(r.errors, 2);
    }

    void theRawPresetLintsCountTheWayTheLoaderCounts()
    {
        // The caps exist to say what the loader will DROP, so counting anything the
        // loader does not count makes the lint assert a drop that never happens.
        // parsePackPresets increments its preset budget only after the object-shape
        // check, and its per-preset budget tests the map it is building, which a JSON
        // null never enters.
        QTemporaryDir tmp;
        REQUIRE_ANIMATION_FIXTURE(tmp);

        QJsonObject obj = basePack(QStringLiteral("preset-counting"));
        obj.insert(QStringLiteral("parameters"),
                   QJsonArray{animationParam(QStringLiteral("speed"), QStringLiteral("float"), 1.0)});

        // 64 usable presets plus one non-object body. The loader keeps all 64 and
        // drops nothing, so the cap lint must stay silent — but the malformed body
        // is now named, which nothing reported before.
        QJsonObject presets;
        for (int i = 0; i < 64; ++i) {
            presets.insert(QStringLiteral("p%1").arg(i), QJsonObject{{QStringLiteral("speed"), 1.5}});
        }
        presets.insert(QStringLiteral("Bad"), QJsonValue(7));
        obj.insert(QStringLiteral("presets"), presets);
        PackResult r = validate(tmp, QStringLiteral("preset-counting"), obj);
        QVERIFY2(!r.report.contains(QStringLiteral("only the first 64 are loaded and the rest are dropped")),
                 qPrintable(r.report));
        QVERIFY2(r.report.contains(QStringLiteral("preset 'Bad' is not an object")), qPrintable(r.report));

        // Same one level down: 64 real values beside a null. The null costs no slot,
        // so nothing is dropped, and the null itself is what gets reported.
        QJsonObject fat;
        for (int i = 0; i < 64; ++i) {
            fat.insert(QStringLiteral("v%1").arg(i), 1.0);
        }
        fat.insert(QStringLiteral("nulled"), QJsonValue());
        obj.insert(QStringLiteral("presets"), QJsonObject{{QStringLiteral("Fat"), fat}});
        r = validate(tmp, QStringLiteral("preset-counting"), obj);
        QVERIFY2(!r.report.contains(QStringLiteral("values; only the first")), qPrintable(r.report));
        QVERIFY2(r.report.contains(QStringLiteral("sets 'nulled' to null")), qPrintable(r.report));
    }

    void aPercentInAPresetKeyDoesNotRewriteTheMessage()
    {
        // The preset key is author-controlled and lands in %1. A CHAINED .arg
        // substitutes it and then searches the RESULT, so a key carrying its own
        // marker had that marker replaced by the next argument, renaming the preset
        // the author has to go and fix.
        QTemporaryDir tmp;
        REQUIRE_ANIMATION_FIXTURE(tmp);

        QJsonObject obj = basePack(QStringLiteral("preset-percent"));
        obj.insert(QStringLiteral("parameters"),
                   QJsonArray{animationParam(QStringLiteral("speed"), QStringLiteral("float"), 1.0)});
        QJsonObject fat;
        for (int i = 0; i < 65; ++i) {
            fat.insert(QStringLiteral("v%1").arg(i), 1.0);
        }
        obj.insert(QStringLiteral("presets"), QJsonObject{{QStringLiteral("%3 mode"), fat}});
        const PackResult r = validate(tmp, QStringLiteral("preset-percent"), obj);
        QVERIFY2(r.report.contains(QStringLiteral("preset '%3 mode' sets 65 values")), qPrintable(r.report));
    }

    void thePresetsHeaderIsPrintedOnce()
    {
        // The three collectors used to print their own header, so a pack tripping two
        // of them announced "presets ERROR" twice in one report.
        QTemporaryDir tmp;
        REQUIRE_ANIMATION_FIXTURE(tmp);

        QJsonObject obj = basePack(QStringLiteral("preset-onehdr"));
        obj.insert(QStringLiteral("parameters"),
                   QJsonArray{animationParam(QStringLiteral("speed"), QStringLiteral("float"), 1.0)});
        // A raw-block fault and a parsed-map fault together.
        QJsonObject presets;
        presets.insert(QStringLiteral("NotAnObject"), QJsonValue(7));
        presets.insert(QStringLiteral("Undeclared"), QJsonObject{{QStringLiteral("nope"), 1.0}});
        obj.insert(QStringLiteral("presets"), presets);
        const PackResult r = validate(tmp, QStringLiteral("preset-onehdr"), obj);
        QVERIFY2(r.report.contains(QStringLiteral("is not an object, so it is dropped")), qPrintable(r.report));
        QVERIFY2(r.report.contains(QStringLiteral("which the pack does not declare")), qPrintable(r.report));
        QCOMPARE(r.report.count(QStringLiteral("presets        ERROR")), 1);
    }

    void theRawPresetsBlockIsLintedForWhatTheParseHides()
    {
        // Three faults the PARSED map cannot show, because by then they have already
        // happened: a non-object `presets` is ignored wholesale (the pack ships none), and
        // both loader caps truncate silently. Each cost the author presets with only a log
        // line, and the shared lint receives the result rather than the declaration.
        QTemporaryDir tmp;
        REQUIRE_ANIMATION_FIXTURE(tmp);

        // A non-object block.
        QJsonObject obj = basePack(QStringLiteral("raw-presets"));
        obj.insert(QStringLiteral("parameters"),
                   QJsonArray{animationParam(QStringLiteral("speed"), QStringLiteral("float"), 1.0)});
        obj.insert(QStringLiteral("presets"), QJsonArray{});
        PackResult r = validate(tmp, QStringLiteral("raw-presets"), obj);
        QVERIFY2(r.report.contains(QStringLiteral("`presets` is not an object")), qPrintable(r.report));
        QCOMPARE(r.errors, 1);

        // More presets than the loader keeps.
        QJsonObject many;
        for (int i = 0; i < 70; ++i) {
            many.insert(QStringLiteral("p%1").arg(i), QJsonObject{{QStringLiteral("speed"), 1.5}});
        }
        obj.insert(QStringLiteral("presets"), many);
        r = validate(tmp, QStringLiteral("raw-presets"), obj);
        QVERIFY2(r.report.contains(QStringLiteral("declares 70 presets")), qPrintable(r.report));

        // More values in one preset than the loader keeps. The declared-parameter lints
        // fire for the undeclared ids too, so assert on this diagnostic rather than a count.
        QJsonObject fat;
        for (int i = 0; i < 70; ++i) {
            fat.insert(QStringLiteral("v%1").arg(i), 1.0);
        }
        obj.insert(QStringLiteral("presets"), QJsonObject{{QStringLiteral("Fat"), fat}});
        r = validate(tmp, QStringLiteral("raw-presets"), obj);
        QVERIFY2(r.report.contains(QStringLiteral("sets 70 values")), qPrintable(r.report));

        // And a well-formed block draws none of the three.
        obj.insert(QStringLiteral("presets"),
                   QJsonObject{{QStringLiteral("Fine"), QJsonObject{{QStringLiteral("speed"), 1.5}}}});
        r = validate(tmp, QStringLiteral("raw-presets"), obj);
        QVERIFY2(!r.report.contains(QStringLiteral("is not an object")), qPrintable(r.report));
        QVERIFY2(!r.report.contains(QStringLiteral("presets;")), qPrintable(r.report));
        QCOMPARE(r.errors, 0);
    }

    void theOverlayArmLintsPresetsToo()
    {
        // The preset lint is wired into all four validator arms, but every test
        // above drives the ANIMATION one — so deleting the call from the overlay,
        // surface or pointer arm left the suite green. This covers the overlay arm
        // through the harness that was already sitting in this file unused.
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        QJsonObject param;
        param.insert(QStringLiteral("id"), QStringLiteral("speed"));
        // `name` is required by the overlay metadata schema, which the overlay arm
        // validates before it reaches any lint — without it the pack fails schema
        // validation and the preset lint is never consulted at all.
        param.insert(QStringLiteral("name"), QStringLiteral("Speed"));
        param.insert(QStringLiteral("type"), QStringLiteral("float"));
        param.insert(QStringLiteral("default"), 1.0);
        param.insert(QStringLiteral("min"), 0.0);
        param.insert(QStringLiteral("max"), 2.0);

        QJsonObject obj = overlayPack(QStringLiteral("ov-preset"));
        obj.insert(QStringLiteral("multipass"), false);
        obj.insert(QStringLiteral("parameters"), QJsonArray{param});
        QJsonObject presets;
        presets.insert(QStringLiteral("Undeclared"), QJsonObject{{QStringLiteral("noSuchThing"), 1.0}});
        presets.insert(QStringLiteral("TooFast"), QJsonObject{{QStringLiteral("speed"), 99.0}});
        obj.insert(QStringLiteral("presets"), presets);

        const PackResult r = validateOverlay(tmp, QStringLiteral("ov-preset"), obj);
        QVERIFY2(r.report.contains(QStringLiteral("which the pack does not declare")), qPrintable(r.report));
        QVERIFY2(r.report.contains(QStringLiteral("above its declared maximum")), qPrintable(r.report));
        QVERIFY(r.errors > 0);
    }

    void theOverlayArmLintsAPresetImagePath()
    {
        // No overlay test declared an image parameter, so the whole image branch of
        // the preset lint was uncovered. Writing this one is what showed the
        // containment half of it to be DEAD: parsePackPresets refuses an escaping
        // value and drops the entry, so the lint never sees it. Both halves are
        // asserted here — the dropped values produce no report line, which is the
        // behaviour to notice rather than the behaviour to want, and the existence
        // check catches the case that does survive the parse.
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        QJsonObject param;
        param.insert(QStringLiteral("id"), QStringLiteral("tex"));
        param.insert(QStringLiteral("name"), QStringLiteral("Texture"));
        param.insert(QStringLiteral("type"), QStringLiteral("image"));
        param.insert(QStringLiteral("default"), QString());

        QJsonObject obj = overlayPack(QStringLiteral("ov-preset-image"));
        obj.insert(QStringLiteral("multipass"), false);
        obj.insert(QStringLiteral("parameters"), QJsonArray{param});
        QJsonObject presets;
        presets.insert(QStringLiteral("Escaping"),
                       QJsonObject{{QStringLiteral("tex"), QStringLiteral("../../../etc/passwd")}});
        presets.insert(QStringLiteral("Absolute"), QJsonObject{{QStringLiteral("tex"), QStringLiteral("/etc/passwd")}});
        // A path inside the pack that simply is not there. Also a silent no-op at
        // runtime (the parameter falls back to its default), and also unreported
        // until now.
        presets.insert(QStringLiteral("Missing"), QJsonObject{{QStringLiteral("tex"), QStringLiteral("absent.png")}});
        // Empty is legitimate: "no texture for this slot".
        presets.insert(QStringLiteral("None"), QJsonObject{{QStringLiteral("tex"), QString()}});
        obj.insert(QStringLiteral("presets"), presets);

        const PackResult r = validateOverlay(tmp, QStringLiteral("ov-preset-image"), obj);
        // The in-pack path that does not exist IS reported. This is the half of the
        // image branch that can fire.
        QVERIFY2(r.report.contains(QStringLiteral("preset 'Missing'")), qPrintable(r.report));
        QVERIFY2(r.report.contains(QStringLiteral("names no file the pack ships")), qPrintable(r.report));
        QVERIFY(r.errors >= 1);
        // The two ESCAPING paths are not reported, and that is the parse's doing, not
        // a miss in the lint: parsePackPresets refuses them and drops the entries, so
        // nothing reaches the parsed map to lint. Pinned so the next reader does not
        // "restore" a containment check here that can never fire — and so that if
        // refusals are ever surfaced out of the parse, this assertion fails and points
        // at the report line that should then exist.
        QVERIFY2(!r.report.contains(QStringLiteral("preset 'Escaping'")), qPrintable(r.report));
        QVERIFY2(!r.report.contains(QStringLiteral("preset 'Absolute'")), qPrintable(r.report));
        // The legitimate empty one is silent too, so the existence check is not simply
        // firing on every image-typed value it sees.
        QVERIFY2(!r.report.contains(QStringLiteral("preset 'None'")), qPrintable(r.report));
    }

    void presetProblemsPrintUnderTheirOwnHeader()
    {
        // A preset fault used to print an unindented line and then leave the
        // metadata section reporting OK directly below it, while still counting
        // the error — a report that contradicted itself.
        QTemporaryDir tmp;
        REQUIRE_ANIMATION_FIXTURE(tmp);

        QJsonObject obj = basePack(QStringLiteral("preset-header"));
        obj.insert(QStringLiteral("parameters"),
                   QJsonArray{animationParam(QStringLiteral("speed"), QStringLiteral("float"), 1.0)});
        QJsonObject presets;
        presets.insert(QStringLiteral("Odd"), QJsonObject{{QStringLiteral("noSuchThing"), 1.0}});
        obj.insert(QStringLiteral("presets"), presets);

        const PackResult r = validate(tmp, QStringLiteral("preset-header"), obj);
        QVERIFY2(r.report.contains(QStringLiteral("presets        ERROR")), qPrintable(r.report));
        // Indented under that header, like every sibling lint.
        QVERIFY2(r.report.contains(QStringLiteral("    preset 'Odd'")), qPrintable(r.report));
    }
};

QTEST_MAIN(TestPackValidatorPresets)
#include "test_pack_validator_presets.moc"
