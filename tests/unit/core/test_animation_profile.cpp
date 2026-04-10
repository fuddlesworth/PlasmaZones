// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_animation_profile.cpp
 * @brief Unit tests for AnimationProfile, SpringParams, and AnimationProfileTree
 *
 * Validates:
 * - SpringParams default values, equality, JSON round-trip
 * - AnimationProfile emptiness, mergeFrom semantics, JSON round-trip, equality
 * - AnimationProfileTree 4-level hierarchy, inheritance resolution, JSON round-trip
 * - animationStyleToString / animationStyleFromString conversions (including overlay styles)
 */

#include <QTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include "../../../src/core/animationprofile.h"
#include "../../../src/common/animationtreedata.h"

using namespace PlasmaZones;

class TestAnimationProfile : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // =====================================================================
    // SpringParams
    // =====================================================================

    void springParams_defaultValues()
    {
        SpringParams sp;
        QVERIFY(qFuzzyCompare(1.0 + sp.dampingRatio, 1.0 + 1.0));
        QVERIFY(qFuzzyCompare(1.0 + sp.stiffness, 1.0 + 800.0));
        QVERIFY(qFuzzyCompare(1.0 + sp.epsilon, 1.0 + 0.0001));
    }

    void springParams_equality()
    {
        SpringParams a;
        SpringParams b;
        QVERIFY(a == b);

        b.dampingRatio = 0.5;
        QVERIFY(a != b);
    }

    void springParams_jsonRoundTrip()
    {
        SpringParams original;
        original.dampingRatio = 0.7;
        original.stiffness = 1200.0;
        original.epsilon = 0.01;

        QJsonObject json = springParamsToJson(original);
        SpringParams restored = springParamsFromJson(json);

        QCOMPARE(original, restored);
    }

    void springParams_fromJsonEmptyObject()
    {
        // Empty JSON object should produce default values
        QJsonObject empty;
        SpringParams sp = springParamsFromJson(empty);

        SpringParams defaults;
        QCOMPARE(sp, defaults);
    }

    void springParams_fromJsonPartial()
    {
        // Only set stiffness; others should remain at defaults
        QJsonObject partial;
        partial[QLatin1String("stiffness")] = 400.0;

        SpringParams sp = springParamsFromJson(partial);
        QVERIFY(qFuzzyCompare(1.0 + sp.dampingRatio, 1.0 + 1.0));
        QVERIFY(qFuzzyCompare(1.0 + sp.stiffness, 1.0 + 400.0));
        QVERIFY(qFuzzyCompare(1.0 + sp.epsilon, 1.0 + 0.0001));
    }

    // =====================================================================
    // AnimationStyle string conversions
    // =====================================================================

    void animationStyle_roundTripAllValues()
    {
        const QList<AnimationStyle> allStyles = {
            AnimationStyle::Morph,     AnimationStyle::Slide,   AnimationStyle::Popin,
            AnimationStyle::SlideFade, AnimationStyle::None,    AnimationStyle::Custom,
            AnimationStyle::FadeIn,    AnimationStyle::SlideUp, AnimationStyle::ScaleIn,
        };

        for (AnimationStyle style : allStyles) {
            QString str = animationStyleToString(style);
            AnimationStyle back = animationStyleFromString(str);
            QCOMPARE(back, style);
        }
    }

    void animationStyle_unknownStringReturnsMorph()
    {
        QCOMPARE(animationStyleFromString(QStringLiteral("garbage")), AnimationStyle::Morph);
        QCOMPARE(animationStyleFromString(QStringLiteral("foo")), AnimationStyle::Morph);
    }

    void animationStyle_emptyStringReturnsMorph()
    {
        QCOMPARE(animationStyleFromString(QString()), AnimationStyle::Morph);
    }

    void animationStyle_overlayStyleStrings()
    {
        QCOMPARE(animationStyleToString(AnimationStyle::FadeIn), QStringLiteral("fadein"));
        QCOMPARE(animationStyleToString(AnimationStyle::SlideUp), QStringLiteral("slideup"));
        QCOMPARE(animationStyleToString(AnimationStyle::ScaleIn), QStringLiteral("scalein"));
        QCOMPARE(animationStyleFromString(QStringLiteral("fadein")), AnimationStyle::FadeIn);
        QCOMPARE(animationStyleFromString(QStringLiteral("slideup")), AnimationStyle::SlideUp);
        QCOMPARE(animationStyleFromString(QStringLiteral("scalein")), AnimationStyle::ScaleIn);
    }

    void animationStyle_domainClassification()
    {
        QCOMPARE(animationStyleDomain(AnimationStyle::Morph), QStringLiteral("window"));
        QCOMPARE(animationStyleDomain(AnimationStyle::Slide), QStringLiteral("window"));
        QCOMPARE(animationStyleDomain(AnimationStyle::FadeIn), QStringLiteral("overlay"));
        QCOMPARE(animationStyleDomain(AnimationStyle::ScaleIn), QStringLiteral("overlay"));
        QCOMPARE(animationStyleDomain(AnimationStyle::None), QStringLiteral("both"));
        QCOMPARE(animationStyleDomain(AnimationStyle::Custom), QStringLiteral("both"));
    }

    // =====================================================================
    // AnimationProfile — default construction / isEmpty
    // =====================================================================

    void profile_defaultIsEmpty()
    {
        AnimationProfile p;
        QVERIFY(p.isEmpty());
        QVERIFY(!p.enabled.has_value());
        QVERIFY(!p.timingMode.has_value());
        QVERIFY(!p.duration.has_value());
        QVERIFY(!p.easingCurve.has_value());
        QVERIFY(!p.spring.has_value());
        QVERIFY(!p.style.has_value());
        QVERIFY(!p.styleParam.has_value());
        QVERIFY(!p.shaderPath.has_value());
        QVERIFY(!p.shaderParams.has_value());
    }

    void profile_settingFieldMakesNonEmpty()
    {
        {
            AnimationProfile p;
            p.enabled = true;
            QVERIFY(!p.isEmpty());
        }
        {
            AnimationProfile p;
            p.duration = 200;
            QVERIFY(!p.isEmpty());
        }
        {
            AnimationProfile p;
            p.style = AnimationStyle::Slide;
            QVERIFY(!p.isEmpty());
        }
        {
            AnimationProfile p;
            p.spring = SpringParams{};
            QVERIFY(!p.isEmpty());
        }
        {
            AnimationProfile p;
            p.shaderPath = QStringLiteral("shaders/custom.glsl");
            QVERIFY(!p.isEmpty());
        }
    }

    // =====================================================================
    // AnimationProfile — mergeFrom
    // =====================================================================

    void profile_mergeFrom_sourceOverridesNullopt()
    {
        AnimationProfile target;
        AnimationProfile source;
        source.duration = 500;
        source.enabled = true;

        target.mergeFrom(source);

        QVERIFY(target.duration.has_value());
        QCOMPARE(*target.duration, 500);
        QVERIFY(target.enabled.has_value());
        QCOMPARE(*target.enabled, true);
    }

    void profile_mergeFrom_nulloptDoesNotOverride()
    {
        AnimationProfile target;
        target.duration = 300;

        AnimationProfile source;
        // source.duration is nullopt

        target.mergeFrom(source);

        QVERIFY(target.duration.has_value());
        QCOMPARE(*target.duration, 300);
    }

    void profile_mergeFrom_bothSetSourceWins()
    {
        AnimationProfile target;
        target.duration = 300;

        AnimationProfile source;
        source.duration = 500;

        target.mergeFrom(source);

        QCOMPARE(*target.duration, 500);
    }

    void profile_mergeFrom_bothNulloptStaysNullopt()
    {
        AnimationProfile target;
        AnimationProfile source;

        target.mergeFrom(source);

        QVERIFY(!target.duration.has_value());
        QVERIFY(target.isEmpty());
    }

    // =====================================================================
    // AnimationProfile — JSON round-trip
    // =====================================================================

    void profile_jsonRoundTrip_allFields()
    {
        AnimationProfile original;
        original.enabled = true;
        original.timingMode = TimingMode::Spring;
        original.duration = 400;
        original.easingCurve = QStringLiteral("0.25,0.1,0.25,1.0");
        original.spring = SpringParams{0.8, 600.0, 0.001};
        original.style = AnimationStyle::Popin;
        original.styleParam = 0.5;
        original.shaderPath = QStringLiteral("shaders/custom.glsl");
        QVariantMap params;
        params[QStringLiteral("intensity")] = 0.9;
        original.shaderParams = params;

        QJsonObject json = original.toJson();
        AnimationProfile restored = AnimationProfile::fromJson(json);

        QCOMPARE(original, restored);
    }

    void profile_jsonPartialFields()
    {
        // Only set some fields
        AnimationProfile original;
        original.duration = 250;
        original.style = AnimationStyle::Slide;

        QJsonObject json = original.toJson();

        // Verify only set fields appear in JSON
        QVERIFY(json.contains(QLatin1String("duration")));
        QVERIFY(json.contains(QLatin1String("style")));
        QVERIFY(!json.contains(QLatin1String("enabled")));
        QVERIFY(!json.contains(QLatin1String("timingMode")));
        QVERIFY(!json.contains(QLatin1String("spring")));

        // Verify missing fields become nullopt on load
        AnimationProfile restored = AnimationProfile::fromJson(json);
        QVERIFY(!restored.enabled.has_value());
        QVERIFY(!restored.timingMode.has_value());
        QCOMPARE(*restored.duration, 250);
        QCOMPARE(*restored.style, AnimationStyle::Slide);
    }

    void profile_jsonRoundTrip_geometryField()
    {
        AnimationProfile original;
        original.geometryMode = QStringLiteral("morph");

        QJsonObject json = original.toJson();
        QVERIFY(json.contains(QLatin1String("geometry")));
        QCOMPARE(json[QLatin1String("geometry")].toString(), QStringLiteral("morph"));

        AnimationProfile restored = AnimationProfile::fromJson(json);
        QVERIFY(restored.geometryMode.has_value());
        QCOMPARE(*restored.geometryMode, QStringLiteral("morph"));
        QCOMPARE(original, restored);
    }

    void profile_jsonRoundTrip_invalidGeometryRejected()
    {
        QJsonObject json;
        json[QLatin1String("geometry")] = QStringLiteral("invalid_geom");

        AnimationProfile restored = AnimationProfile::fromJson(json);
        QVERIFY(!restored.geometryMode.has_value());
    }

    // =====================================================================
    // AnimationProfile — operator==
    // =====================================================================

    void profile_equality()
    {
        AnimationProfile a;
        AnimationProfile b;
        QVERIFY(a == b); // both empty

        a.duration = 300;
        QVERIFY(a != b);

        b.duration = 300;
        QVERIFY(a == b);

        a.styleParam = 0.5;
        b.styleParam = 0.5;
        QVERIFY(a == b);

        b.styleParam = 0.7;
        QVERIFY(a != b);
    }

    // =====================================================================
    // AnimationProfileTree — static structure queries (4-level hierarchy)
    // =====================================================================

    void tree_allEventNames_count()
    {
        QStringList names = AnimationProfileTree::allEventNames();
        QCOMPARE(names.size(), AnimationTreeEdgeCount + 1); // edges + "global" root
    }

    void tree_allEventNames_containsExpected()
    {
        QStringList names = AnimationProfileTree::allEventNames();
        QVERIFY(names.contains(QStringLiteral("global")));
        // Domain nodes
        QVERIFY(names.contains(QStringLiteral("windowGeometry")));
        QVERIFY(names.contains(QStringLiteral("overlay")));
        // Window geometry
        QVERIFY(names.contains(QStringLiteral("snap")));
        QVERIFY(names.contains(QStringLiteral("snapIn")));
        QVERIFY(names.contains(QStringLiteral("snapOut")));
        QVERIFY(names.contains(QStringLiteral("snapResize")));
        QVERIFY(names.contains(QStringLiteral("layoutSwitch")));
        QVERIFY(names.contains(QStringLiteral("layoutSwitchIn")));
        QVERIFY(names.contains(QStringLiteral("layoutSwitchOut")));
        QVERIFY(names.contains(QStringLiteral("autotileBorder")));
        QVERIFY(names.contains(QStringLiteral("borderIn")));
        QVERIFY(names.contains(QStringLiteral("borderOut")));
        // Overlay
        QVERIFY(names.contains(QStringLiteral("zoneHighlight")));
        QVERIFY(names.contains(QStringLiteral("zoneHighlightIn")));
        QVERIFY(names.contains(QStringLiteral("zoneHighlightOut")));
        QVERIFY(names.contains(QStringLiteral("osd")));
        QVERIFY(names.contains(QStringLiteral("layoutOsdIn")));
        QVERIFY(names.contains(QStringLiteral("layoutOsdOut")));
        QVERIFY(names.contains(QStringLiteral("navigationOsdIn")));
        QVERIFY(names.contains(QStringLiteral("navigationOsdOut")));
        QVERIFY(names.contains(QStringLiteral("popup")));
        QVERIFY(names.contains(QStringLiteral("layoutPickerIn")));
        QVERIFY(names.contains(QStringLiteral("layoutPickerOut")));
        QVERIFY(names.contains(QStringLiteral("snapAssistIn")));
        QVERIFY(names.contains(QStringLiteral("snapAssistOut")));
        QVERIFY(names.contains(QStringLiteral("zoneSelector")));
        QVERIFY(names.contains(QStringLiteral("zoneSelectorIn")));
        QVERIFY(names.contains(QStringLiteral("zoneSelectorOut")));
        QVERIFY(names.contains(QStringLiteral("preview")));
        QVERIFY(names.contains(QStringLiteral("previewIn")));
        QVERIFY(names.contains(QStringLiteral("previewOut")));
        QVERIFY(names.contains(QStringLiteral("dim")));
    }

    void tree_isValidEventName_acceptsKnown()
    {
        QVERIFY(AnimationProfileTree::isValidEventName(QStringLiteral("global")));
        QVERIFY(AnimationProfileTree::isValidEventName(QStringLiteral("windowGeometry")));
        QVERIFY(AnimationProfileTree::isValidEventName(QStringLiteral("overlay")));
        QVERIFY(AnimationProfileTree::isValidEventName(QStringLiteral("snapIn")));
        QVERIFY(AnimationProfileTree::isValidEventName(QStringLiteral("layoutOsdIn")));
        QVERIFY(AnimationProfileTree::isValidEventName(QStringLiteral("preview")));
        QVERIFY(AnimationProfileTree::isValidEventName(QStringLiteral("dim")));
    }

    void tree_isValidEventName_rejectsInvalid()
    {
        QVERIFY(!AnimationProfileTree::isValidEventName(QStringLiteral("garbage")));
        QVERIFY(!AnimationProfileTree::isValidEventName(QStringLiteral("zoneSnapIn"))); // old name
        QVERIFY(!AnimationProfileTree::isValidEventName(QStringLiteral("fade"))); // old name
        QVERIFY(!AnimationProfileTree::isValidEventName(QString()));
    }

    // =====================================================================
    // AnimationProfileTree — parentOf (4-level hierarchy)
    // =====================================================================

    void tree_parentOf_leafToCategory()
    {
        QCOMPARE(AnimationProfileTree::parentOf(QStringLiteral("snapIn")), QStringLiteral("snap"));
        QCOMPARE(AnimationProfileTree::parentOf(QStringLiteral("snapOut")), QStringLiteral("snap"));
        QCOMPARE(AnimationProfileTree::parentOf(QStringLiteral("snapResize")), QStringLiteral("snap"));
        QCOMPARE(AnimationProfileTree::parentOf(QStringLiteral("layoutSwitchIn")), QStringLiteral("layoutSwitch"));
        QCOMPARE(AnimationProfileTree::parentOf(QStringLiteral("borderIn")), QStringLiteral("autotileBorder"));
        QCOMPARE(AnimationProfileTree::parentOf(QStringLiteral("zoneHighlightIn")), QStringLiteral("zoneHighlight"));
        QCOMPARE(AnimationProfileTree::parentOf(QStringLiteral("layoutOsdIn")), QStringLiteral("osd"));
        QCOMPARE(AnimationProfileTree::parentOf(QStringLiteral("layoutPickerIn")), QStringLiteral("popup"));
        QCOMPARE(AnimationProfileTree::parentOf(QStringLiteral("zoneSelectorIn")), QStringLiteral("zoneSelector"));
        QCOMPARE(AnimationProfileTree::parentOf(QStringLiteral("previewIn")), QStringLiteral("preview"));
    }

    void tree_parentOf_categoryToDomain()
    {
        QCOMPARE(AnimationProfileTree::parentOf(QStringLiteral("snap")), QStringLiteral("windowGeometry"));
        QCOMPARE(AnimationProfileTree::parentOf(QStringLiteral("layoutSwitch")), QStringLiteral("windowGeometry"));
        QCOMPARE(AnimationProfileTree::parentOf(QStringLiteral("autotileBorder")), QStringLiteral("windowGeometry"));
        QCOMPARE(AnimationProfileTree::parentOf(QStringLiteral("zoneHighlight")), QStringLiteral("overlay"));
        QCOMPARE(AnimationProfileTree::parentOf(QStringLiteral("osd")), QStringLiteral("overlay"));
        QCOMPARE(AnimationProfileTree::parentOf(QStringLiteral("popup")), QStringLiteral("overlay"));
        QCOMPARE(AnimationProfileTree::parentOf(QStringLiteral("zoneSelector")), QStringLiteral("overlay"));
        QCOMPARE(AnimationProfileTree::parentOf(QStringLiteral("preview")), QStringLiteral("overlay"));
    }

    void tree_parentOf_domainToGlobal()
    {
        QCOMPARE(AnimationProfileTree::parentOf(QStringLiteral("windowGeometry")), QStringLiteral("global"));
        QCOMPARE(AnimationProfileTree::parentOf(QStringLiteral("overlay")), QStringLiteral("global"));
        QCOMPARE(AnimationProfileTree::parentOf(QStringLiteral("dim")), QStringLiteral("global"));
    }

    void tree_parentOf_globalHasNoParent()
    {
        QVERIFY(AnimationProfileTree::parentOf(QStringLiteral("global")).isEmpty());
    }

    // =====================================================================
    // AnimationProfileTree — childrenOf
    // =====================================================================

    void tree_childrenOf_global()
    {
        QStringList children = AnimationProfileTree::childrenOf(QStringLiteral("global"));
        QVERIFY(children.contains(QStringLiteral("windowGeometry")));
        QVERIFY(children.contains(QStringLiteral("overlay")));
        QVERIFY(children.contains(QStringLiteral("dim")));
        QCOMPARE(children.size(), 3);
    }

    void tree_childrenOf_windowGeometry()
    {
        QStringList children = AnimationProfileTree::childrenOf(QStringLiteral("windowGeometry"));
        QVERIFY(children.contains(QStringLiteral("snap")));
        QVERIFY(children.contains(QStringLiteral("layoutSwitch")));
        QVERIFY(children.contains(QStringLiteral("autotileBorder")));
        QCOMPARE(children.size(), 3);
    }

    void tree_childrenOf_overlay()
    {
        QStringList children = AnimationProfileTree::childrenOf(QStringLiteral("overlay"));
        QVERIFY(children.contains(QStringLiteral("zoneHighlight")));
        QVERIFY(children.contains(QStringLiteral("osd")));
        QVERIFY(children.contains(QStringLiteral("popup")));
        QVERIFY(children.contains(QStringLiteral("zoneSelector")));
        QVERIFY(children.contains(QStringLiteral("preview")));
        QCOMPARE(children.size(), 5);
    }

    void tree_childrenOf_snap()
    {
        QStringList children = AnimationProfileTree::childrenOf(QStringLiteral("snap"));
        QVERIFY(children.contains(QStringLiteral("snapIn")));
        QVERIFY(children.contains(QStringLiteral("snapOut")));
        QVERIFY(children.contains(QStringLiteral("snapResize")));
        QCOMPARE(children.size(), 3);
    }

    void tree_childrenOf_osd()
    {
        QStringList children = AnimationProfileTree::childrenOf(QStringLiteral("osd"));
        QVERIFY(children.contains(QStringLiteral("layoutOsdIn")));
        QVERIFY(children.contains(QStringLiteral("layoutOsdOut")));
        QVERIFY(children.contains(QStringLiteral("navigationOsdIn")));
        QVERIFY(children.contains(QStringLiteral("navigationOsdOut")));
        QCOMPARE(children.size(), 4);
    }

    void tree_childrenOf_leafIsEmpty()
    {
        QVERIFY(AnimationProfileTree::childrenOf(QStringLiteral("snapIn")).isEmpty());
        QVERIFY(AnimationProfileTree::childrenOf(QStringLiteral("dim")).isEmpty());
        QVERIFY(AnimationProfileTree::childrenOf(QStringLiteral("layoutOsdIn")).isEmpty());
    }

    // =====================================================================
    // AnimationProfileTree — defaultTree
    // =====================================================================

    void tree_defaultTree_hasGlobalProfile()
    {
        AnimationProfileTree tree = AnimationProfileTree::defaultTree();

        // The global node should have a non-empty raw profile
        AnimationProfile globalRaw = tree.rawProfile(QStringLiteral("global"));
        QVERIFY(!globalRaw.isEmpty());
        QVERIFY(globalRaw.enabled.has_value());
        QCOMPARE(*globalRaw.enabled, true);
        QCOMPARE(*globalRaw.duration, 300);
        QCOMPARE(*globalRaw.timingMode, TimingMode::Easing);
        QCOMPARE(*globalRaw.style, AnimationStyle::Morph);
    }

    void tree_defaultTree_hasDomainProfiles()
    {
        AnimationProfileTree tree = AnimationProfileTree::defaultTree();

        // windowGeometry domain should have Morph style
        AnimationProfile wgRaw = tree.rawProfile(QStringLiteral("windowGeometry"));
        QVERIFY(!wgRaw.isEmpty());
        QCOMPARE(*wgRaw.style, AnimationStyle::Morph);

        // overlay domain should have ScaleIn style and shorter duration
        AnimationProfile ovRaw = tree.rawProfile(QStringLiteral("overlay"));
        QVERIFY(!ovRaw.isEmpty());
        QCOMPARE(*ovRaw.style, AnimationStyle::ScaleIn);
        QCOMPARE(*ovRaw.duration, 150);
    }

    void tree_defaultTree_overlayInheritsCorrectly()
    {
        AnimationProfileTree tree = AnimationProfileTree::defaultTree();

        // A leaf overlay event should inherit overlay defaults
        AnimationProfile resolved = tree.resolvedProfile(QStringLiteral("layoutOsdIn"));
        QVERIFY(resolved.style.has_value());
        QCOMPARE(*resolved.style, AnimationStyle::ScaleIn);
        QCOMPARE(*resolved.duration, 150);
        QCOMPARE(*resolved.enabled, true); // from global
    }

    void tree_defaultTree_windowGeometryInheritsCorrectly()
    {
        AnimationProfileTree tree = AnimationProfileTree::defaultTree();

        // A leaf window event should inherit windowGeometry defaults
        AnimationProfile resolved = tree.resolvedProfile(QStringLiteral("snapIn"));
        QVERIFY(resolved.style.has_value());
        QCOMPARE(*resolved.style, AnimationStyle::Morph);
        QCOMPARE(*resolved.duration, 300); // from global
        QCOMPARE(*resolved.enabled, true);
    }

    // =====================================================================
    // AnimationProfileTree — setProfile / rawProfile / clearProfile
    // =====================================================================

    void tree_setAndGetProfile()
    {
        AnimationProfileTree tree;

        AnimationProfile p;
        p.duration = 200;
        p.style = AnimationStyle::Slide;

        tree.setProfile(QStringLiteral("snapIn"), p);

        AnimationProfile retrieved = tree.rawProfile(QStringLiteral("snapIn"));
        QCOMPARE(*retrieved.duration, 200);
        QCOMPARE(*retrieved.style, AnimationStyle::Slide);
    }

    void tree_clearProfile()
    {
        AnimationProfileTree tree;

        AnimationProfile p;
        p.duration = 200;
        tree.setProfile(QStringLiteral("snapIn"), p);

        tree.clearProfile(QStringLiteral("snapIn"));

        AnimationProfile cleared = tree.rawProfile(QStringLiteral("snapIn"));
        QVERIFY(cleared.isEmpty());
    }

    void tree_setEmptyProfileRemovesIt()
    {
        AnimationProfileTree tree;

        AnimationProfile p;
        p.duration = 200;
        tree.setProfile(QStringLiteral("snapIn"), p);

        // Setting an empty profile should remove the entry
        AnimationProfile empty;
        tree.setProfile(QStringLiteral("snapIn"), empty);

        QVERIFY(tree.rawProfile(QStringLiteral("snapIn")).isEmpty());
    }

    void tree_setProfileRejectsInvalidEvent()
    {
        AnimationProfileTree tree;

        AnimationProfile p;
        p.duration = 200;
        tree.setProfile(QStringLiteral("bogus"), p);

        // Should not store anything for invalid event names
        QVERIFY(tree.rawProfile(QStringLiteral("bogus")).isEmpty());
    }

    // =====================================================================
    // AnimationProfileTree — resolvedProfile inheritance (4-level)
    // =====================================================================

    void tree_resolvedProfile_inheritsFromGlobal()
    {
        AnimationProfileTree tree;

        AnimationProfile global;
        global.duration = 500;
        global.enabled = true;
        tree.setProfile(QStringLiteral("global"), global);

        // snapIn has no override, should inherit through windowGeometry → snap → global
        AnimationProfile resolved = tree.resolvedProfile(QStringLiteral("snapIn"));
        QVERIFY(resolved.duration.has_value());
        QCOMPARE(*resolved.duration, 500);
        QVERIFY(resolved.enabled.has_value());
        QCOMPARE(*resolved.enabled, true);
    }

    void tree_resolvedProfile_domainOverridesGlobal()
    {
        AnimationProfileTree tree;

        AnimationProfile global;
        global.duration = 500;
        tree.setProfile(QStringLiteral("global"), global);

        AnimationProfile wg;
        wg.duration = 300;
        tree.setProfile(QStringLiteral("windowGeometry"), wg);

        // snapIn should get 300 from windowGeometry, not 500 from global
        AnimationProfile resolved = tree.resolvedProfile(QStringLiteral("snapIn"));
        QCOMPARE(*resolved.duration, 300);
    }

    void tree_resolvedProfile_categoryOverridesDomain()
    {
        AnimationProfileTree tree;

        AnimationProfile global;
        global.duration = 500;
        tree.setProfile(QStringLiteral("global"), global);

        AnimationProfile wg;
        wg.duration = 300;
        tree.setProfile(QStringLiteral("windowGeometry"), wg);

        AnimationProfile snap;
        snap.duration = 200;
        tree.setProfile(QStringLiteral("snap"), snap);

        // snapIn inherits from snap, which overrides windowGeometry
        AnimationProfile resolved = tree.resolvedProfile(QStringLiteral("snapIn"));
        QCOMPARE(*resolved.duration, 200);
    }

    void tree_resolvedProfile_leafOverridesCategory()
    {
        AnimationProfileTree tree;

        AnimationProfile global;
        global.duration = 500;
        tree.setProfile(QStringLiteral("global"), global);

        AnimationProfile snap;
        snap.duration = 200;
        tree.setProfile(QStringLiteral("snap"), snap);

        AnimationProfile snapIn;
        snapIn.duration = 100;
        tree.setProfile(QStringLiteral("snapIn"), snapIn);

        AnimationProfile resolved = tree.resolvedProfile(QStringLiteral("snapIn"));
        QCOMPARE(*resolved.duration, 100);
    }

    void tree_resolvedProfile_overlayDomainIsolation()
    {
        AnimationProfileTree tree;

        AnimationProfile global;
        global.duration = 500;
        tree.setProfile(QStringLiteral("global"), global);

        // Set different durations on each domain
        AnimationProfile wg;
        wg.duration = 300;
        tree.setProfile(QStringLiteral("windowGeometry"), wg);

        AnimationProfile ov;
        ov.duration = 150;
        tree.setProfile(QStringLiteral("overlay"), ov);

        // Window events get windowGeometry duration
        QCOMPARE(*tree.resolvedProfile(QStringLiteral("snapIn")).duration, 300);
        QCOMPARE(*tree.resolvedProfile(QStringLiteral("layoutSwitchIn")).duration, 300);

        // Overlay events get overlay duration
        QCOMPARE(*tree.resolvedProfile(QStringLiteral("layoutOsdIn")).duration, 150);
        QCOMPARE(*tree.resolvedProfile(QStringLiteral("layoutPickerIn")).duration, 150);
        QCOMPARE(*tree.resolvedProfile(QStringLiteral("zoneSelectorIn")).duration, 150);

        // Dim gets global (standalone, not under either domain)
        QCOMPARE(*tree.resolvedProfile(QStringLiteral("dim")).duration, 500);
    }

    void tree_resolvedProfile_partialInheritanceAcrossLevels()
    {
        AnimationProfileTree tree;

        // Set timingMode on global
        AnimationProfile global;
        global.timingMode = TimingMode::Spring;
        tree.setProfile(QStringLiteral("global"), global);

        // Set style on windowGeometry domain
        AnimationProfile wg;
        wg.style = AnimationStyle::Popin;
        tree.setProfile(QStringLiteral("windowGeometry"), wg);

        // Set duration on snap category
        AnimationProfile snap;
        snap.duration = 200;
        tree.setProfile(QStringLiteral("snap"), snap);

        // Set enabled on snapIn leaf
        AnimationProfile snapIn;
        snapIn.enabled = false;
        tree.setProfile(QStringLiteral("snapIn"), snapIn);

        // Resolved should have all four from different levels
        AnimationProfile resolved = tree.resolvedProfile(QStringLiteral("snapIn"));
        QCOMPARE(*resolved.timingMode, TimingMode::Spring); // from global
        QCOMPARE(*resolved.style, AnimationStyle::Popin); // from windowGeometry
        QCOMPARE(*resolved.duration, 200); // from snap
        QCOMPARE(*resolved.enabled, false); // from snapIn
    }

    void tree_resolvedProfile_invalidEventReturnsEmpty()
    {
        AnimationProfileTree tree;
        AnimationProfile resolved = tree.resolvedProfile(QStringLiteral("invalid"));
        QVERIFY(resolved.isEmpty());
    }

    // =====================================================================
    // AnimationProfileTree — JSON round-trip
    // =====================================================================

    void tree_jsonRoundTrip_empty()
    {
        AnimationProfileTree original;
        QJsonObject json = original.toJson();
        AnimationProfileTree restored = AnimationProfileTree::fromJson(json);
        QCOMPARE(original, restored);
    }

    void tree_jsonRoundTrip_withOverrides()
    {
        AnimationProfileTree original;

        AnimationProfile global;
        global.enabled = true;
        global.duration = 300;
        global.timingMode = TimingMode::Easing;
        original.setProfile(QStringLiteral("global"), global);

        AnimationProfile snap;
        snap.duration = 200;
        snap.style = AnimationStyle::Slide;
        original.setProfile(QStringLiteral("snap"), snap);

        AnimationProfile osd;
        osd.enabled = false;
        original.setProfile(QStringLiteral("osd"), osd);

        QJsonObject json = original.toJson();
        AnimationProfileTree restored = AnimationProfileTree::fromJson(json);

        QCOMPARE(original, restored);
    }

    void tree_jsonRoundTrip_preservesResolvedProfiles()
    {
        AnimationProfileTree original;

        AnimationProfile global;
        global.duration = 400;
        global.enabled = true;
        original.setProfile(QStringLiteral("global"), global);

        AnimationProfile snap;
        snap.style = AnimationStyle::SlideFade;
        original.setProfile(QStringLiteral("snap"), snap);

        QJsonObject json = original.toJson();
        AnimationProfileTree restored = AnimationProfileTree::fromJson(json);

        // Verify resolved profile for a leaf node is the same after round-trip
        AnimationProfile origResolved = original.resolvedProfile(QStringLiteral("snapIn"));
        AnimationProfile restResolved = restored.resolvedProfile(QStringLiteral("snapIn"));

        QCOMPARE(*origResolved.duration, *restResolved.duration);
        QCOMPARE(*origResolved.enabled, *restResolved.enabled);
        QCOMPARE(*origResolved.style, *restResolved.style);
    }

    void tree_jsonRoundTrip_defaultTree()
    {
        AnimationProfileTree original = AnimationProfileTree::defaultTree();
        QJsonObject json = original.toJson();
        AnimationProfileTree restored = AnimationProfileTree::fromJson(json);
        QCOMPARE(original, restored);
    }

    // =====================================================================
    // AnimationProfileTree — equality
    // =====================================================================

    void tree_equality()
    {
        AnimationProfileTree a;
        AnimationProfileTree b;
        QVERIFY(a == b);

        AnimationProfile p;
        p.duration = 200;
        a.setProfile(QStringLiteral("global"), p);
        QVERIFY(a != b);

        b.setProfile(QStringLiteral("global"), p);
        QVERIFY(a == b);
    }
};

QTEST_MAIN(TestAnimationProfile)

#include "test_animation_profile.moc"
