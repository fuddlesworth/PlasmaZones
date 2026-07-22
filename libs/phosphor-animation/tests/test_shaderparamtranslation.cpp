// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/AnimationShaderContract.h>
#include <PhosphorAnimation/AnimationShaderRegistry.h>
#include <PhosphorShaders/CustomParamsKey.h>

#include <QColor>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTest>

using PhosphorAnimationShaders::AnimationShaderEffect;
using PhosphorAnimationShaders::AnimationShaderRegistry;

class TestShaderParamTranslation : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // ── translateAnimationParams ─────────────────────────────────────
    //
    // Slot allocation contract: parameters fill `customParams<N>_<x|y|z|w>`
    // in metadata declaration order, with N starting at 1 and components
    // x/y/z/w within a vec4. Both runtimes consume the resulting map; a
    // shader.frag reads the corresponding `customParams[N-1].xyz` slot
    // via the `#define` macros each effect.frag declares. A regression
    // here would silently break parameter delivery on both render
    // execution sites.

    void testTranslateAnimationParamsSlotAllocation()
    {
        AnimationShaderEffect eff;
        eff.id = QStringLiteral("slide");
        eff.fragmentShaderPath = QStringLiteral("effect.frag");
        // Two float params — should land in customParams1_x, customParams1_y.
        AnimationShaderEffect::ParameterInfo p1;
        p1.id = QStringLiteral("direction");
        p1.type = QStringLiteral("int");
        p1.defaultValue = 0;
        AnimationShaderEffect::ParameterInfo p2;
        p2.id = QStringLiteral("parallax");
        p2.type = QStringLiteral("float");
        p2.defaultValue = 0.0;
        eff.parameters = {p1, p2};

        const QVariantMap friendly{{QStringLiteral("direction"), 1}, {QStringLiteral("parallax"), 0.2}};
        const QVariantMap translated = AnimationShaderRegistry::translateAnimationParams(eff, friendly);

        QCOMPARE(translated.size(), 2);
        QCOMPARE(translated.value(QStringLiteral("customParams1_x")).toInt(), 1);
        QCOMPARE(translated.value(QStringLiteral("customParams1_y")).toDouble(), 0.2);
        QVERIFY(!translated.contains(QStringLiteral("direction"))); // friendly keys dropped
    }

    void testTranslateAnimationParamsFallsBackToDeclaredDefaults()
    {
        AnimationShaderEffect eff;
        eff.id = QStringLiteral("dissolve");
        eff.fragmentShaderPath = QStringLiteral("effect.frag");
        AnimationShaderEffect::ParameterInfo grain;
        grain.id = QStringLiteral("grain");
        grain.type = QStringLiteral("float");
        grain.defaultValue = 0.05;
        AnimationShaderEffect::ParameterInfo softness;
        softness.id = QStringLiteral("softness");
        softness.type = QStringLiteral("float");
        softness.defaultValue = 0.1;
        eff.parameters = {grain, softness};

        // Friendly map empty → both slots get metadata defaults.
        const QVariantMap translated = AnimationShaderRegistry::translateAnimationParams(eff, QVariantMap());
        QCOMPARE(translated.value(QStringLiteral("customParams1_x")).toDouble(), 0.05);
        QCOMPARE(translated.value(QStringLiteral("customParams1_y")).toDouble(), 0.1);
    }

    void testTranslateAnimationParamsBoolCoerced()
    {
        AnimationShaderEffect eff;
        eff.id = QStringLiteral("test-bool");
        eff.fragmentShaderPath = QStringLiteral("effect.frag");
        AnimationShaderEffect::ParameterInfo p;
        p.id = QStringLiteral("flag");
        p.type = QStringLiteral("bool");
        p.defaultValue = false;
        eff.parameters = {p};

        const QVariantMap translatedTrue =
            AnimationShaderRegistry::translateAnimationParams(eff, {{QStringLiteral("flag"), true}});
        QCOMPARE(translatedTrue.value(QStringLiteral("customParams1_x")).toFloat(), 1.0f);

        const QVariantMap translatedFalse =
            AnimationShaderRegistry::translateAnimationParams(eff, {{QStringLiteral("flag"), false}});
        QCOMPARE(translatedFalse.value(QStringLiteral("customParams1_x")).toFloat(), 0.0f);
    }

    void testTranslateAnimationParamsCrossesVec4Boundary()
    {
        AnimationShaderEffect eff;
        eff.id = QStringLiteral("test-five");
        eff.fragmentShaderPath = QStringLiteral("effect.frag");
        // Five float params: 0..3 fill customParams1_x..customParams1_w,
        // the fifth lands in customParams2_x.
        for (int i = 0; i < 5; ++i) {
            AnimationShaderEffect::ParameterInfo p;
            p.id = QStringLiteral("p%1").arg(i);
            p.type = QStringLiteral("float");
            p.defaultValue = double(i);
            eff.parameters.append(p);
        }
        const QVariantMap translated = AnimationShaderRegistry::translateAnimationParams(eff, QVariantMap());
        QCOMPARE(translated.value(QStringLiteral("customParams1_x")).toDouble(), 0.0);
        QCOMPARE(translated.value(QStringLiteral("customParams1_y")).toDouble(), 1.0);
        QCOMPARE(translated.value(QStringLiteral("customParams1_z")).toDouble(), 2.0);
        QCOMPARE(translated.value(QStringLiteral("customParams1_w")).toDouble(), 3.0);
        QCOMPARE(translated.value(QStringLiteral("customParams2_x")).toDouble(), 4.0);
    }

    void testTranslateAnimationParamsEmptyForInvalidEffect()
    {
        AnimationShaderEffect invalid; // no id, no frag — isValid() == false
        const QVariantMap translated =
            AnimationShaderRegistry::translateAnimationParams(invalid, {{QStringLiteral("foo"), 1.0}});
        QVERIFY(translated.isEmpty());
    }

    // ── slotKey ──────────────────────────────────────────────────────
    //
    // Both the registry encoder and the two decoders (kwin-effect's
    // per-transition pack, ShaderEffect::setShaderParams in phosphor-rendering)
    // build customParams keys via this single helper. Pin the format —
    // any drift here would fragment the contract across three call sites.

    void testSlotKey()
    {
        using PhosphorAnimationShaders::AnimationShaderContract::slotKey;
        QCOMPARE(slotKey(0, 'x'), QStringLiteral("customParams1_x"));
        QCOMPARE(slotKey(0, 'y'), QStringLiteral("customParams1_y"));
        QCOMPARE(slotKey(0, 'z'), QStringLiteral("customParams1_z"));
        QCOMPARE(slotKey(0, 'w'), QStringLiteral("customParams1_w"));
        QCOMPARE(slotKey(1, 'x'), QStringLiteral("customParams2_x"));
        QCOMPARE(slotKey(7, 'w'), QStringLiteral("customParams8_w"));
    }

    /// Flat-slot overload covers the in-range path the registry encoder
    /// actually uses (translateAnimationParams calls `slotKey(floatSlot)`),
    /// plus the boundary policy: out-of-range slots return an empty
    /// QString rather than wrapping around to a valid in-range key.
    /// Wrap-around would silently collide with another slot and corrupt
    /// the decoder's UBO upload — pin the empty-on-OOB contract here.
    void testSlotKeyFlatIndex()
    {
        using PhosphorAnimationShaders::AnimationShaderContract::slotKey;
        // In-range — same outputs as the (vec, comp) overload.
        QCOMPARE(slotKey(0), QStringLiteral("customParams1_x"));
        QCOMPARE(slotKey(3), QStringLiteral("customParams1_w"));
        QCOMPARE(slotKey(4), QStringLiteral("customParams2_x"));
        QCOMPARE(slotKey(31), QStringLiteral("customParams8_w"));
        // Out-of-range — empty, not a wrapped-around collision.
        QVERIFY(slotKey(-1).isEmpty());
        QVERIFY(slotKey(32).isEmpty());
        QVERIFY(slotKey(100).isEmpty());
    }

    // ── colorKey ─────────────────────────────────────────────────────
    //
    // Sibling helper to `CustomParams::slotKey` for the `customColors[N]`
    // region. Three call sites consume keys produced here:
    // `ShaderEffect::setShaderParams`'s color-decoder branch,
    // `AnimationShaderRegistry::translateAnimationParams`'s color-param
    // encoder, and the kwin-effect's per-transition `customColorsValues`
    // pack. Pin the format so a future drift fragments at most this
    // single test, not all three call sites.

    void testColorKey()
    {
        using PhosphorShaders::CustomColors::colorKey;
        // 0-based input → 1-based output, matching the GLSL author
        // convention (`customColor1` is the first slot).
        QCOMPARE(colorKey(0), QStringLiteral("customColor1"));
        QCOMPARE(colorKey(1), QStringLiteral("customColor2"));
        QCOMPARE(colorKey(15), QStringLiteral("customColor16"));
        // Out-of-range — empty, mirroring `slotKey(int)`'s graceful-
        // degradation contract. Wrap-around would silently collide with a
        // valid in-range key.
        QVERIFY(colorKey(-1).isEmpty());
        QVERIFY(colorKey(16).isEmpty());
        QVERIFY(colorKey(100).isEmpty());
    }

    // ── translateAnimationParams: color parameter routing ────────────
    //
    // Color-typed parameters route to `customColor<N>` slots, sibling to
    // the float/int/bool `customParams<N>_<x|y|z|w>` allocator. The two
    // allocators advance independently — a color param does NOT consume
    // a float sub-slot. Pin the encoder so a regression that accidentally
    // collapsed the two allocators back into one (or dropped color
    // routing altogether, as the predecessor revision did) surfaces here
    // rather than as a silent black-default at runtime.
    void testColorParamTranslation()
    {
        AnimationShaderEffect eff;
        eff.id = QStringLiteral("test-color");
        eff.fragmentShaderPath = QStringLiteral("/dummy/effect.frag");
        AnimationShaderEffect::ParameterInfo tint;
        tint.id = QStringLiteral("tint");
        tint.type = QStringLiteral("color");
        tint.defaultValue = QColor(Qt::red);
        AnimationShaderEffect::ParameterInfo speed;
        speed.id = QStringLiteral("speed");
        speed.type = QStringLiteral("float");
        speed.defaultValue = 1.5;
        eff.parameters = {tint, speed};

        const QVariantMap result = AnimationShaderRegistry::translateAnimationParams(eff, {});
        QVERIFY(result.contains(QStringLiteral("customColor1")));
        QVERIFY(result.contains(QStringLiteral("customParams1_x")));
        const QColor c = result.value(QStringLiteral("customColor1")).value<QColor>();
        QCOMPARE(c, QColor(Qt::red));
        QCOMPARE(result.value(QStringLiteral("customParams1_x")).toDouble(), 1.5);
    }

    /// Independence of the float and color allocators. A
    /// `[color, float, color, float]` declaration must produce
    /// `customColor1 + customParams1_x + customColor2 + customParams1_y`,
    /// NOT `customColor1 + customParams1_y + customColor2 +
    /// customParams2_x`. The allocators advance separately; a regression
    /// that collapsed them back into a single counter (or that
    /// accidentally bumped floatSlot for color params) would silently
    /// corrupt the float-slot delivery for any pack mixing the two
    /// types.
    void testColorAndFloatAllocatorsAdvanceIndependently()
    {
        AnimationShaderEffect eff;
        eff.id = QStringLiteral("test-mixed");
        eff.fragmentShaderPath = QStringLiteral("/dummy/effect.frag");
        const auto makeParam = [](const QString& id, const QString& type, const QVariant& def) {
            AnimationShaderEffect::ParameterInfo p;
            p.id = id;
            p.type = type;
            p.defaultValue = def;
            return p;
        };
        eff.parameters = {
            makeParam(QStringLiteral("c0"), QStringLiteral("color"), QColor(Qt::red)),
            makeParam(QStringLiteral("f0"), QStringLiteral("float"), 0.1),
            makeParam(QStringLiteral("c1"), QStringLiteral("color"), QColor(Qt::green)),
            makeParam(QStringLiteral("f1"), QStringLiteral("float"), 0.2),
        };

        const QVariantMap r = AnimationShaderRegistry::translateAnimationParams(eff, {});
        // Color allocator: c0 → customColor1, c1 → customColor2.
        QCOMPARE(r.value(QStringLiteral("customColor1")).value<QColor>(), QColor(Qt::red));
        QCOMPARE(r.value(QStringLiteral("customColor2")).value<QColor>(), QColor(Qt::green));
        // Float allocator advances independently: f0 → customParams1_x
        // (NOT customParams1_y, which would happen if the color params
        // had bumped floatSlot), f1 → customParams1_y.
        QCOMPARE(r.value(QStringLiteral("customParams1_x")).toDouble(), 0.1);
        QCOMPARE(r.value(QStringLiteral("customParams1_y")).toDouble(), 0.2);
        // Pin the negative side too — neither customParams2_x nor a
        // stray entry from a collapsed allocator should appear.
        QVERIFY(!r.contains(QStringLiteral("customParams2_x")));
    }

    /// String hex codes from a friendlyParams map (e.g. user-edited
    /// config that wrote `"#ff8800"`) coerce to QColor at the registry
    /// boundary. Without this coercion, the kwin-effect's
    /// `value<QColor>()` decoder would receive an invalid QColor and
    /// silently render the slot as black.
    void testColorParamCoercesStringHex()
    {
        AnimationShaderEffect eff;
        eff.id = QStringLiteral("test-color-string");
        eff.fragmentShaderPath = QStringLiteral("/dummy/effect.frag");
        AnimationShaderEffect::ParameterInfo tint;
        tint.id = QStringLiteral("tint");
        tint.type = QStringLiteral("color");
        // Note no defaultValue — friendly map is the only source.
        eff.parameters = {tint};

        const QVariantMap result = AnimationShaderRegistry::translateAnimationParams(
            eff, {{QStringLiteral("tint"), QStringLiteral("#ff8800")}});
        QVERIFY(result.contains(QStringLiteral("customColor1")));
        const QColor c = result.value(QStringLiteral("customColor1")).value<QColor>();
        QVERIFY(c.isValid());
        QCOMPARE(c, QColor(0xff, 0x88, 0x00));
    }

    /// Alpha channel survives the QColor → QVariantMap → kwin-effect
    /// QVector4D round-trip. Without an explicit assertion, an encoder
    /// regression that dropped the alpha (e.g. `setRgb()` instead of
    /// `setRgba()`) would silently slip past `testColorParamTranslation`
    /// because `QColor(Qt::red)` and a `setRgb()`-only result both
    /// compare equal under `QColor::operator==` when alpha defaults to
    /// 255.
    void testColorParamPreservesAlpha()
    {
        AnimationShaderEffect eff;
        eff.id = QStringLiteral("test-color-alpha");
        eff.fragmentShaderPath = QStringLiteral("/dummy/effect.frag");
        AnimationShaderEffect::ParameterInfo tint;
        tint.id = QStringLiteral("tint");
        tint.type = QStringLiteral("color");
        eff.parameters = {tint};

        // Friendly map carries an explicit non-255 alpha — pin that the
        // encoder threads it through to the customColor1 slot. Wrapped
        // in a scope block so a future maintainer adding a third case
        // can't accidentally have a `result` shadow the prior block's.
        {
            const QColor source(0xff, 0x00, 0x00, 0x80);
            const QVariantMap result =
                AnimationShaderRegistry::translateAnimationParams(eff, {{QStringLiteral("tint"), source}});
            QVERIFY(result.contains(QStringLiteral("customColor1")));
            const QColor c = result.value(QStringLiteral("customColor1")).value<QColor>();
            QVERIFY(c.isValid());
            QCOMPARE(c.alpha(), 0x80);
            QCOMPARE(c, source);
        }

        // Qt's QColor accepts 8-char hex in `#AARRGGBB` form (alpha
        // first), per `QColor::QColor(QString)` — NOT the CSS-style
        // `#RRGGBBAA` convention. Pin that the encoder threads alpha
        // through whichever form Qt's parser actually accepts.
        {
            const QVariantMap fromHex = AnimationShaderRegistry::translateAnimationParams(
                eff, {{QStringLiteral("tint"), QStringLiteral("#80ff0000")}});
            const QColor cHex = fromHex.value(QStringLiteral("customColor1")).value<QColor>();
            QVERIFY(cHex.isValid());
            QCOMPARE(cHex.alpha(), 0x80);
            QCOMPARE(cHex.red(), 0xff);
        }
    }

    /// Coercion-edge contract for the friendlyParams → customColor<N>
    /// path. The translator's coerce lambda accepts QColor, QString
    /// (any QColor-parseable form including SVG names), and falls back
    /// to the declared default — ultimately to `Qt::transparent` if no
    /// path resolves. Pin every edge so a regression in the lambda
    /// surfaces here rather than as a silent black-default at runtime.
    void testColorParamCoercionEdges()
    {
        AnimationShaderEffect eff;
        eff.id = QStringLiteral("test-color-edges");
        eff.fragmentShaderPath = QStringLiteral("/dummy/effect.frag");
        AnimationShaderEffect::ParameterInfo tint;
        tint.id = QStringLiteral("tint");
        tint.type = QStringLiteral("color");
        tint.defaultValue = QColor(Qt::blue);
        eff.parameters = {tint};

        // SVG colour name resolves via QColor's string ctor.
        {
            const QVariantMap r = AnimationShaderRegistry::translateAnimationParams(
                eff, {{QStringLiteral("tint"), QStringLiteral("red")}});
            QCOMPARE(r.value(QStringLiteral("customColor1")).value<QColor>(), QColor(Qt::red));
        }
        // Invalid hex string falls back to the declared default (blue).
        {
            const QVariantMap r = AnimationShaderRegistry::translateAnimationParams(
                eff, {{QStringLiteral("tint"), QStringLiteral("#zzzzzz")}});
            QCOMPARE(r.value(QStringLiteral("customColor1")).value<QColor>(), QColor(Qt::blue));
        }
        // Empty string falls back to the declared default (blue).
        {
            const QVariantMap r =
                AnimationShaderRegistry::translateAnimationParams(eff, {{QStringLiteral("tint"), QString()}});
            QCOMPARE(r.value(QStringLiteral("customColor1")).value<QColor>(), QColor(Qt::blue));
        }
        // Wrong-type variant (int) falls back to the declared default
        // (blue) — int → QColor cannot resolve, int → QString gives
        // "42", which is not a valid colour, so the chain bottoms out
        // on the declared default.
        {
            const QVariantMap r =
                AnimationShaderRegistry::translateAnimationParams(eff, {{QStringLiteral("tint"), 42}});
            QCOMPARE(r.value(QStringLiteral("customColor1")).value<QColor>(), QColor(Qt::blue));
        }

        // No defaultValue + no friendlyParams entry → ultimate fallback
        // to Qt::transparent (the chain's documented sentinel).
        AnimationShaderEffect noDefault;
        noDefault.id = QStringLiteral("test-color-no-default");
        noDefault.fragmentShaderPath = QStringLiteral("/dummy/effect.frag");
        AnimationShaderEffect::ParameterInfo bare;
        bare.id = QStringLiteral("tint");
        bare.type = QStringLiteral("color");
        noDefault.parameters = {bare};
        const QVariantMap rNoSrc = AnimationShaderRegistry::translateAnimationParams(noDefault, {});
        QCOMPARE(rNoSrc.value(QStringLiteral("customColor1")).value<QColor>(), QColor(Qt::transparent));
    }

    /// Lower-digit hex forms accepted by `QColor::QColor(QString)` survive
    /// the friendlyParams → customColor coercion. The encoder routes any
    /// QString-shaped value through Qt's parser, so 3-digit, 12-bit, and
    /// 16-bit forms all work alongside the canonical 6/8-digit forms
    /// covered above. A future encoder tightening that forced a strict
    /// 6-digit-only check would silently break user configs containing
    /// any of these — pin every shape Qt actually accepts here. Note
    /// Qt does NOT accept 4-digit `#argb` or `#rgba` forms — only the
    /// shapes asserted below.
    void testColorParamCoercesShortHex()
    {
        AnimationShaderEffect eff;
        eff.id = QStringLiteral("test-color-short-hex");
        eff.fragmentShaderPath = QStringLiteral("/dummy/effect.frag");
        AnimationShaderEffect::ParameterInfo tint;
        tint.id = QStringLiteral("tint");
        tint.type = QStringLiteral("color");
        eff.parameters = {tint};

        // 3-digit `#rgb` expands to `#rrggbb` per Qt's parser.
        {
            const QVariantMap r = AnimationShaderRegistry::translateAnimationParams(
                eff, {{QStringLiteral("tint"), QStringLiteral("#f80")}});
            const QColor c = r.value(QStringLiteral("customColor1")).value<QColor>();
            QVERIFY(c.isValid());
            QCOMPARE(c, QColor(0xff, 0x88, 0x00));
        }
        // 12-bit `#rrrgggbbb` accepted by Qt — extra precision is
        // truncated to 8 bits per channel. Just assert validity here;
        // Qt's exact truncation rule isn't part of this contract.
        {
            const QVariantMap r = AnimationShaderRegistry::translateAnimationParams(
                eff, {{QStringLiteral("tint"), QStringLiteral("#fff888000")}});
            const QColor c = r.value(QStringLiteral("customColor1")).value<QColor>();
            QVERIFY(c.isValid());
        }
        // 16-bit `#rrrrggggbbbb` accepted by Qt.
        {
            const QVariantMap r = AnimationShaderRegistry::translateAnimationParams(
                eff, {{QStringLiteral("tint"), QStringLiteral("#ffff88880000")}});
            const QColor c = r.value(QStringLiteral("customColor1")).value<QColor>();
            QVERIFY(c.isValid());
        }
        // The `"transparent"` keyword resolves to (0,0,0,0).
        {
            const QVariantMap r = AnimationShaderRegistry::translateAnimationParams(
                eff, {{QStringLiteral("tint"), QStringLiteral("transparent")}});
            const QColor c = r.value(QStringLiteral("customColor1")).value<QColor>();
            QVERIFY(c.isValid());
            QCOMPARE(c, QColor(Qt::transparent));
        }
        // 4-digit forms are NOT supported — pin negative coverage so a
        // future regression that "added" 4-digit support without going
        // through the registry's strict default fallback surfaces here.
        // (Without a declared default, an unparseable string falls
        // through to Qt::transparent — the documented sentinel.)
        AnimationShaderEffect noDefault;
        noDefault.id = QStringLiteral("test-color-4digit-rejected");
        noDefault.fragmentShaderPath = QStringLiteral("/dummy/effect.frag");
        AnimationShaderEffect::ParameterInfo bare;
        bare.id = QStringLiteral("tint");
        bare.type = QStringLiteral("color");
        noDefault.parameters = {bare};
        const QVariantMap rReject = AnimationShaderRegistry::translateAnimationParams(
            noDefault, {{QStringLiteral("tint"), QStringLiteral("#8f80")}});
        QCOMPARE(rReject.value(QStringLiteral("customColor1")).value<QColor>(), QColor(Qt::transparent));
    }

    /// Parameter declarations beyond the customParams flat-slot budget
    /// (`kMaxParameterSlots` = 32) are silently dropped at the
    /// `translateAnimationParams` boundary with a `qCWarning`. Without
    /// this test the encoder could regress to wrap-around indexing into
    /// slot 0 (corrupting the first parameter) or to writing a phantom
    /// `customParams9_x` key that consumers never look at. Pin the
    /// budget cap by declaring 33 float params and asserting only 32
    /// land in the result.
    void testTranslateAnimationParamsFloatBudgetOverflow()
    {
        AnimationShaderEffect eff;
        eff.id = QStringLiteral("test-overflow-float");
        eff.fragmentShaderPath = QStringLiteral("/dummy/effect.frag");
        constexpr int kBudget = PhosphorAnimationShaders::AnimationShaderContract::kMaxParameterSlots;
        for (int i = 0; i < kBudget + 1; ++i) {
            AnimationShaderEffect::ParameterInfo p;
            p.id = QStringLiteral("p%1").arg(i);
            p.type = QStringLiteral("float");
            p.defaultValue = double(i);
            eff.parameters.append(p);
        }

        // Expect ONE budget-overflow qCWarning for the 33rd parameter.
        // Using QTest::ignoreMessage so it's scoped to this test only —
        // unlike QLoggingCategory::setFilterRules which mutates global
        // state and would persist if a downstream QVERIFY failed before
        // the restore call. Wrap the literal substrings in
        // QRegularExpression::escape for consistency with the
        // path-traversal-guard tests below — only the `.*` between
        // the escaped halves is treated as a regex metasequence.
        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression(QRegularExpression::escape(QStringLiteral("translateAnimationParams"))
                                                + QStringLiteral(".*")
                                                + QRegularExpression::escape(QStringLiteral("budget"))));
        const QVariantMap r = AnimationShaderRegistry::translateAnimationParams(eff, {});

        // Last in-budget slot (`customParams8_w` per the 1-based +
        // x/y/z/w encoder) populated.
        const QString lastSlot = PhosphorAnimationShaders::AnimationShaderContract::slotKey(kBudget - 1);
        QVERIFY(r.contains(lastSlot));
        // The 33rd param must NOT have produced any key — neither a
        // wrap-around nor a phantom customParams9_x.
        QVERIFY(!r.contains(QStringLiteral("customParams9_x")));
        // Total entries should not exceed the budget.
        QVERIFY(r.size() <= kBudget);
    }

    /// Color declarations beyond the customColors slot budget
    /// (`kMaxCustomColors` = 16) are silently dropped with a `qCWarning`.
    /// Symmetric coverage to the float-budget test above.
    void testTranslateAnimationParamsColorBudgetOverflow()
    {
        AnimationShaderEffect eff;
        eff.id = QStringLiteral("test-overflow-color");
        eff.fragmentShaderPath = QStringLiteral("/dummy/effect.frag");
        constexpr int kBudget = PhosphorAnimationShaders::AnimationShaderContract::kMaxCustomColors;
        for (int i = 0; i < kBudget + 1; ++i) {
            AnimationShaderEffect::ParameterInfo p;
            p.id = QStringLiteral("c%1").arg(i);
            p.type = QStringLiteral("color");
            p.defaultValue = QColor(Qt::red);
            eff.parameters.append(p);
        }

        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression(QRegularExpression::escape(QStringLiteral("translateAnimationParams"))
                                                + QStringLiteral(".*")
                                                + QRegularExpression::escape(QStringLiteral("budget"))));
        const QVariantMap r = AnimationShaderRegistry::translateAnimationParams(eff, {});

        // Last in-budget slot is `customColor16` (1-based).
        QVERIFY(r.contains(QStringLiteral("customColor%1").arg(kBudget)));
        // The 17th color must NOT have produced any key — neither a
        // wrap-around nor a phantom customColor17.
        QVERIFY(!r.contains(QStringLiteral("customColor%1").arg(kBudget + 1)));
        QVERIFY(r.size() <= kBudget);
    }

    // ── User-texture pipeline (kMaxUserTextureSlots = 3) ──────────────
    //
    // Pin the texture-slot surface introduced by the user-texture
    // unification: schema cap, JSON round-trip, equality discrimination,
    // translateAnimationParams emit (pack defaults + runtime overrides),
    // and the texture-only effect case (no float/color params).
    //
    // The earlier audit found this surface had ZERO test coverage. Each
    // case below exists because a regression there has caused observable
    // user-visible breakage at least once during PR #399 development.

    void testTextureSlotCapDropsSurplus()
    {
        // metadata.json declares 4 textures; AnimationShaderContract caps
        // at 3 (one per user-facing iChannel/uTexture slot). Surplus
        // entries are silently dropped at fromJson time.
        QJsonArray texArr;
        for (int i = 0; i < 5; ++i) {
            QJsonObject t;
            t.insert(QLatin1String("path"), QStringLiteral("tex%1.png").arg(i));
            t.insert(QLatin1String("wrap"), QStringLiteral("clamp"));
            texArr.append(t);
        }
        QJsonObject obj;
        obj.insert(QLatin1String("id"), QStringLiteral("over-cap"));
        obj.insert(QLatin1String("fragmentShader"), QStringLiteral("e.frag"));
        obj.insert(QLatin1String("textures"), texArr);

        const auto eff = AnimationShaderEffect::fromJson(obj);
        QCOMPARE(eff.textures.size(), PhosphorAnimationShaders::AnimationShaderContract::kMaxUserTextureSlots);
        // Surviving entries must be the FIRST kMax entries — preserving
        // declaration order so the effect's slot↔file mapping is stable.
        QCOMPARE(eff.textures[0].path, QStringLiteral("tex0.png"));
        QCOMPARE(eff.textures[2].path, QStringLiteral("tex2.png"));
    }

    void testTextureSlotEmptyPathDropped()
    {
        // An author writing `{"path": "", "wrap": "repeat"}` produces a
        // slot with no bound texture. fromJson drops it rather than
        // emitting a wrap-only override that would attach a wrap mode
        // to an unbound sampler.
        QJsonArray texArr;
        QJsonObject empty;
        empty.insert(QLatin1String("path"), QString());
        empty.insert(QLatin1String("wrap"), QStringLiteral("repeat"));
        QJsonObject good;
        good.insert(QLatin1String("path"), QStringLiteral("real.png"));
        texArr.append(empty);
        texArr.append(good);

        QJsonObject obj;
        obj.insert(QLatin1String("id"), QStringLiteral("empty-path"));
        obj.insert(QLatin1String("fragmentShader"), QStringLiteral("e.frag"));
        obj.insert(QLatin1String("textures"), texArr);

        const auto eff = AnimationShaderEffect::fromJson(obj);
        QCOMPARE(eff.textures.size(), 1);
        QCOMPARE(eff.textures[0].path, QStringLiteral("real.png"));
    }

    void testTextureSlotJsonRoundTrip()
    {
        AnimationShaderEffect e;
        e.id = QStringLiteral("rt");
        e.fragmentShaderPath = QStringLiteral("e.frag");
        AnimationShaderEffect::TextureSlot t1{QStringLiteral("a.png"), QStringLiteral("clamp")};
        AnimationShaderEffect::TextureSlot t2{QStringLiteral("b.svg"), QStringLiteral("repeat")};
        e.textures.append(t1);
        e.textures.append(t2);

        const auto restored = AnimationShaderEffect::fromJson(e.toJson());
        QCOMPARE(restored.textures.size(), 2);
        QCOMPARE(restored.textures[0].path, QStringLiteral("a.png"));
        QCOMPARE(restored.textures[0].wrap, QStringLiteral("clamp"));
        QCOMPARE(restored.textures[1].path, QStringLiteral("b.svg"));
        QCOMPARE(restored.textures[1].wrap, QStringLiteral("repeat"));
    }

    void testTextureSlotEqualityDifferentiates()
    {
        AnimationShaderEffect a;
        a.id = QStringLiteral("eq");
        a.fragmentShaderPath = QStringLiteral("e.frag");
        a.textures.append({QStringLiteral("noise.png"), QStringLiteral("repeat")});

        AnimationShaderEffect b = a;
        QVERIFY(a == b);

        // Path differs.
        b.textures[0].path = QStringLiteral("other.png");
        QVERIFY(a != b);
        b = a;

        // Wrap differs.
        b.textures[0].wrap = QStringLiteral("clamp");
        QVERIFY(a != b);
        b = a;

        // Slot count differs.
        b.textures.append({QStringLiteral("more.png"), QStringLiteral("clamp")});
        QVERIFY(a != b);
    }

    void testTranslateAnimationParamsEmitsPackTextureDefaults()
    {
        AnimationShaderEffect eff;
        eff.id = QStringLiteral("texdefault");
        eff.fragmentShaderPath = QStringLiteral("/abs/e.frag");
        eff.sourceDir = QStringLiteral("/abs");
        eff.textures.append({QStringLiteral("/abs/tex.png"), QStringLiteral("repeat")});

        const QVariantMap r = AnimationShaderRegistry::translateAnimationParams(eff, {});
        // Slot 0 → uTexture1 (uTexture0 reserved for surface).
        QCOMPARE(r.value(QStringLiteral("uTexture1")).toString(), QStringLiteral("/abs/tex.png"));
        QCOMPARE(r.value(QStringLiteral("uTexture1_wrap")).toString(), QStringLiteral("repeat"));
    }

    void testTranslateAnimationParamsRuntimeOverrideWinsOverPackDefault()
    {
        AnimationShaderEffect eff;
        eff.id = QStringLiteral("texoverride");
        eff.fragmentShaderPath = QStringLiteral("/abs/e.frag");
        eff.sourceDir = QStringLiteral("/abs");
        eff.textures.append({QStringLiteral("/abs/pack.png"), QStringLiteral("clamp")});

        const QVariantMap friendly{
            {QStringLiteral("uTexture1"), QStringLiteral("/user/override.png")},
            {QStringLiteral("uTexture1_wrap"), QStringLiteral("mirror")},
        };
        const QVariantMap r = AnimationShaderRegistry::translateAnimationParams(eff, friendly);
        QCOMPARE(r.value(QStringLiteral("uTexture1")).toString(), QStringLiteral("/user/override.png"));
        QCOMPARE(r.value(QStringLiteral("uTexture1_wrap")).toString(), QStringLiteral("mirror"));
    }

    void testTranslateAnimationParamsTextureOnlyEffect()
    {
        // A texture-only effect (no parameters declared) used to early-
        // return from translateAnimationParams before the texture loop
        // ran. Pin that the relaxation correctly emits texture keys
        // even when `effect.parameters.isEmpty()`.
        AnimationShaderEffect eff;
        eff.id = QStringLiteral("texonly");
        eff.fragmentShaderPath = QStringLiteral("/abs/e.frag");
        eff.sourceDir = QStringLiteral("/abs");
        eff.textures.append({QStringLiteral("/abs/atlas.png"), QString()});

        const QVariantMap r = AnimationShaderRegistry::translateAnimationParams(eff, {});
        QVERIFY(r.contains(QStringLiteral("uTexture1")));
        QCOMPARE(r.value(QStringLiteral("uTexture1")).toString(), QStringLiteral("/abs/atlas.png"));
        // Empty wrap on the slot — translateAnimationParams omits the
        // `_wrap` key entirely so a consumer's "missing key = no
        // change" semantic preserves whatever default the runtime uses
        // (clamp on both runtimes).
        QVERIFY(!r.contains(QStringLiteral("uTexture1_wrap")));
    }

    void testTranslateAnimationParamsSkipsEmptyPathSlots()
    {
        // No pack default, no override — the slot key must NOT appear in
        // the output map. Otherwise downstream consumers would see an
        // empty `uTexture1` and wipe a previously-bound texture.
        AnimationShaderEffect eff;
        eff.id = QStringLiteral("noslot");
        eff.fragmentShaderPath = QStringLiteral("e.frag");
        // No textures at all.

        const QVariantMap r = AnimationShaderRegistry::translateAnimationParams(eff, {});
        QVERIFY(!r.contains(QStringLiteral("uTexture1")));
        QVERIFY(!r.contains(QStringLiteral("uTexture2")));
        QVERIFY(!r.contains(QStringLiteral("uTexture3")));
    }

    /// Empty-string `uTextureN` override is treated as an explicit
    /// clear: the slot is dropped from the result map entirely. The
    /// pack-default `wrap` value MUST NOT survive into the result —
    /// emitting a wrap-only key would attach a wrap mode to an
    /// unbound sampler, violating the downstream contract. Pin the
    /// exact behaviour: neither `uTexture1` nor `uTexture1_wrap`
    /// appears in the output even though the pack supplied both.
    void testTranslateAnimationParamsEmptyOverrideClearsPackWrap()
    {
        AnimationShaderEffect eff;
        eff.id = QStringLiteral("texclear");
        eff.fragmentShaderPath = QStringLiteral("/abs/e.frag");
        eff.sourceDir = QStringLiteral("/abs");
        // Pack default supplies BOTH path and wrap.
        eff.textures.append({QStringLiteral("/abs/pack.png"), QStringLiteral("repeat")});

        const QVariantMap friendly{
            {QStringLiteral("uTexture1"), QString()}, // explicit empty-string clear
        };
        const QVariantMap r = AnimationShaderRegistry::translateAnimationParams(eff, friendly);

        // (a) explicit empty-string override clears the texture slot.
        QVERIFY(!r.contains(QStringLiteral("uTexture1")));
        // (b) pack's wrap value does not appear in the result `wrap` slot.
        QVERIFY(!r.contains(QStringLiteral("uTexture1_wrap")));
    }

    /// Dual-key edge case for the empty-string override + companion
    /// wrap-override interaction. A caller that supplies BOTH
    /// `uTexture1 = ""` (explicit clear) AND `uTexture1_wrap = "repeat"`
    /// in the same friendlyParams map must end up with NEITHER key in
    /// the result: the empty path triggers the "skip both keys" branch
    /// at the end of the texture loop, which dominates the wrap
    /// override's reassignment. Pin this so a future edit that adds an
    /// early `wrap.clear()` in the empty-path branch (which would be
    /// silently overwritten by the wrap override below it) doesn't
    /// accidentally regress the orphan-wrap-emit guard.
    void testTranslateAnimationParamsEmptyOverridePlusWrapOverrideDropsBoth()
    {
        AnimationShaderEffect eff;
        eff.id = QStringLiteral("texdualclear");
        eff.fragmentShaderPath = QStringLiteral("/abs/e.frag");
        eff.sourceDir = QStringLiteral("/abs");
        eff.textures.append({QStringLiteral("/abs/pack.png"), QStringLiteral("clamp")});

        const QVariantMap friendly{
            {QStringLiteral("uTexture1"), QString()}, // explicit clear
            {QStringLiteral("uTexture1_wrap"), QStringLiteral("repeat")}, // companion wrap override
        };
        const QVariantMap r = AnimationShaderRegistry::translateAnimationParams(eff, friendly);

        // BOTH keys absent — the empty-path skip below the wrap-
        // override dominates regardless of QVariantMap iteration order.
        QVERIFY(!r.contains(QStringLiteral("uTexture1")));
        QVERIFY(!r.contains(QStringLiteral("uTexture1_wrap")));
    }

    void testTranslateAnimationParamsWrapOnlyOverrideRequiresPath()
    {
        // friendlyParams contains `uTexture1_wrap` but neither the pack
        // nor friendlyParams supplies a path → wrap key is silently
        // skipped (orphan wrap on an unbound sampler is a contract
        // violation downstream).
        AnimationShaderEffect eff;
        eff.id = QStringLiteral("orphanwrap");
        eff.fragmentShaderPath = QStringLiteral("e.frag");

        const QVariantMap friendly{
            {QStringLiteral("uTexture1_wrap"), QStringLiteral("repeat")},
        };
        const QVariantMap r = AnimationShaderRegistry::translateAnimationParams(eff, friendly);
        QVERIFY(!r.contains(QStringLiteral("uTexture1")));
        QVERIFY(!r.contains(QStringLiteral("uTexture1_wrap")));
    }
};

QTEST_MAIN(TestShaderParamTranslation)
#include "test_shaderparamtranslation.moc"
