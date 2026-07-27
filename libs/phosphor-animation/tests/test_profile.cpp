// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/CurveRegistry.h>
#include <PhosphorAnimation/Easing.h>
#include <PhosphorAnimation/Profile.h>
#include <PhosphorAnimation/Spring.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTest>

#include <limits>

using PhosphorAnimation::Curve;
using PhosphorAnimation::CurveRegistry;
using PhosphorAnimation::Easing;
using PhosphorAnimation::Profile;
using PhosphorAnimation::SequenceMode;
using PhosphorAnimation::Spring;

class TestProfile : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void testDefaultAllOptionalsUnset()
    {
        Profile p;
        QVERIFY(p.curve == nullptr); // null = "inherit"
        QVERIFY(!p.duration.has_value());
        QVERIFY(!p.minDistance.has_value());
        QVERIFY(!p.sequenceMode.has_value());
        QVERIFY(!p.staggerInterval.has_value());
        QVERIFY(!p.presetName.has_value());
    }

    void testEffectiveGettersUseLibraryDefaults()
    {
        // An unset Profile reads the library defaults through the
        // effective* accessors — so consumers never see a raw nullopt.
        Profile p;
        QCOMPARE(p.effectiveDuration(), Profile::DefaultDuration);
        QCOMPARE(p.effectiveMinDistance(), Profile::DefaultMinDistance);
        QCOMPARE(p.effectiveSequenceMode(), Profile::DefaultSequenceMode);
        QCOMPARE(p.effectiveStaggerInterval(), Profile::DefaultStaggerInterval);
    }

    void testWithDefaultsFillsDefaultableOptionals()
    {
        Profile empty;
        const Profile filled = empty.withDefaults();
        QVERIFY(filled.duration.has_value());
        QVERIFY(filled.minDistance.has_value());
        QVERIFY(filled.sequenceMode.has_value());
        QVERIFY(filled.staggerInterval.has_value());
        // curve is filled with a library-default OutCubic Easing — callers
        // that pass through withDefaults() get a fully-populated Profile.
        // The "curve left null" shape from before was a trap where
        // downstream code still had to null-check.
        QVERIFY(filled.curve != nullptr);
        QCOMPARE(filled.curve->typeId(), QStringLiteral("bezier"));
        // presetName is the ONE field withDefaults() does NOT backfill: it has
        // no library default, so it stays disengaged and callers must keep
        // treating it as optional (value_or), never dereference it blind. Both
        // Profile::withDefaults() and ProfileTree::resolve() document that
        // exception, so pin it — a future backfill with QString() would satisfy
        // every other assertion here while silently collapsing the
        // engaged-empty ("explicit clear") vs unset ("inherit") distinction the
        // overlay cascade depends on.
        QVERIFY(!filled.presetName.has_value());
    }

    void testWithDefaultsPreservesExistingCurve()
    {
        Profile p;
        auto spring = std::make_shared<Spring>(Spring::bouncy());
        p.curve = spring;
        const Profile filled = p.withDefaults();
        // An existing curve must not be replaced by the library default.
        QCOMPARE(filled.curve.get(), spring.get());
    }

    // ─── Equality ───

    void testEqualityWithSameValues()
    {
        Profile a;
        Profile b;
        QCOMPARE(a, b);

        a.curve = std::make_shared<Spring>(Spring::snappy());
        b.curve = std::make_shared<Spring>(Spring::snappy());
        QCOMPARE(a, b); // equal curves are value-equal even if different shared_ptrs
    }

    void testEqualityDifferentCurveSubclass()
    {
        Profile a;
        a.curve = std::make_shared<Easing>();
        Profile b;
        b.curve = std::make_shared<Spring>();
        QVERIFY(a != b);
    }

    void testEqualityNullVsConcrete()
    {
        Profile a;
        Profile b;
        b.curve = std::make_shared<Easing>();
        QVERIFY(a != b);
    }

    void testEqualityUnsetVsExplicitDefault()
    {
        // A Profile with duration unset is NOT equal to one with
        // duration explicitly set to the default. This is the whole
        // point of optional-based fields — "I didn't say" and "I said
        // the default" are different.
        Profile unset;
        Profile explicitDefault;
        explicitDefault.duration = Profile::DefaultDuration;
        QVERIFY(unset != explicitDefault);
    }

    void testEqualityFieldsOnly()
    {
        Profile a;
        Profile b;
        a.duration = 300.0;
        QVERIFY(a != b);
        b.duration = 300.0;
        QCOMPARE(a, b);
    }

    void testEqualityPolymorphicCurveUsesTightComparison()
    {
        // Two Easings differing below toString() precision (0.005) must
        // compare unequal through the polymorphic curve->equals() path,
        // not equal via a lossy string round-trip.
        auto a = std::make_shared<Easing>();
        a->type = Easing::Type::ElasticOut;
        a->amplitude = 1.003;
        auto b = std::make_shared<Easing>();
        b->type = Easing::Type::ElasticOut;
        b->amplitude = 1.006;

        Profile pa;
        pa.curve = a;
        Profile pb;
        pb.curve = b;
        QVERIFY(pa != pb);
    }

    // ─── Serialization ───

    void testToJsonOmitsUnsetFields()
    {
        Profile p;
        const QJsonObject obj = p.toJson();
        // Every optional field is unset → omitted. Only an empty object.
        QVERIFY(!obj.contains(QLatin1String("curve")));
        QVERIFY(!obj.contains(QLatin1String("duration")));
        QVERIFY(!obj.contains(QLatin1String("minDistance")));
        QVERIFY(!obj.contains(QLatin1String("sequenceMode")));
        QVERIFY(!obj.contains(QLatin1String("staggerInterval")));
        QVERIFY(!obj.contains(QLatin1String("presetName")));
    }

    void testToJsonIncludesExplicitlySetDefault()
    {
        // "duration = 150" (explicitly set, even if equal to library
        // default) MUST appear in JSON — otherwise the override is
        // silently dropped and the parent wins when reloaded.
        Profile p;
        p.duration = Profile::DefaultDuration;
        const QJsonObject obj = p.toJson();
        QVERIFY(obj.contains(QLatin1String("duration")));
    }

    void testRoundTripEasing()
    {
        Profile original;
        original.curve = std::make_shared<Easing>();
        original.duration = 250.0;
        original.minDistance = 15;
        original.sequenceMode = SequenceMode::Cascade;
        original.staggerInterval = 45;
        original.presetName = QStringLiteral("My Fast");

        const Profile restored = Profile::fromJson(original.toJson(), CurveRegistry{});
        QCOMPARE(restored, original);
    }

    // NOTE: no parent and no tree here — this is the JSON round-trip alone.
    // test_profiletree.cpp carries the identically-shaped case that DOES exercise
    // a parent override.
    void testPresetNameEngagedEmptyRoundTripsInJson()
    {
        // Regression: presetName is std::optional<QString>, so an engaged
        // empty-string override is distinct from a nullopt "inherit".
        // `toJson` must emit an engaged-but-empty name; `fromJson` must
        // round-trip it back to engaged-empty, not drop it as "unset".
        Profile p;
        p.presetName = QString();
        const QJsonObject obj = p.toJson();
        QVERIFY(obj.contains(QLatin1String("presetName")));

        const Profile restored = Profile::fromJson(obj, CurveRegistry{});
        QVERIFY(restored.presetName.has_value());
        QVERIFY(restored.presetName->isEmpty());
    }

    void testPresetNameAbsentVsEngagedEmptyDistinguishedInJson()
    {
        // Belt-and-braces around the QJsonObject "" vs missing-key
        // contract: a Profile with no presetName must round-trip with
        // the field absent; a Profile with engaged-empty presetName
        // must round-trip with the field present-and-empty. Equality
        // distinguishes the two cases, which is the whole point of
        // optional-bearing fields.
        Profile absent;
        Profile engagedEmpty;
        engagedEmpty.presetName = QString();

        QVERIFY(absent != engagedEmpty);

        const Profile absentRestored = Profile::fromJson(absent.toJson(), CurveRegistry{});
        const Profile engagedEmptyRestored = Profile::fromJson(engagedEmpty.toJson(), CurveRegistry{});

        QVERIFY(!absentRestored.presetName.has_value());
        QVERIFY(engagedEmptyRestored.presetName.has_value());
        QVERIFY(absentRestored != engagedEmptyRestored);
    }

    void testRoundTripSpring()
    {
        Profile original;
        original.curve = std::make_shared<Spring>(Spring::bouncy());
        original.duration = 400.0;

        const Profile restored = Profile::fromJson(original.toJson(), CurveRegistry{});
        QCOMPARE(restored.duration, original.duration);
        QVERIFY(restored.curve != nullptr);
        QCOMPARE(restored.curve->typeId(), QStringLiteral("spring"));
        QVERIFY(restored.curve->equals(*original.curve));
    }

    void testFromJsonMissingFieldsLeaveUnset()
    {
        QJsonObject obj;
        obj.insert(QLatin1String("duration"), 999.0);
        const Profile p = Profile::fromJson(obj, CurveRegistry{});
        QCOMPARE(*p.duration, 999.0);
        QVERIFY(!p.minDistance.has_value());
        QVERIFY(!p.staggerInterval.has_value());
        QVERIFY(p.curve == nullptr);
    }

    void testFromJsonSequenceModeClamps()
    {
        // Unknown integer values fall back to the library default enum.
        QJsonObject obj;
        obj.insert(QLatin1String("sequenceMode"), 99);
        const Profile p = Profile::fromJson(obj, CurveRegistry{});
        QCOMPARE(*p.sequenceMode, Profile::DefaultSequenceMode);
    }

    // ─── Input validation (H1) ───

    void testFromJsonRejectsNegativeDuration()
    {
        QJsonObject obj;
        obj.insert(QLatin1String("duration"), -150.0);
        const Profile p = Profile::fromJson(obj, CurveRegistry{});
        // Negative duration rejected → duration stays unset → effective
        // reads library default.
        QVERIFY(!p.duration.has_value());
        QCOMPARE(p.effectiveDuration(), Profile::DefaultDuration);
    }

    void testFromJsonRejectsZeroDuration()
    {
        QJsonObject obj;
        obj.insert(QLatin1String("duration"), 0.0);
        const Profile p = Profile::fromJson(obj, CurveRegistry{});
        QVERIFY(!p.duration.has_value());
    }

    void testFromJsonRejectsNonFiniteDuration()
    {
        // JSON parsers typically reject NaN/Infinity as JSON numbers,
        // but QJsonValue::toDouble accepts a default that a caller
        // could in theory coerce. Guard via the explicit isfinite()
        // check — this covers any downstream where a malformed value
        // sneaks in.
        QJsonObject obj;
        obj.insert(QLatin1String("duration"), QJsonValue(std::numeric_limits<double>::infinity()));
        const Profile p = Profile::fromJson(obj, CurveRegistry{});
        QVERIFY(!p.duration.has_value());
    }

    void testFromJsonRejectsAbsurdlyLargeDuration()
    {
        QJsonObject obj;
        // Two hours — past the 1-hour sanity bound. Accepting would
        // risk int-overflow via qRound() downstream.
        obj.insert(QLatin1String("duration"), 2.0 * 60.0 * 60.0 * 1000.0);
        const Profile p = Profile::fromJson(obj, CurveRegistry{});
        QVERIFY(!p.duration.has_value());
    }

    /// The ACCEPTED side of the duration boundary, matching the minDistance
    /// and staggerInterval at-cap twins: without it, flipping the validator's
    /// `>` to `>=` would reject exactly MaxDurationMs and every test would
    /// still pass.
    void testFromJsonAcceptsDurationAtTheCap()
    {
        QJsonObject obj;
        obj.insert(QLatin1String("duration"), double(Profile::MaxDurationMs));
        const Profile p = Profile::fromJson(obj, CurveRegistry{});
        QVERIFY(p.duration.has_value());
        QCOMPARE(*p.duration, Profile::MaxDurationMs);
    }

    /// The unresolved-curve branch: a spec `tryCreate` cannot resolve leaves
    /// `curve` UNSET (so it inherits) rather than substituting anything, and
    /// warns with the original spec. The sibling fields still parse. Deleting
    /// the tryCreate routing (falling back to create()'s silent default
    /// cubic-bezier) reddens the null check; deleting the diagnostic reddens
    /// ignoreMessage. NOTE for future editors: the rate limiter is
    /// process-wide, so this slot must stay the only one in this binary
    /// feeding this exact spec.
    void testFromJsonLeavesUnresolvableCurveUnsetAndWarns()
    {
        QJsonObject obj;
        obj.insert(QLatin1String("curve"), QStringLiteral("srping:14,0.6"));
        obj.insert(QLatin1String("duration"), 250.0);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("did not resolve")));
        const Profile p = Profile::fromJson(obj, CurveRegistry{});
        QVERIFY(!p.curve);
        QVERIFY(p.duration.has_value());
        QCOMPARE(*p.duration, 250.0);
    }

    void testFromJsonAcceptsLargeButReasonableDuration()
    {
        QJsonObject obj;
        obj.insert(QLatin1String("duration"), 5000.0);
        const Profile p = Profile::fromJson(obj, CurveRegistry{});
        QVERIFY(p.duration.has_value());
        QCOMPARE(*p.duration, 5000.0);
    }

    void testFromJsonRejectsNegativeMinDistance()
    {
        QJsonObject obj;
        obj.insert(QLatin1String("minDistance"), -5);
        const Profile p = Profile::fromJson(obj, CurveRegistry{});
        QVERIFY(!p.minDistance.has_value());
        QCOMPARE(p.effectiveMinDistance(), Profile::DefaultMinDistance);
    }

    /// The bound is checked BEFORE the round. `qRound` on a value outside the
    /// int range is undefined behaviour, and this value comes from a file a user
    /// can hand-edit, so the old post-round check only worked by accident — on
    /// the platform where the UB conversion happens to pin to INT_MIN, which the
    /// negative test then caught. Deleting the range clause makes this row hit
    /// that UB again.
    void testFromJsonRejectsOutOfRangeMinDistance_data()
    {
        QTest::addColumn<double>("raw");
        QTest::newRow("astronomical") << 1e300;
        QTest::newRow("just over the domain cap") << double(Profile::MaxMinDistancePx) + 1.0;
        QTest::newRow("infinity") << std::numeric_limits<double>::infinity();
        QTest::newRow("nan") << std::numeric_limits<double>::quiet_NaN();
    }

    void testFromJsonRejectsOutOfRangeMinDistance()
    {
        QFETCH(double, raw);
        QJsonObject obj;
        obj.insert(QLatin1String("minDistance"), raw);
        const Profile p = Profile::fromJson(obj, CurveRegistry{});
        QVERIFY(!p.minDistance.has_value());
        QCOMPARE(p.effectiveMinDistance(), Profile::DefaultMinDistance);
    }

    /// The ACCEPTED side of the same boundary. Without it, flipping the
    /// validator's `>` to `>=` would reject exactly MaxMinDistancePx and every
    /// test would still pass.
    void testFromJsonAcceptsMinDistanceAtTheCap()
    {
        QJsonObject obj;
        obj.insert(QLatin1String("minDistance"), double(Profile::MaxMinDistancePx));
        const Profile p = Profile::fromJson(obj, CurveRegistry{});
        QVERIFY(p.minDistance.has_value());
        QCOMPARE(*p.minDistance, Profile::MaxMinDistancePx);
    }

    /// Same shape for sequenceMode, which is finiteness-checked AND
    /// range-checked. Unlike minDistance it lands on the library default
    /// ENGAGED rather than being left unset, so it blocks inheritance instead
    /// of allowing it. That asymmetry is `Profile::fromJson`'s, and
    /// `sanitizedProfileMap` mirrors it.
    void testFromJsonSubstitutesDefaultForOutOfRangeSequenceMode()
    {
        QJsonObject obj;
        obj.insert(QLatin1String("sequenceMode"), 1e300);
        // The DIAGNOSTIC is the load-bearing assertion, and it has to be the
        // out-of-range one specifically. Deleting the range guard sends 1e300
        // into `qRound` as undefined behaviour; in a Release build (where Qt's
        // own Q_ASSERT is compiled out) that yields INT_MIN, which reaches the
        // unknown-enumerator branch and produces this same sequenceMode. So
        // matching on "unknown sequenceMode" would pass either way. Only
        // "rejecting sequenceMode" is a string the unguarded path can never
        // produce.
        //
        // One note for whoever mutation-tests ANY of the four range guards in
        // this file (duration, minDistance, sequenceMode, staggerInterval): in
        // a DEBUG build a guard-deleted path aborts on Qt's own Q_ASSERT inside
        // qCheckedFPConversionToInteger rather than failing cleanly, which
        // kills the whole binary and takes every slot after it with it. The
        // truncated run is collateral, not N more broken slots.
        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression(QStringLiteral("rejecting sequenceMode .*out of int range")));
        const Profile p = Profile::fromJson(obj, CurveRegistry{});
        QVERIFY(p.sequenceMode.has_value());
        QCOMPARE(*p.sequenceMode, Profile::DefaultSequenceMode);
    }

    void testFromJsonAcceptsZeroMinDistance()
    {
        // Zero = "no skip threshold" — the documented default. Must
        // be accepted as a valid explicit setting distinct from
        // "unset".
        QJsonObject obj;
        obj.insert(QLatin1String("minDistance"), 0);
        const Profile p = Profile::fromJson(obj, CurveRegistry{});
        QVERIFY(p.minDistance.has_value());
        QCOMPARE(*p.minDistance, 0);
    }

    /// `QJsonValue::toInt(default)` returns `default` verbatim when
    /// the value is a JSON Number-with-fractional-zero like `5.0`,
    /// so a file written by a non-C++ serializer that emits the
    /// integer as a double silently fell back to the library
    /// default. The fix routes through `toDouble()` + `qRound()`.
    /// Regression guard: a JSON double `5.0` must produce
    /// `minDistance = 5`, not the library default.
    void testFromJsonMinDistanceAcceptsWholeDouble()
    {
        QJsonObject obj;
        obj.insert(QLatin1String("minDistance"), 5.0);
        const Profile p = Profile::fromJson(obj, CurveRegistry{});
        QVERIFY2(p.minDistance.has_value(),
                 "minDistance 5.0 must engage the optional (double silent-fallback regression)");
        QCOMPARE(*p.minDistance, 5);
    }

    /// A non-string `presetName` (e.g. `"presetName": 42`) must NOT
    /// engage the optional with an empty string. Previously
    /// `toString()` on a non-string QJsonValue returned "" and the
    /// optional ended up engaged-empty — semantically distinct from
    /// "unset" (engaged-empty means "explicit empty override") and
    /// so a malformed JSON silently mutated to an explicit override.
    /// Fix: guard on `isString()` before assigning.
    void testFromJsonRejectsNonStringPresetName()
    {
        QJsonObject obj;
        obj.insert(QLatin1String("presetName"), 42);
        const Profile p = Profile::fromJson(obj, CurveRegistry{});
        QVERIFY2(!p.presetName.has_value(),
                 "non-string presetName must NOT engage the optional (not even with empty string)");

        // Cross-check: boolean and object values also don't engage.
        QJsonObject objBool;
        objBool.insert(QLatin1String("presetName"), true);
        QVERIFY(!Profile::fromJson(objBool, CurveRegistry{}).presetName.has_value());

        QJsonObject objObj;
        objObj.insert(QLatin1String("presetName"), QJsonObject{});
        QVERIFY(!Profile::fromJson(objObj, CurveRegistry{}).presetName.has_value());
    }

    /// Sanity: a genuine string `presetName` DOES still engage the
    /// optional — guards against a regression where the new
    /// `isString()` check is over-tightened.
    void testFromJsonAcceptsStringPresetName()
    {
        QJsonObject obj;
        obj.insert(QLatin1String("presetName"), QStringLiteral("My Preset"));
        const Profile p = Profile::fromJson(obj, CurveRegistry{});
        QVERIFY(p.presetName.has_value());
        QCOMPARE(*p.presetName, QStringLiteral("My Preset"));

        // Empty string is also accepted — it's the documented
        // engaged-empty override semantic.
        QJsonObject objEmpty;
        objEmpty.insert(QLatin1String("presetName"), QString());
        const Profile pEmpty = Profile::fromJson(objEmpty, CurveRegistry{});
        QVERIFY(pEmpty.presetName.has_value());
        QVERIFY(pEmpty.presetName->isEmpty());
    }
    // ─── Type rejection (the isDouble guard) ──────────────────────────────

    /// A field holding something that is not a JSON NUMBER must be left UNSET,
    /// so the value is inherited from the parent profile.
    ///
    /// This is the whole point of the type guard, and it is invisible to a
    /// range test. `QJsonValue::toDouble(default)` hands back the DEFAULT for
    /// any non-number, and every library default passes every range check — so
    /// without the guard the field lands ENGAGED at that default, which BLOCKS
    /// inheritance. `minDistance` and `staggerInterval` are the witnesses that
    /// matter: their defaults are in range, so only the type check can reject
    /// them. (`duration`'s default would also pass, but a string coerces to 0,
    /// which its `> 0` bound rejects anyway — so a duration row alone would not
    /// pin the guard.)
    void testFromJsonLeavesNonNumericFieldsUnset_data()
    {
        QTest::addColumn<QByteArray>("json");
        QTest::addColumn<QByteArray>("field");

        QTest::newRow("minDistance string")
            << QByteArrayLiteral(R"({"minDistance":"5"})") << QByteArrayLiteral("minDistance");
        QTest::newRow("minDistance bool")
            << QByteArrayLiteral(R"({"minDistance":true})") << QByteArrayLiteral("minDistance");
        QTest::newRow("minDistance null")
            << QByteArrayLiteral(R"({"minDistance":null})") << QByteArrayLiteral("minDistance");
        QTest::newRow("minDistance object")
            << QByteArrayLiteral(R"({"minDistance":{"a":1}})") << QByteArrayLiteral("minDistance");
        QTest::newRow("staggerInterval string")
            << QByteArrayLiteral(R"({"staggerInterval":"20"})") << QByteArrayLiteral("staggerInterval");
        QTest::newRow("staggerInterval bool")
            << QByteArrayLiteral(R"({"staggerInterval":false})") << QByteArrayLiteral("staggerInterval");
        QTest::newRow("duration string") << QByteArrayLiteral(R"({"duration":"900"})") << QByteArrayLiteral("duration");
        QTest::newRow("duration bool") << QByteArrayLiteral(R"({"duration":true})") << QByteArrayLiteral("duration");
    }

    void testFromJsonLeavesNonNumericFieldsUnset()
    {
        QFETCH(QByteArray, json);
        QFETCH(QByteArray, field);

        const QJsonObject obj = QJsonDocument::fromJson(json).object();
        QVERIFY(!obj.isEmpty());

        // The type rejection is reported once per (field, type) per process, so
        // an ignoreMessage here would fail on the second row that shares a pair.
        // The assertion below is what pins the behaviour; the diagnostic is
        // covered by testFromJsonReportsANonNumericFieldOnce.
        const Profile p = Profile::fromJson(obj, CurveRegistry{});

        if (field == QByteArrayLiteral("minDistance")) {
            QVERIFY2(!p.minDistance.has_value(), "a non-numeric minDistance was engaged and now blocks inheritance");
        } else if (field == QByteArrayLiteral("staggerInterval")) {
            QVERIFY2(!p.staggerInterval.has_value(),
                     "a non-numeric staggerInterval was engaged and now blocks inheritance");
        } else {
            QVERIFY2(!p.duration.has_value(), "a non-numeric duration was engaged and now blocks inheritance");
        }
    }

    /// sequenceMode is the ONE field that substitutes an ENGAGED default rather
    /// than staying unset, and a non-numeric value must take that path too —
    /// leaving it unset would let a parent's Cascade through on a leaf whose own
    /// file says otherwise.
    void testFromJsonSubstitutesDefaultForNonNumericSequenceMode()
    {
        QJsonObject obj;
        obj.insert(QLatin1String("sequenceMode"), QStringLiteral("cascade"));
        const Profile p = Profile::fromJson(obj, CurveRegistry{});
        QVERIFY(p.sequenceMode.has_value());
        QCOMPARE(*p.sequenceMode, Profile::DefaultSequenceMode);
    }

    /// The type rejection is diagnosed, and diagnosed ONCE per (field, type) per
    /// process. Profiles are re-read on every settings change and the daemon
    /// republishes on the slider's drag path, so an unrated warning would emit
    /// tens of times a second for one hand-edited value.
    ///
    /// A field/type pair no other slot in this file uses, so the rate limiter's
    /// process-wide state cannot have been consumed already.
    void testFromJsonReportsANonNumericFieldOnce()
    {
        QJsonObject obj;
        obj.insert(QLatin1String("duration"), QJsonArray{});

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("ignoring non-numeric duration")));
        const Profile first = Profile::fromJson(obj, CurveRegistry{});
        QVERIFY(!first.duration.has_value());

        // Second read of the same field/type emits nothing. An unrated warning
        // would surface here as an unexpected QWARN.
        const Profile second = Profile::fromJson(obj, CurveRegistry{});
        QVERIFY(!second.duration.has_value());
    }

    // ─── staggerInterval range ────────────────────────────────────────────

    /// staggerInterval carried a full validator with no coverage at all: every
    /// sibling field has a rejects-out-of-range plus accepts-at-the-cap pair.
    void testFromJsonRejectsOutOfRangeStaggerInterval_data()
    {
        QTest::addColumn<double>("raw");
        QTest::newRow("negative") << -1.0;
        QTest::newRow("just over the cap") << double(Profile::MaxStaggerIntervalMs) + 1.0;
        QTest::newRow("astronomical") << 1e300;
        QTest::newRow("infinity") << std::numeric_limits<double>::infinity();
        QTest::newRow("nan") << std::numeric_limits<double>::quiet_NaN();
    }

    void testFromJsonRejectsOutOfRangeStaggerInterval()
    {
        QFETCH(double, raw);
        QJsonObject obj;
        obj.insert(QLatin1String("staggerInterval"), raw);
        const Profile p = Profile::fromJson(obj, CurveRegistry{});
        QVERIFY(!p.staggerInterval.has_value());
        QCOMPARE(p.effectiveStaggerInterval(), Profile::DefaultStaggerInterval);
    }

    /// The accepted side of the same boundary, so flipping the validator's `>`
    /// to `>=` cannot pass unnoticed.
    void testFromJsonAcceptsStaggerIntervalAtTheCap()
    {
        QJsonObject obj;
        obj.insert(QLatin1String("staggerInterval"), double(Profile::MaxStaggerIntervalMs));
        const Profile p = Profile::fromJson(obj, CurveRegistry{});
        QVERIFY(p.staggerInterval.has_value());
        QCOMPARE(*p.staggerInterval, Profile::MaxStaggerIntervalMs);
    }

    /// Zero is a legal explicit "no stagger", distinct from unset.
    void testFromJsonAcceptsZeroStaggerInterval()
    {
        QJsonObject obj;
        obj.insert(QLatin1String("staggerInterval"), 0);
        const Profile p = Profile::fromJson(obj, CurveRegistry{});
        QVERIFY(p.staggerInterval.has_value());
        QCOMPARE(*p.staggerInterval, 0);
    }

    /// A whole-number DOUBLE round-trips to the same int, like minDistance.
    void testFromJsonStaggerIntervalAcceptsWholeDouble()
    {
        QJsonObject obj;
        obj.insert(QLatin1String("staggerInterval"), 30.0);
        const Profile p = Profile::fromJson(obj, CurveRegistry{});
        QVERIFY(p.staggerInterval.has_value());
        QCOMPARE(*p.staggerInterval, 30);
    }
};

QTEST_MAIN(TestProfile)
#include "test_profile.moc"
