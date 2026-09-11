// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_settings_overlay_shader_tree.cpp
 * @brief Settings — OverlayShaderTree persistence (the overlay analogue of
 *        test_settings_shader_tree).
 *
 * Pinned behaviour:
 *   - setOverlayShaderTree round-trips through the JSON blob (the
 *     Overlays schema group must declare the key or
 *     PhosphorConfig::Store::write drops the blob silently)
 *   - a fresh Settings instance on the same config reads the value back
 *     (the daemon-reads-what-the-settings-app-wrote path)
 *   - a same-tree write is a no-op: no spurious overlayShaderTreeChanged
 *   - the JSON facade parses and routes through the typed setter
 */

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>
#include <QUuid>

#include <PhosphorShaders/ShaderPreset.h>
#include <PhosphorShaders/ShaderPresetRegistry.h>

#include "config/configdefaults.h"
#include "config/configmigration.h"
#include "config/settings.h"
#include "core/types/overlayshadertree.h"
#include "helpers/IsolatedConfigGuard.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

namespace {
const QString kLayoutId = QStringLiteral("{aaaa0000-0000-0000-0000-000000000000}");
}

class TestSettingsOverlayShaderTree : public QObject
{
    Q_OBJECT

private:
    /// Write @p tree straight into config.json under the overlays group,
    /// bypassing Settings so the schema validator is exercised on READ against
    /// bytes the typed setter would never have produced. Group names are
    /// dot-paths that JsonBackend stores as NESTED objects, so the nesting is
    /// built rather than the dotted name inserted flat.
    [[nodiscard]] static bool writeRawTree(const QJsonObject& tree)
    {
        QJsonObject group{{ConfigDefaults::overlayShaderTreeKey(), tree}};
        const QStringList segments = ConfigDefaults::overlaysGroup().split(QLatin1Char('.'));
        for (auto it = segments.crbegin(); it != segments.crend(); ++it)
            group = QJsonObject{{*it, group}};
        QJsonObject root = group;
        root.insert(ConfigKeys::versionKey(), ConfigSchemaVersion);
        QFile f(ConfigDefaults::configFilePath());
        if (!QDir().mkpath(QFileInfo(f).absolutePath()) || !f.open(QIODevice::WriteOnly))
            return false;
        const QByteArray bytes = QJsonDocument(root).toJson();
        return f.write(bytes) == static_cast<qint64>(bytes.size());
    }

private Q_SLOTS:
    /// The two counting bounds in the sanitizer. Neither has a fixture
    /// otherwise, so raising either to INT_MAX leaves the suite green.
    ///
    /// Both are asserted as "fewer than offered, and more than none": pinning
    /// the exact cap here would make the test a copy of the constant rather
    /// than a check that a cap runs at all, and a guard that emptied the key
    /// would pass a bare upper bound.
    void testOverlayShaderTree_parameterAndOverrideCountsAreBounded()
    {
        IsolatedConfigGuard guard;

        QJsonObject params;
        for (int i = 0; i < 300; ++i)
            params.insert(QStringLiteral("p%1").arg(i), i);
        QJsonObject overrides;
        for (int i = 0; i < 2000; ++i) {
            overrides.insert(
                QUuid::createUuid().toString(),
                QJsonObject{{QLatin1String(OverlayShaderProfile::JsonFieldShaderId), QStringLiteral("neon-city")}});
        }
        QVERIFY(writeRawTree(QJsonObject{
            {QLatin1String(OverlayShaderTree::JsonFieldBaseline),
             QJsonObject{{QLatin1String(OverlayShaderProfile::JsonFieldShaderId), QStringLiteral("cosmic-flow")},
                         {QLatin1String(OverlayShaderProfile::JsonFieldParameters), params}}},
            {QLatin1String(OverlayShaderTree::JsonFieldOverrides), overrides}}));

        Settings s;
        const OverlayShaderTree read = s.overlayShaderTree();
        const int keptParams = read.baseline().parameters.size();
        const int keptOverrides = read.overriddenLayouts().size();
        QVERIFY2(keptParams > 0 && keptParams < 300,
                 qPrintable(QStringLiteral("parameter cap did not run: kept %1 of 300").arg(keptParams)));
        QVERIFY2(keptOverrides > 0 && keptOverrides < 2000,
                 qPrintable(QStringLiteral("override cap did not run: kept %1 of 2000").arg(keptOverrides)));
        // The bound truncates; it does not corrupt what survives.
        QCOMPARE(read.baseline().shaderId, QStringLiteral("cosmic-flow"));
    }

    /// The UUID arm of the sanitizer in BOTH directions: a key that parses but
    /// is spelled without braces is NORMALIZED (not merely kept, and not
    /// refused), and a key that does not parse at all is refused. The existing
    /// sanitize slot covers only the refusal.
    void testOverlayShaderTree_unbracedOverrideKeyIsNormalizedOnRead()
    {
        IsolatedConfigGuard guard;
        const QString unbraced = QStringLiteral("ffff0000-0000-0000-0000-000000000000");
        const QJsonObject node{{QLatin1String(OverlayShaderProfile::JsonFieldShaderId), QStringLiteral("aurora")}};
        QVERIFY(writeRawTree(
            QJsonObject{{QLatin1String(OverlayShaderTree::JsonFieldOverrides), QJsonObject{{unbraced, node}}}}));

        Settings s;
        const OverlayShaderTree read = s.overlayShaderTree();
        QVERIFY2(read.hasOverride(QUuid::fromString(unbraced).toString()),
                 "an unbraced override key was not normalized to the braced form every reader asks with");
        QVERIFY2(!read.hasOverride(unbraced), "the unbraced spelling survived alongside the braced one");
    }

    /// OverlayShaderTree::fromJson's `!it.value().isObject()` guard on the
    /// override map. A non-object node must be skipped, not admitted as a
    /// default-constructed profile that would then shadow the baseline for
    /// that layout. Nothing else in the suite feeds fromJson a non-object
    /// node, so dropping the guard stays green.
    void testOverlayShaderTree_nonObjectOverrideNodeIsSkippedNotShadowing()
    {
        OverlayShaderTree tree = OverlayShaderTree::fromJson(QJsonObject{
            {QLatin1String(OverlayShaderTree::JsonFieldBaseline),
             QJsonObject{{QLatin1String(OverlayShaderProfile::JsonFieldShaderId), QStringLiteral("cosmic-flow")}}},
            {QLatin1String(OverlayShaderTree::JsonFieldOverrides),
             QJsonObject{{kLayoutId, QJsonValue(QStringLiteral("not-an-object"))}}}});

        QVERIFY2(!tree.hasOverride(kLayoutId), "a non-object override node became an override");
        QCOMPARE(tree.resolve(kLayoutId).shaderId, QStringLiteral("cosmic-flow"));
    }

    void testOverlayShaderTree_setRoundTripsThroughDisk()
    {
        IsolatedConfigGuard guard;
        {
            Settings a;
            OverlayShaderTree tree;
            tree.setBaseline({QStringLiteral("cosmic-flow"), {{QStringLiteral("speed"), 1.5}}});
            tree.setOverride(kLayoutId, {QStringLiteral("neon-city"), {}});
            QSignalSpy spy(&a, &Settings::overlayShaderTreeChanged);
            a.setOverlayShaderTree(tree);
            QCOMPARE(spy.count(), 1);

            const OverlayShaderTree reread = a.overlayShaderTree();
            QCOMPARE(reread.baseline().shaderId, QStringLiteral("cosmic-flow"));
            QCOMPARE(reread.resolve(kLayoutId).shaderId, QStringLiteral("neon-city"));

            // Writing the identical tree back must not fire the signal.
            a.setOverlayShaderTree(reread);
            QCOMPARE(spy.count(), 1);
        }
        {
            // Fresh instance, same isolated config: the cross-process path.
            Settings b;
            const OverlayShaderTree reread = b.overlayShaderTree();
            QVERIFY(reread.hasOverride(kLayoutId));
            QCOMPARE(reread.baseline().parameters.value(QStringLiteral("speed")).toDouble(), 1.5);
        }
    }

    void testOverlayShaderTree_jsonFacadeRoutesThroughTypedSetter()
    {
        IsolatedConfigGuard guard;
        Settings s;
        OverlayShaderTree tree;
        tree.setOverride(kLayoutId, {QStringLiteral("neon-city"), {}});
        const QString json = QString::fromUtf8(QJsonDocument(tree.toJson()).toJson(QJsonDocument::Compact));

        QSignalSpy spy(&s, &Settings::overlayShaderTreeChanged);
        s.setOverlayShaderTreeJson(json);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(s.overlayShaderTree().resolve(kLayoutId).shaderId, QStringLiteral("neon-city"));

        // Malformed JSON is ignored, not treated as a clear.
        s.setOverlayShaderTreeJson(QStringLiteral("not json"));
        QCOMPARE(spy.count(), 1);
        QVERIFY(s.overlayShaderTree().hasOverride(kLayoutId));

        // Empty string resets to the empty tree.
        s.setOverlayShaderTreeJson(QString());
        QCOMPARE(spy.count(), 2);
        QVERIFY(s.overlayShaderTree().isEmpty());
    }

    void testOverlayShaderTree_parameterOnlyChangeFiresAndPersists()
    {
        // Pins that the no-op gate compares PARAMETERS too: a same-shader
        // write with a changed parameter map must fire the signal and land
        // on disk (a future operator== that ignored parameters would leave
        // this suite green while parameter edits stop saving).
        IsolatedConfigGuard guard;
        Settings s;
        OverlayShaderTree tree;
        tree.setOverride(kLayoutId, {QStringLiteral("neon-city"), {{QStringLiteral("speed"), 1.0}}});
        s.setOverlayShaderTree(tree);

        QSignalSpy spy(&s, &Settings::overlayShaderTreeChanged);
        tree.setOverride(kLayoutId, {QStringLiteral("neon-city"), {{QStringLiteral("speed"), 2.0}}});
        s.setOverlayShaderTree(tree);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(s.overlayShaderTree().resolve(kLayoutId).parameters.value(QStringLiteral("speed")).toDouble(), 2.0);
    }

    /// The schema validator, driven through the door the typed setter does not
    /// cover: a config.json written by something other than Settings. A
    /// settings profile being applied reaches the same key the same way
    /// (Store::importFromJson writes each declared key's blob value straight
    /// through, and write() runs the validator), and the file is hand-editable
    /// besides.
    ///
    /// Every assertion here is on something OverlayShaderTree::fromJson would
    /// otherwise keep. The typed round trip already drops unknown FIELDS, so
    /// asserting on those would pass with the validator removed and pin
    /// nothing — these three axes are what actually distinguishes the guard
    /// from its absence.
    void testOverlayShaderTree_rawConfigIsSanitizedOnRead()
    {
        IsolatedConfigGuard guard;

        const QString overLong = QString(2000, QLatin1Char('x'));
        QJsonObject legit{
            {QLatin1String(OverlayShaderProfile::JsonFieldShaderId), QStringLiteral("neon-city")},
            {QLatin1String(OverlayShaderProfile::JsonFieldParameters), QJsonObject{{QStringLiteral("speed"), 2.0}}}};
        QJsonObject overrides{
            {kLayoutId, legit},
            // Unresolvable key shape: the pre-v8 editor could stamp these, and
            // they surface on the assignments page as nameless broken rows.
            {QStringLiteral("autotile:bsp"), legit},
        };
        QJsonObject baseline{
            {QLatin1String(OverlayShaderProfile::JsonFieldShaderId), overLong},
            {QLatin1String(OverlayShaderProfile::JsonFieldParameters),
             QJsonObject{{QStringLiteral("tex"), overLong},
                         {QStringLiteral("nested"), QJsonObject{{QStringLiteral("a"), 1}}},
                         {QStringLiteral("speed"), 0.5}}},
        };
        const QJsonObject tree{{QLatin1String(OverlayShaderTree::JsonFieldBaseline), baseline},
                               {QLatin1String(OverlayShaderTree::JsonFieldOverrides), overrides}};

        QVERIFY(writeRawTree(tree));

        Settings s;
        const OverlayShaderTree read = s.overlayShaderTree();

        // The legitimate override is untouched — a guard that empties the key
        // would satisfy every drop assertion below and be useless.
        QVERIFY(read.hasOverride(kLayoutId));
        QCOMPARE(read.directOverride(kLayoutId).shaderId, QStringLiteral("neon-city"));
        QCOMPARE(read.directOverride(kLayoutId).parameters.value(QStringLiteral("speed")).toDouble(), 2.0);

        QVERIFY2(!read.hasOverride(QStringLiteral("autotile:bsp")), "non-UUID override key survived");
        QVERIFY2(read.baseline().shaderId.isEmpty(), "over-long shaderId survived");
        QVERIFY2(!read.baseline().parameters.contains(QStringLiteral("tex")), "over-long parameter value survived");
        QVERIFY2(!read.baseline().parameters.contains(QStringLiteral("nested")), "nested parameter survived");
        // The sound parameter beside the dropped ones is kept.
        QCOMPARE(read.baseline().parameters.value(QStringLiteral("speed")).toDouble(), 0.5);

        // Idempotent: the validator's own output must survive a second pass,
        // which is the contract KeyDef::validator states.
        //
        // Re-writing `read` through the setter would prove nothing — it is
        // what the getter just returned, so the setter's value-equality gate
        // returns before writing and the comparison is a value against
        // itself. Feed the RAW fixture back to disk instead and re-read with a
        // fresh Settings, so the validator genuinely runs a second time over
        // its own first output.
        QVERIFY(writeRawTree(tree));
        Settings second;
        QCOMPARE(second.overlayShaderTree(), read);
    }

    // ─────── Preset reference ───────

    void testPresetIdRoundTrips()
    {
        OverlayShaderProfile p;
        p.shaderId = QStringLiteral("cosmic-flow");
        p.presetId = QStringLiteral("{teal}");
        p.parameters = QVariantMap{{QStringLiteral("speed"), 2.0}};

        const OverlayShaderProfile back = OverlayShaderProfile::fromJson(p.toJson());
        QCOMPARE(back, p);
        QCOMPARE(back.presetId, QStringLiteral("{teal}"));
    }

    void testProfileCarryingOnlyAPresetIsNotEmpty()
    {
        // isEmpty() gates persistence, so a node carrying nothing but a preset
        // reference still has to be written — otherwise picking a preset
        // without touching a parameter would not survive a restart.
        OverlayShaderProfile p;
        p.presetId = QStringLiteral("{teal}");
        QVERIFY(!p.isEmpty());
    }

    void testPresetIdParticipatesInEquality()
    {
        OverlayShaderProfile a;
        a.shaderId = QStringLiteral("cosmic-flow");
        OverlayShaderProfile b = a;
        QCOMPARE(a, b);
        b.presetId = QStringLiteral("{teal}");
        QVERIFY(a != b);
    }

    void testPresetIdSurvivesTheSchemaSanitizer()
    {
        // The sanitizer rebuilds the profile field by field at the persistence
        // boundary, so a field it forgets is silently dropped on every save.
        // This is the regression guard for exactly that.
        IsolatedConfigGuard guard;
        Settings a;
        OverlayShaderTree tree;
        OverlayShaderProfile node;
        node.shaderId = QStringLiteral("cosmic-flow");
        node.presetId = QStringLiteral("{teal}");
        tree.setBaseline(node);
        a.setOverlayShaderTree(tree);

        QCOMPARE(a.overlayShaderTree().baseline().presetId, QStringLiteral("{teal}"));
    }

    void testOverLongPresetIdIsDroppedButShaderSurvives()
    {
        // Each field is bounded independently: an over-long preset id falls
        // back to the assignment's own parameters, which is the same thing a
        // preset id naming no preset already resolves to.
        IsolatedConfigGuard guard;
        Settings a;
        OverlayShaderTree tree;
        OverlayShaderProfile node;
        node.shaderId = QStringLiteral("cosmic-flow");
        node.presetId = QString(4096, QLatin1Char('x'));
        tree.setBaseline(node);
        a.setOverlayShaderTree(tree);

        QCOMPARE(a.overlayShaderTree().baseline().shaderId, QStringLiteral("cosmic-flow"));
        QVERIFY(a.overlayShaderTree().baseline().presetId.isEmpty());
    }

    /// The flatten, which now lives beside this profile type rather than being
    /// open-coded inside OverlayService at two call sites.
    ///
    /// The overlay twin of the animation and surface `withPresetsResolved`, and
    /// the same three properties are what matter: the assignment's own values win
    /// over the preset's, the reference is CLEARED so a second flatten is a no-op,
    /// and a reference that resolves to nothing leaves the stored values alone.
    void testWithPresetsResolvedFlattensOnceAndDegradesCleanly()
    {
        PhosphorShaders::ShaderPresetRegistry registry;
        PhosphorShaders::ShaderPreset teal;
        teal.id = QStringLiteral("{teal}");
        teal.name = QStringLiteral("Teal");
        teal.packId = QStringLiteral("cosmic-flow");
        teal.params = QVariantMap{{QStringLiteral("speed"), 0.5}, {QStringLiteral("glow"), 0.2}};
        registry.setUserPresets(PhosphorShaders::ShaderFamily::Overlay, {teal});

        OverlayShaderProfile node;
        node.shaderId = QStringLiteral("cosmic-flow");
        node.presetId = QStringLiteral("{teal}");
        node.parameters = QVariantMap{{QStringLiteral("glow"), 0.9}};

        const OverlayShaderProfile flat = withPresetsResolved(node, registry);
        // Untouched parameter follows the preset; the edited one stays edited.
        QCOMPARE(flat.parameters.value(QStringLiteral("speed")).toDouble(), 0.5);
        QCOMPARE(flat.parameters.value(QStringLiteral("glow")).toDouble(), 0.9);
        // Cleared, which is what says "already applied" — and makes a second pass
        // a no-op rather than a double application.
        QVERIFY(flat.presetId.isEmpty());
        QCOMPARE(withPresetsResolved(flat, registry), flat);

        // An assignment can outlive the preset it points at, and then renders the
        // way it did before it pointed at one.
        OverlayShaderProfile orphan = node;
        orphan.presetId = QStringLiteral("{gone}");
        const OverlayShaderProfile degraded = withPresetsResolved(orphan, registry);
        QCOMPARE(degraded.parameters.value(QStringLiteral("glow")).toDouble(), 0.9);
        QVERIFY(!degraded.parameters.contains(QStringLiteral("speed")));

        // A profile naming no preset comes back untouched, reference included.
        OverlayShaderProfile plain;
        plain.shaderId = QStringLiteral("cosmic-flow");
        plain.parameters = QVariantMap{{QStringLiteral("speed"), 2.0}};
        QCOMPARE(withPresetsResolved(plain, registry), plain);
    }
};

QTEST_MAIN(TestSettingsOverlayShaderTree)
#include "test_settings_overlay_shader_tree.moc"
