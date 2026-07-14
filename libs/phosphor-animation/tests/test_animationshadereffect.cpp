// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/AnimationShaderEffect.h>
#include <PhosphorAnimation/ProfilePaths.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QTest>

using PhosphorAnimationShaders::AnimationShaderEffect;

class TestAnimationShaderEffect : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void testDefaultIsInvalid()
    {
        AnimationShaderEffect e;
        QVERIFY(!e.isValid());
    }

    void testValidWithIdAndFragmentShader()
    {
        AnimationShaderEffect e;
        e.id = QStringLiteral("dissolve");
        e.fragmentShaderPath = QStringLiteral("effect.frag");
        QVERIFY(e.isValid());
    }

    void testJsonRoundTrip()
    {
        AnimationShaderEffect original;
        original.id = QStringLiteral("slide");
        original.name = QStringLiteral("Slide");
        original.description = QStringLiteral("Slide transition");
        original.author = QStringLiteral("Phosphor");
        original.version = QStringLiteral("1.0");
        original.category = QStringLiteral("Geometric");
        original.fragmentShaderPath = QStringLiteral("effect.frag");
        original.vertexShaderPath = QStringLiteral("effect.vert");
        original.previewPath = QStringLiteral("preview.png");

        AnimationShaderEffect::ParameterInfo p;
        p.id = QStringLiteral("direction");
        p.name = QStringLiteral("Direction");
        p.type = QStringLiteral("float");
        p.defaultValue = 0.0;
        p.minValue = 0.0;
        p.maxValue = 3.0;
        original.parameters.append(p);

        const QJsonObject json = original.toJson();
        const AnimationShaderEffect restored = AnimationShaderEffect::fromJson(json);

        QCOMPARE(restored, original);
    }

    void testJsonPreservesParameters()
    {
        AnimationShaderEffect e;
        e.id = QStringLiteral("glitch");
        e.fragmentShaderPath = QStringLiteral("effect.frag");

        AnimationShaderEffect::ParameterInfo p1;
        p1.id = QStringLiteral("intensity");
        p1.name = QStringLiteral("Intensity");
        p1.type = QStringLiteral("float");
        p1.defaultValue = 0.5;

        AnimationShaderEffect::ParameterInfo p2;
        p2.id = QStringLiteral("blockSize");
        p2.name = QStringLiteral("Block Size");
        p2.type = QStringLiteral("int");
        p2.defaultValue = 8;
        p2.minValue = 1;
        p2.maxValue = 64;

        e.parameters = {p1, p2};

        const AnimationShaderEffect restored = AnimationShaderEffect::fromJson(e.toJson());
        QCOMPARE(restored.parameters.size(), 2);
        QCOMPARE(restored.parameters[0].id, QStringLiteral("intensity"));
        QCOMPARE(restored.parameters[1].id, QStringLiteral("blockSize"));
    }

    void testFromJsonMissingFields()
    {
        QJsonObject obj;
        obj.insert(QLatin1String("id"), QStringLiteral("test"));

        const AnimationShaderEffect e = AnimationShaderEffect::fromJson(obj);
        QCOMPARE(e.id, QStringLiteral("test"));
        QVERIFY(e.fragmentShaderPath.isEmpty());
        QVERIFY(!e.isValid());
    }

    void testEquality()
    {
        AnimationShaderEffect a;
        a.id = QStringLiteral("dissolve");
        a.fragmentShaderPath = QStringLiteral("effect.frag");

        AnimationShaderEffect b = a;
        QCOMPARE(a, b);

        b.id = QStringLiteral("morph");
        QVERIFY(a != b);

        // operator== must observe appliesTo: two effects differing ONLY in
        // their declared event classes are not equal. Without the dedicated
        // branch this would falsely compare equal.
        AnimationShaderEffect c = a;
        AnimationShaderEffect d = a;
        d.appliesTo = QStringList{QStringLiteral("geometry")};
        QVERIFY(c != d);
    }

    /// Multipass / wallpaper / depth / buffer fields survive a full
    /// `toJson()` → `fromJson()` round-trip. The PR that promoted the
    /// animation UBO to the full BaseUniforms layout added every field
    /// listed below; without coverage here, a regression that dropped
    /// (say) `bufferWraps` from the JSON serialisation would silently
    /// degrade multipass animation packs to single-pass on the daemon
    /// path with no test signal.
    void testJsonPreservesMultipassFields()
    {
        AnimationShaderEffect original;
        original.id = QStringLiteral("multipass-test");
        original.fragmentShaderPath = QStringLiteral("effect.frag");
        original.isMultipass = true;
        original.bufferShaderPaths = {QStringLiteral("buffer-a.frag"), QStringLiteral("buffer-b.frag")};
        original.useWallpaper = true;
        original.bufferFeedback = true;
        original.bufferScale = 0.5;
        original.bufferWrap = QStringLiteral("repeat");
        original.bufferWraps = {QStringLiteral("clamp"), QStringLiteral("mirror")};
        original.bufferFilter = QStringLiteral("linear");
        original.bufferFilters = {QStringLiteral("nearest"), QStringLiteral("linear")};
        original.useDepthBuffer = true;
        original.useAudio = true;
        original.geometryGridSubdivisions = 40;

        const AnimationShaderEffect restored = AnimationShaderEffect::fromJson(original.toJson());
        QCOMPARE(restored, original);
    }

    /// `geometryGrid` is the per-axis quad subdivision count for
    /// vertex-stage geometry shaders. It is clamped to >= 0 at parse time
    /// (a negative count is meaningless) and omitted from `toJson` when 0
    /// (terse-by-default, same idiom as `fboExtent` / `multipass`). Pin
    /// both so a regression that drops the clamp or the omit surfaces in
    /// CI rather than as a broken geometry transition.
    void testFromJsonGeometryGrid()
    {
        QJsonObject obj;
        obj.insert(QLatin1String("id"), QStringLiteral("test"));
        obj.insert(QLatin1String("fragmentShader"), QStringLiteral("effect.frag"));

        // Negative clamps to 0.
        obj.insert(QLatin1String("geometryGrid"), -5);
        QCOMPARE(AnimationShaderEffect::fromJson(obj).geometryGridSubdivisions, 0);

        // A positive value round-trips and is emitted.
        obj.insert(QLatin1String("geometryGrid"), 48);
        const AnimationShaderEffect parsed = AnimationShaderEffect::fromJson(obj);
        QCOMPARE(parsed.geometryGridSubdivisions, 48);
        QCOMPARE(parsed.toJson().value(QLatin1String("geometryGrid")).toInt(), 48);

        // Zero (the default) is omitted from toJson entirely.
        AnimationShaderEffect zero;
        zero.id = QStringLiteral("test");
        zero.fragmentShaderPath = QStringLiteral("effect.frag");
        QVERIFY(!zero.toJson().contains(QLatin1String("geometryGrid")));
    }

    /// `bufferScale` is clamped to `[0.125, 1.0]` at parse time so a
    /// metadata.json author can't accidentally allocate gigabyte-sized
    /// FBOs by writing a >1.0 multiplier or render at sub-pixel scales
    /// that produce visible aliasing. The field still round-trips
    /// for any in-range value (covered above); pin the clamp here so a
    /// regression that removed it surfaces directly.
    void testFromJsonClampsBufferScale()
    {
        QJsonObject obj;
        obj.insert(QLatin1String("id"), QStringLiteral("test"));
        obj.insert(QLatin1String("fragmentShader"), QStringLiteral("effect.frag"));
        obj.insert(QLatin1String("bufferScale"), 5.0);
        const AnimationShaderEffect overshoot = AnimationShaderEffect::fromJson(obj);
        QCOMPARE(overshoot.bufferScale, qreal(1.0));

        obj.insert(QLatin1String("bufferScale"), 0.001);
        const AnimationShaderEffect undershoot = AnimationShaderEffect::fromJson(obj);
        QCOMPARE(undershoot.bufferScale, qreal(0.125));
    }

    /// `fboExtent` grammar parser coverage. Accepts exactly two forms
    /// — `"anchor"` and `"surface"` — case-insensitive. Anything else
    /// (including the legacy `anchor+N` ring-fraction grammar) is
    /// rejected with a journal warning and falls back to the default
    /// (Anchor). Driven by a `_data()` table so a regression that
    /// silently accepts a malformed form surfaces in CI rather than
    /// waiting on a visual regression.
    void testFromJsonFboExtent_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<AnimationShaderEffect::FboExtentKind>("expectedKind");

        const auto kAnchor = AnimationShaderEffect::FboExtentKind::Anchor;
        const auto kSurface = AnimationShaderEffect::FboExtentKind::Surface;

        // Accepted grammar.
        QTest::newRow("anchor") << QStringLiteral("anchor") << kAnchor;
        QTest::newRow("surface") << QStringLiteral("surface") << kSurface;
        QTest::newRow("anchor-uppercase") << QStringLiteral("ANCHOR") << kAnchor;
        QTest::newRow("surface-mixed") << QStringLiteral("Surface") << kSurface;
        QTest::newRow("leading-ws") << QStringLiteral("  anchor") << kAnchor;

        // Malformed / unsupported values: parser falls back to the
        // struct default (Anchor) and emits a journal warning. The
        // legacy `anchor+N` ring grammar lives in this bucket since
        // ring expansion is no longer supported on either runtime.
        QTest::newRow("empty") << QString() << kAnchor;
        QTest::newRow("whitespace-only") << QStringLiteral("   ") << kAnchor;
        QTest::newRow("garbage") << QStringLiteral("foo") << kAnchor;
        QTest::newRow("legacy-anchor+0.5") << QStringLiteral("anchor+0.5") << kAnchor;
        QTest::newRow("legacy-anchor+50%") << QStringLiteral("anchor+50%") << kAnchor;
    }

    void testFromJsonFboExtent()
    {
        QFETCH(QString, input);
        QFETCH(AnimationShaderEffect::FboExtentKind, expectedKind);

        QJsonObject obj;
        obj.insert(QLatin1String("id"), QStringLiteral("test"));
        obj.insert(QLatin1String("fragmentShader"), QStringLiteral("effect.frag"));
        obj.insert(QLatin1String("fboExtent"), input);
        const AnimationShaderEffect e = AnimationShaderEffect::fromJson(obj);
        QCOMPARE(e.fboExtentKind, expectedKind);
    }

    /// Round-trip Surface extent through JSON. toJson emits
    /// `"fboExtent": "surface"`; fromJson reads it back.
    void testFboExtentSurfaceRoundTrip()
    {
        AnimationShaderEffect original;
        original.id = QStringLiteral("fly-in");
        original.fragmentShaderPath = QStringLiteral("effect.frag");
        original.fboExtentKind = AnimationShaderEffect::FboExtentKind::Surface;

        const AnimationShaderEffect restored = AnimationShaderEffect::fromJson(original.toJson());
        QCOMPARE(restored.fboExtentKind, AnimationShaderEffect::FboExtentKind::Surface);
    }

    /// `appliesTo` round-trips through JSON, and an unset list stays empty
    /// (universal) without emitting the key.
    void testAppliesToRoundTrip()
    {
        AnimationShaderEffect original;
        original.id = QStringLiteral("window-morph");
        original.fragmentShaderPath = QStringLiteral("effect.frag");
        original.appliesTo = QStringList{QStringLiteral("geometry")};

        const QJsonObject json = original.toJson();
        QVERIFY(json.contains(QLatin1String("appliesTo")));
        const AnimationShaderEffect restored = AnimationShaderEffect::fromJson(json);
        QCOMPARE(restored.appliesTo, original.appliesTo);

        // Universal (empty) effect omits the key entirely.
        AnimationShaderEffect universal;
        universal.id = QStringLiteral("fade");
        universal.fragmentShaderPath = QStringLiteral("effect.frag");
        QVERIFY(!universal.toJson().contains(QLatin1String("appliesTo")));
        QVERIFY(AnimationShaderEffect::fromJson(universal.toJson()).appliesTo.isEmpty());

        // The opt-in classes round-trip too — a serializer regression that
        // handled only "geometry" would strip a desktop or move pack's
        // constraint on save, turning it universal (and then refused
        // everywhere by the opt-in rule).
        AnimationShaderEffect moveOnly;
        moveOnly.id = QStringLiteral("wobble");
        moveOnly.fragmentShaderPath = QStringLiteral("effect.frag");
        moveOnly.appliesTo = QStringList{QStringLiteral("move")};
        QCOMPARE(AnimationShaderEffect::fromJson(moveOnly.toJson()).appliesTo, moveOnly.appliesTo);

        AnimationShaderEffect desktopOnly;
        desktopOnly.id = QStringLiteral("desktop-cube");
        desktopOnly.fragmentShaderPath = QStringLiteral("effect.frag");
        desktopOnly.appliesTo = QStringList{QStringLiteral("desktop")};
        QCOMPARE(AnimationShaderEffect::fromJson(desktopOnly.toJson()).appliesTo, desktopOnly.appliesTo);
    }

    /// Unknown / duplicate tokens are dropped at parse time; a list that
    /// validates down to empty is treated as universal.
    void testAppliesToValidation()
    {
        QJsonObject obj;
        obj.insert(QLatin1String("id"), QStringLiteral("x"));
        obj.insert(QLatin1String("fragmentShader"), QStringLiteral("effect.frag"));
        QJsonArray arr;
        arr.append(QStringLiteral("geometry"));
        arr.append(QStringLiteral("geometry")); // duplicate
        arr.append(QStringLiteral("teleport")); // unknown
        obj.insert(QLatin1String("appliesTo"), arr);

        const AnimationShaderEffect e = AnimationShaderEffect::fromJson(obj);
        QCOMPARE(e.appliesTo, (QStringList{QStringLiteral("geometry")}));

        // "desktop" is part of the accepted vocabulary (the two-texture switch
        // contract). A regression that dropped it would silently discard every
        // desktop pack's constraint, making it universal (and then refused
        // everywhere by the opt-in rule), so pin it explicitly.
        QJsonObject desktopObj;
        desktopObj.insert(QLatin1String("id"), QStringLiteral("d"));
        desktopObj.insert(QLatin1String("fragmentShader"), QStringLiteral("effect.frag"));
        QJsonArray desktopArr;
        desktopArr.append(QStringLiteral("desktop"));
        desktopObj.insert(QLatin1String("appliesTo"), desktopArr);
        QCOMPARE(AnimationShaderEffect::fromJson(desktopObj).appliesTo, (QStringList{QStringLiteral("desktop")}));

        // "move" (the held interactive drag) is part of the accepted vocabulary.
        QJsonObject moveObj;
        moveObj.insert(QLatin1String("id"), QStringLiteral("m"));
        moveObj.insert(QLatin1String("fragmentShader"), QStringLiteral("effect.frag"));
        QJsonArray moveArr;
        moveArr.append(QStringLiteral("move"));
        moveObj.insert(QLatin1String("appliesTo"), moveArr);
        QCOMPARE(AnimationShaderEffect::fromJson(moveObj).appliesTo, (QStringList{QStringLiteral("move")}));

        QJsonObject allBad = obj;
        QJsonArray bad;
        bad.append(QStringLiteral("nonsense"));
        allBad.insert(QLatin1String("appliesTo"), bad);
        QVERIFY(AnimationShaderEffect::fromJson(allBad).appliesTo.isEmpty());

        // A non-array value (scalar where an array is expected) and non-string
        // array elements are both hand-authoring mistakes — neither should
        // throw or smuggle garbage in; the field reduces to universal/valid.
        QJsonObject scalar = obj;
        scalar.insert(QLatin1String("appliesTo"), QStringLiteral("geometry"));
        QVERIFY(AnimationShaderEffect::fromJson(scalar).appliesTo.isEmpty());

        QJsonObject mixed;
        mixed.insert(QLatin1String("id"), QStringLiteral("y"));
        mixed.insert(QLatin1String("fragmentShader"), QStringLiteral("effect.frag"));
        QJsonArray mixedArr;
        mixedArr.append(QStringLiteral("geometry"));
        mixedArr.append(7); // numeric element silently skipped
        mixed.insert(QLatin1String("appliesTo"), mixedArr);
        QCOMPARE(AnimationShaderEffect::fromJson(mixed).appliesTo, (QStringList{QStringLiteral("geometry")}));
    }

    /// The (effect × path) predicate: a geometry-only effect is compatible
    /// with geometry legs, incompatible with appearance legs, and a
    /// universal effect is compatible everywhere. An ambiguous row (the
    /// mixed `window` root) is never reported incompatible.
    void testShaderEffectAppliesToEventPath()
    {
        using PhosphorAnimationShaders::shaderEffectAppliesToEventPath;

        AnimationShaderEffect morph;
        morph.id = QStringLiteral("window-morph");
        morph.fragmentShaderPath = QStringLiteral("effect.frag");
        morph.appliesTo = QStringList{QStringLiteral("geometry")};

        namespace PP = PhosphorAnimation::ProfilePaths;
        // Every geometry leg eventClassForPath classifies must be compatible with
        // a geometry-only effect. Reference the taxonomy constants (not literals)
        // so a leaf rename can't silently keep these passing, and pin the full
        // disjunction so dropping any leg from the classifier is caught.
        // `WindowMove` is NOT in this set: the interactive-drag leaf is its own
        // opt-in `move` class (see the move-effect block below). Neither is
        // `WindowMovement` — geometry-classed, but a cascade parent rather
        // than a leg; it gets its own compatibility check further down. There
        // are no resize legs at all — the interactive-resize and snapResize
        // events were dropped from the taxonomy.
        for (const QString& geo : {PP::WindowSnapIn, PP::WindowSnapOut, PP::WindowLayoutSwitch, PP::WindowMaximize}) {
            QVERIFY2(shaderEffectAppliesToEventPath(morph, geo), qPrintable(geo));
        }
        // The geometry-classed cascade parent accepts a geometry effect too
        // (it is a category row, not a leg — the move-only refusal on the
        // same row is pinned in the move-effect block below).
        QVERIFY(shaderEffectAppliesToEventPath(morph, PP::WindowMovement));
        // Every appearance leg must be incompatible with a geometry-only effect.
        for (const QString& app : {PP::WindowOpen, PP::WindowClose, PP::WindowMinimize, PP::WindowFocus, PP::OsdShow,
                                   PP::OsdHide, PP::PopupLayoutPickerShow, PP::PopupZoneSelectorHide}) {
            QVERIFY2(!shaderEffectAppliesToEventPath(morph, app), qPrintable(app));
        }
        // Unclassified paths (mixed `window` root, non-window families) are
        // never provably incompatible — the predicate stays permissive.
        QVERIFY(shaderEffectAppliesToEventPath(morph, PP::Window));
        QVERIFY(shaderEffectAppliesToEventPath(morph, PP::EditorSnapIn));
        QVERIFY(shaderEffectAppliesToEventPath(morph, PP::PanelSlideIn));

        AnimationShaderEffect fade; // universal (no appliesTo)
        fade.id = QStringLiteral("fade");
        fade.fragmentShaderPath = QStringLiteral("effect.frag");
        QVERIFY(shaderEffectAppliesToEventPath(fade, PP::WindowOpen));
        QVERIFY(shaderEffectAppliesToEventPath(fade, PP::WindowSnapIn));
        // The desktop class is opt-in: a universal single-surface effect must NOT
        // bleed onto a desktop path (its lone surface sampler would be unbound).
        QVERIFY(!shaderEffectAppliesToEventPath(fade, PP::DesktopSwitch));
        QVERIFY(!shaderEffectAppliesToEventPath(fade, PP::DesktopPeek));
        QVERIFY(!shaderEffectAppliesToEventPath(fade, PP::Desktop));
        // The move class is opt-in for the same structural reason: a universal
        // pack cannot drive the held interactive drag.
        QVERIFY(!shaderEffectAppliesToEventPath(fade, PP::WindowMove));

        // Appearance-only effect: mirror image — incompatible on geometry legs,
        // compatible on appearance legs.
        AnimationShaderEffect appearanceOnly;
        appearanceOnly.id = QStringLiteral("aretha-materialize");
        appearanceOnly.fragmentShaderPath = QStringLiteral("effect.frag");
        appearanceOnly.appliesTo = QStringList{QStringLiteral("appearance")};
        QVERIFY(shaderEffectAppliesToEventPath(appearanceOnly, PP::WindowOpen));
        QVERIFY(!shaderEffectAppliesToEventPath(appearanceOnly, PP::WindowSnapIn));
        // A single-surface (non-desktop) effect never runs on a desktop path.
        QVERIFY(!shaderEffectAppliesToEventPath(appearanceOnly, PP::DesktopSwitch));

        // Desktop two-texture effect: accepted ONLY on desktop paths, refused on
        // every single-surface (window / OSD) leg.
        AnimationShaderEffect desktop;
        desktop.id = QStringLiteral("desktop-cube");
        desktop.fragmentShaderPath = QStringLiteral("effect.frag");
        desktop.appliesTo = QStringList{QStringLiteral("desktop")};
        QVERIFY(shaderEffectAppliesToEventPath(desktop, PP::DesktopSwitch));
        // The show-desktop peek leaf accepts the same desktop-contract packs.
        QVERIFY(shaderEffectAppliesToEventPath(desktop, PP::DesktopPeek));
        QVERIFY(shaderEffectAppliesToEventPath(desktop, PP::Desktop));
        QVERIFY(!shaderEffectAppliesToEventPath(desktop, PP::WindowOpen));
        QVERIFY(!shaderEffectAppliesToEventPath(desktop, PP::WindowMove));
        QVERIFY(!shaderEffectAppliesToEventPath(desktop, PP::OsdShow));
        // Also refused on AMBIGUOUS rows (empty class): the mixed `window` root
        // and the `global` baseline. A two-texture desktop pack must never be
        // offered on a non-desktop row, even one that resolves to no class —
        // symmetric with the universal-effect exclusion from desktop paths.
        QVERIFY(!shaderEffectAppliesToEventPath(desktop, PP::Window));
        QVERIFY(!shaderEffectAppliesToEventPath(desktop, PP::Global));
        // A universal effect stays permissive on those same ambiguous rows.
        QVERIFY(shaderEffectAppliesToEventPath(fade, PP::Window));
        QVERIFY(shaderEffectAppliesToEventPath(fade, PP::Global));

        // Move (interactive drag) effect: opt-in exactly like desktop.
        // Accepted only on the move leaf; refused on the crossfade movement
        // legs and their cascade parent, appearance legs, desktop paths, and
        // ambiguous rows (the move leaf takes no inherited shader, so a
        // move-only pack on an ancestor row is provably runtime-dead).
        AnimationShaderEffect moveOnly;
        moveOnly.id = QStringLiteral("wobble");
        moveOnly.fragmentShaderPath = QStringLiteral("effect.frag");
        moveOnly.appliesTo = QStringList{QStringLiteral("move")};
        QVERIFY(shaderEffectAppliesToEventPath(moveOnly, PP::WindowMove));
        QVERIFY(!shaderEffectAppliesToEventPath(moveOnly, PP::WindowMovement));
        QVERIFY(!shaderEffectAppliesToEventPath(moveOnly, PP::WindowSnapIn));
        QVERIFY(!shaderEffectAppliesToEventPath(moveOnly, PP::WindowOpen));
        QVERIFY(!shaderEffectAppliesToEventPath(moveOnly, PP::DesktopSwitch));
        QVERIFY(!shaderEffectAppliesToEventPath(moveOnly, PP::Window));
        QVERIFY(!shaderEffectAppliesToEventPath(moveOnly, PP::Global));
        // Geometry-only and appearance-only effects are refused on the move
        // leaf (opt-in in both directions).
        QVERIFY(!shaderEffectAppliesToEventPath(morph, PP::WindowMove));
        QVERIFY(!shaderEffectAppliesToEventPath(appearanceOnly, PP::WindowMove));
        // A hybrid declaring geometry AND move drives both sides and stays
        // available on ambiguous rows (it can feed the geometry legs there).
        AnimationShaderEffect hybrid;
        hybrid.id = QStringLiteral("hybrid");
        hybrid.fragmentShaderPath = QStringLiteral("effect.frag");
        hybrid.appliesTo = QStringList{QStringLiteral("geometry"), QStringLiteral("move")};
        QVERIFY(shaderEffectAppliesToEventPath(hybrid, PP::WindowMove));
        QVERIFY(shaderEffectAppliesToEventPath(hybrid, PP::WindowSnapIn));
        QVERIFY(shaderEffectAppliesToEventPath(hybrid, PP::Window));
        QVERIFY(!shaderEffectAppliesToEventPath(hybrid, PP::WindowOpen));
        // A hybrid that does NOT declare "desktop" is still refused on the
        // desktop paths — the geometry/move branches must not leak onto the
        // two-texture switch, whose samplers a single-surface pack never binds.
        QVERIFY(!shaderEffectAppliesToEventPath(hybrid, PP::DesktopSwitch));
        QVERIFY(!shaderEffectAppliesToEventPath(hybrid, PP::Desktop));
    }
};

QTEST_MAIN(TestAnimationShaderEffect)
#include "test_animationshadereffect.moc"
