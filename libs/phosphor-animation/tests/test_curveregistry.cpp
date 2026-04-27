// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/Curve.h>
#include <PhosphorAnimation/CurveRegistry.h>
#include <PhosphorAnimation/Easing.h>
#include <PhosphorAnimation/Spring.h>

#include <QJsonObject>
#include <QTest>

using PhosphorAnimation::Curve;
using PhosphorAnimation::CurveRegistry;
using PhosphorAnimation::Easing;
using PhosphorAnimation::Spring;

class TestCurveRegistry : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // ─── Built-ins ───

    void testBuiltInEasingTypes()
    {
        CurveRegistry reg;
        QVERIFY(reg.has(QStringLiteral("bezier")));
        QVERIFY(reg.has(QStringLiteral("elastic-in")));
        QVERIFY(reg.has(QStringLiteral("elastic-out")));
        QVERIFY(reg.has(QStringLiteral("elastic-in-out")));
        QVERIFY(reg.has(QStringLiteral("bounce-in")));
        QVERIFY(reg.has(QStringLiteral("bounce-out")));
        QVERIFY(reg.has(QStringLiteral("bounce-in-out")));
    }

    void testBuiltInSpring()
    {
        CurveRegistry reg;
        QVERIFY(reg.has(QStringLiteral("spring")));
    }

    /// Regression: `cubic-bezier` is advertised as a builtin both by
    /// `isBuiltinTypeId` (which the loader uses to reject user curves
    /// that would shadow a builtin) and by the build-time
    /// check-animation-profiles.py validator (which accepts profile
    /// JSONs that reference `"curve": "cubic-bezier:..."`). Without a
    /// matching factory registration, `tryCreate("cubic-bezier:...")`
    /// returned null and the profile silently fell back to OutCubic at
    /// runtime — passing the build check while shipping broken motion.
    /// This test pins the three-way alignment.
    void testBuiltInCubicBezierAlias()
    {
        CurveRegistry reg;
        // 1. Factory is registered and resolvable through the colon-form
        //    that the validator accepts.
        QVERIFY(reg.has(QStringLiteral("cubic-bezier")));
        auto curve = reg.tryCreate(QStringLiteral("cubic-bezier:0.25,0.10,0.25,1.00"));
        QVERIFY(curve != nullptr);
        QCOMPARE(curve->typeId(), QStringLiteral("bezier"));

        // 2. Output is identical to the canonical `bezier:` form — they
        //    are aliases by construction, not two divergent implementations.
        auto canonical = reg.tryCreate(QStringLiteral("bezier:0.25,0.10,0.25,1.00"));
        QVERIFY(canonical);
        for (double t : {0.0, 0.25, 0.5, 0.75, 1.0}) {
            QCOMPARE(curve->evaluate(t), canonical->evaluate(t));
        }

        // 3. isBuiltinTypeId() agrees — the loader's user-curve-name
        //    rejection guard and the registered factory set must stay in
        //    sync. This is the corner the documented "deliberate symmetry"
        //    in registerBuiltins() actually relies on.
        QVERIFY(CurveRegistry::isBuiltinTypeId(QStringLiteral("cubic-bezier")));
    }

    // ─── create() parsing ───

    void testCreateBezierBareWireFormat()
    {
        // Bare "x1,y1,x2,y2" is the canonical cubic-bezier wire format.
        auto curve = CurveRegistry{}.create(QStringLiteral("0.33,1.00,0.68,1.00"));
        QVERIFY(curve != nullptr);
        QCOMPARE(curve->typeId(), QStringLiteral("bezier"));
    }

    void testCreatePrefixedBezierAccepted()
    {
        // Both the bare "x1,y1,x2,y2" and the prefixed "bezier:x1,y1,x2,y2"
        // forms round-trip. toString() emits the bare canonical form;
        // fromString accepts the prefix for legacy configs and hand-written
        // settings so they don't silently degrade to the OutCubic default.
        auto curve = CurveRegistry{}.tryCreate(QStringLiteral("bezier:0.25,0.10,0.25,1.00"));
        QVERIFY(curve != nullptr);
        QCOMPARE(curve->typeId(), QStringLiteral("bezier"));
    }

    void testPrefixedBezierMatchesBare()
    {
        // The bare form is canonical; the prefixed form must produce a
        // curve that evaluates identically. Previously only the bare form
        // dispatched to the factory — this test prevents silent divergence
        // if the prefix handling is ever changed.
        CurveRegistry reg;
        auto bare = reg.tryCreate(QStringLiteral("0.25,0.10,0.25,1.00"));
        auto prefixed = reg.tryCreate(QStringLiteral("bezier:0.25,0.10,0.25,1.00"));
        QVERIFY(bare && prefixed);
        QCOMPARE(bare->typeId(), prefixed->typeId());
        for (double t : {0.0, 0.25, 0.5, 0.75, 1.0}) {
            QCOMPARE(bare->evaluate(t), prefixed->evaluate(t));
        }
    }

    void testPrefixCaseInsensitive()
    {
        // Hand-written configs and older docs often use capitalised
        // prefixes ("Bezier:…", "SPRING:…"). Factory typeIds are
        // registered lower-case by convention, so parseSpec folds the
        // prefix case before lookup — otherwise these specs silently
        // degrade to the OutCubic default.
        CurveRegistry reg;
        QVERIFY(reg.tryCreate(QStringLiteral("Bezier:0.25,0.10,0.25,1.00")) != nullptr);
        QVERIFY(reg.tryCreate(QStringLiteral("BEZIER:0.25,0.10,0.25,1.00")) != nullptr);
        QVERIFY(reg.tryCreate(QStringLiteral("Spring:12.0,0.8")) != nullptr);
    }

    void testCreateElastic()
    {
        auto curve = CurveRegistry{}.create(QStringLiteral("elastic-out:1.2,0.4"));
        QVERIFY(curve != nullptr);
        QCOMPARE(curve->typeId(), QStringLiteral("elastic-out"));
    }

    void testCreateSpring()
    {
        auto curve = CurveRegistry{}.create(QStringLiteral("spring:12.0,0.8"));
        QVERIFY(curve != nullptr);
        QCOMPARE(curve->typeId(), QStringLiteral("spring"));
        QVERIFY(curve->isStateful());
    }

    void testCreateEmptyFallsBackToDefault()
    {
        // Empty spec falls through to the same default curve as unknown
        // typeIds. The `animationEasingCurve` setting round-trips through
        // empty during config reload / migration / settings edits, and
        // a null curve would mean linear progression in WindowMotion —
        // a visible regression from the pre-registry OutCubic default.
        // create() guarantees a non-null curve so consumers don't need
        // parallel null-guards.
        auto curve = CurveRegistry{}.create(QString());
        QVERIFY(curve != nullptr);
        QCOMPARE(curve->typeId(), QStringLiteral("bezier"));

        // Sanity: default Easing is OutCubic bezier (0.33, 1, 0.68, 1).
        // At t=0.5 an OutCubic bezier sits above 0.5 — linear would be
        // exactly 0.5. Proves we're not silently handing back linear.
        QVERIFY(curve->evaluate(0.5) > 0.55);
    }

    void testCreateUnknownFallsBack()
    {
        // Unknown typeId falls through to a default bezier instead of
        // returning null — callers get a valid curve to work with.
        auto curve = CurveRegistry{}.create(QStringLiteral("not-a-real-curve:1,2,3"));
        QVERIFY(curve != nullptr);
        QCOMPARE(curve->typeId(), QStringLiteral("bezier"));
    }

    // ─── tryCreate() — explicit-failure path ───

    void testTryCreateEmptyReturnsNull()
    {
        QVERIFY(CurveRegistry{}.tryCreate(QString()) == nullptr);
    }

    void testTryCreateUnknownReturnsNull()
    {
        // tryCreate does NOT substitute a default — callers that want the
        // fallback behavior can use create(); those that want to detect
        // bad input use tryCreate.
        QVERIFY(CurveRegistry{}.tryCreate(QStringLiteral("not-a-real-curve:1,2,3")) == nullptr);
    }

    void testTryCreateValidReturnsCurve()
    {
        auto curve = CurveRegistry{}.tryCreate(QStringLiteral("spring:12.0,0.8"));
        QVERIFY(curve != nullptr);
        QCOMPARE(curve->typeId(), QStringLiteral("spring"));
    }

    // ─── Extension ───

    void testRegisterAndUnregisterCustom()
    {
        CurveRegistry reg;
        const QString id = QStringLiteral("test-custom-linear");

        QVERIFY(!reg.has(id));

        CurveRegistry::Factory factory = [](const QString&, const QString&) -> std::shared_ptr<const Curve> {
            // Linear curve — reuse Easing with bezier (0,0,1,1) shape.
            Easing e;
            e.x1 = 0.0;
            e.y1 = 0.0;
            e.x2 = 1.0;
            e.y2 = 1.0;
            return std::make_shared<Easing>(e);
        };

        const bool replaced = reg.registerFactory(id, factory);
        QVERIFY(!replaced);
        QVERIFY(reg.has(id));

        auto curve = reg.create(id);
        QVERIFY(curve != nullptr);
        // At t=0.5, linear → 0.5 within tolerance.
        QVERIFY(qAbs(curve->evaluate(0.5) - 0.5) < 0.05);

        QVERIFY(reg.unregisterFactory(id));
        QVERIFY(!reg.has(id));
    }

    void testRegisterEmptyRejected()
    {
        CurveRegistry reg;
        const bool result = reg.registerFactory(QString(), [](const QString&, const QString&) {
            return std::make_shared<Easing>();
        });
        QVERIFY(!result);
    }

    void testKnownTypesContainsBuiltIns()
    {
        const QStringList types = CurveRegistry{}.knownTypes();
        QVERIFY(types.contains(QStringLiteral("bezier")));
        QVERIFY(types.contains(QStringLiteral("spring")));
        QVERIFY(types.contains(QStringLiteral("elastic-out")));
    }

    // ─── Owner-tagged partitioning ───

    /// Two independent owners register factories under DIFFERENT
    /// typeIds. `unregisterByOwner(ownerA)` must remove only ownerA's
    /// entries — ownerB's factory stays resolvable. Without owner
    /// partitioning, a single `CurveLoader` destroying itself would have
    /// been forced to unregister per-key, which is wrong when another
    /// registrant shares a key.
    void testUnregisterByOwnerLeavesOtherOwnerIntact()
    {
        CurveRegistry reg;
        const QString ownerA = QStringLiteral("test-owner-a");
        const QString ownerB = QStringLiteral("test-owner-b");
        const QString idA = QStringLiteral("test-curve-a");
        const QString idB = QStringLiteral("test-curve-b");

        auto makeLinear = []() {
            Easing e;
            e.x1 = 0.0;
            e.y1 = 0.0;
            e.x2 = 1.0;
            e.y2 = 1.0;
            return std::make_shared<Easing>(e);
        };

        reg.registerFactory(
            idA,
            [makeLinear](const QString&, const QString&) {
                return makeLinear();
            },
            ownerA);
        reg.registerFactory(
            idB,
            [makeLinear](const QString&, const QString&) {
                return makeLinear();
            },
            ownerB);

        QVERIFY(reg.has(idA));
        QVERIFY(reg.has(idB));

        const int removed = reg.unregisterByOwner(ownerA);
        QCOMPARE(removed, 1);
        QVERIFY(!reg.has(idA));
        QVERIFY2(reg.has(idB), "unregisterByOwner must not evict another owner's factory");
        QVERIFY(reg.tryCreate(idB) != nullptr);
    }

    /// Two owners register OVERLAPPING typeIds (ownerB's registration
    /// replaces ownerA's). When ownerA later tries to clean up by
    /// owner, the entry is already tagged with ownerB, so it survives.
    /// ownerB's own tear-down does evict.
    void testUnregisterByOwnerDoesNotEvictReplacedRegistration()
    {
        CurveRegistry reg;
        const QString ownerA = QStringLiteral("test-owner-a");
        const QString ownerB = QStringLiteral("test-owner-b");
        const QString sharedId = QStringLiteral("test-shared-curve");

        reg.registerFactory(
            sharedId,
            [](const QString&, const QString&) {
                return std::make_shared<Easing>();
            },
            ownerA);
        reg.registerFactory(
            sharedId,
            [](const QString&, const QString&) {
                return std::make_shared<Easing>();
            },
            ownerB);

        QVERIFY(reg.has(sharedId));

        const int removed = reg.unregisterByOwner(ownerA);
        QCOMPARE(removed, 0);
        QVERIFY2(reg.has(sharedId), "unregisterByOwner must only remove entries currently tagged with the owner");

        QCOMPARE(reg.unregisterByOwner(ownerB), 1);
        QVERIFY(!reg.has(sharedId));
    }

    /// Empty owner tag is a no-op (never "match every untagged built-in
    /// and wipe the registry"). Guardrail against accidental empty-
    /// string defaults in caller code.
    void testUnregisterByOwnerEmptyTagIsNoOp()
    {
        CurveRegistry reg;
        const int before = reg.knownTypes().size();
        QCOMPARE(reg.unregisterByOwner(QString()), 0);
        QCOMPARE(reg.knownTypes().size(), before);
        QVERIFY(reg.has(QStringLiteral("spring")));
        QVERIFY(reg.has(QStringLiteral("bezier")));
    }

    /// `tryCreateFromJson` round-trips each built-in typeId from a
    /// parameters object. Mirrors `CurveLoader::Sink::parseFile`
    /// after the loader stopped duplicating the schema.
    void testTryCreateFromJsonBuiltins()
    {
        CurveRegistry reg;

        QJsonObject springParams;
        springParams.insert(QLatin1String("omega"), 14.0);
        springParams.insert(QLatin1String("zeta"), 0.6);
        auto spring = reg.tryCreateFromJson(QStringLiteral("spring"), springParams);
        QVERIFY(spring != nullptr);
        QCOMPARE(spring->typeId(), QStringLiteral("spring"));

        QJsonObject bezierParams;
        bezierParams.insert(QLatin1String("x1"), 0.25);
        bezierParams.insert(QLatin1String("y1"), 0.75);
        bezierParams.insert(QLatin1String("x2"), 0.75);
        bezierParams.insert(QLatin1String("y2"), 0.25);
        auto bezier = reg.tryCreateFromJson(QStringLiteral("cubic-bezier"), bezierParams);
        QVERIFY(bezier != nullptr);
        QCOMPARE(bezier->typeId(), QStringLiteral("bezier"));

        QJsonObject elasticParams;
        elasticParams.insert(QLatin1String("amplitude"), 1.2);
        elasticParams.insert(QLatin1String("period"), 0.4);
        auto elastic = reg.tryCreateFromJson(QStringLiteral("elastic-out"), elasticParams);
        QVERIFY(elastic != nullptr);
        QCOMPARE(elastic->typeId(), QStringLiteral("elastic-out"));
    }

    /// Validation failures from the built-in JSON factories surface
    /// as `nullptr` — same contract as `tryCreate` for a bad spec.
    /// The loader relies on this return value to skip the file.
    void testTryCreateFromJsonValidationRejects()
    {
        CurveRegistry reg;

        QJsonObject badSpring;
        badSpring.insert(QLatin1String("omega"), -5.0);
        badSpring.insert(QLatin1String("zeta"), 0.8);
        QVERIFY(reg.tryCreateFromJson(QStringLiteral("spring"), badSpring) == nullptr);

        QJsonObject badBezier;
        badBezier.insert(QLatin1String("x1"), 1.5);
        badBezier.insert(QLatin1String("y1"), 0.75);
        badBezier.insert(QLatin1String("x2"), 0.5);
        badBezier.insert(QLatin1String("y2"), 0.5);
        QVERIFY(reg.tryCreateFromJson(QStringLiteral("cubic-bezier"), badBezier) == nullptr);
    }

    /// Unknown typeId → nullptr (no JSON factory registered).
    void testTryCreateFromJsonUnknownReturnsNull()
    {
        CurveRegistry reg;
        QVERIFY(reg.tryCreateFromJson(QStringLiteral("not-a-real-curve"), QJsonObject{}) == nullptr);
    }

    /// A third-party curve registered via the untagged
    /// `registerFactory(typeId, factory)` overload has NO JsonFactory
    /// and is unreachable through `tryCreateFromJson` — the loader
    /// logs a skip. Exercises the null-JsonFactory branch of
    /// `tryCreateFromJson`.
    void testTryCreateFromJsonIgnoresStringOnlyFactory()
    {
        CurveRegistry reg;
        const QString id = QStringLiteral("test-string-only");
        reg.registerFactory(id, [](const QString&, const QString&) {
            return std::make_shared<Easing>();
        });
        QVERIFY(reg.tryCreate(id) != nullptr);
        QVERIFY(reg.tryCreateFromJson(id, QJsonObject{}) == nullptr);
    }
};

QTEST_MAIN(TestCurveRegistry)
#include "test_curveregistry.moc"
