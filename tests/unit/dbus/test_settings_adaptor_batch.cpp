// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_settings_adaptor_batch.cpp
 * @brief Unit tests for SettingsAdaptor::getSettings — the batched
 *        read used on the editor startup hot path.
 *
 * Regression guard for PR #324: the editor ctor collapses 8 sequential
 * getSetting() round-trips into a single getSettings() call. If
 * SettingsAdaptor::getSettings regresses — drops known keys, propagates
 * unknown keys, returns the wrong type — the editor silently falls back
 * to hardcoded defaults and user config is ignored until the next change.
 */

#include <QTest>
#include <QDBusVariant>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>

#include <PhosphorAnimation/CurveRegistry.h>
#include <PhosphorAnimation/PhosphorProfileRegistry.h>
#include <PhosphorAnimation/Profile.h>
#include <PhosphorAnimation/ProfilePaths.h>
#include <PhosphorAnimation/ProfileTree.h>

#include "dbus/settingsadaptor.h"
#include "../helpers/StubSettings.h"
#include "../helpers/IsolatedConfigGuard.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

namespace {
// Build a Profile carrying only an explicit duration — enough to assert
// the motionProfileTree getter's baseline/override placement and the
// debounced motionProfileTreeChanged emission.
PhosphorAnimation::Profile profileWithDuration(qreal ms)
{
    PhosphorAnimation::Profile p;
    p.duration = ms;
    return p;
}
} // namespace

// Counting-stub: overrides one setter so the guard-fires test can
// distinguish "setter not invoked" (guard hit) from "setter invoked,
// returned true" (guard missed). StubSettings' getters are hard-coded
// (setters are no-ops), so this subclass keeps the getter unchanged
// and only adds a hit counter for setInnerGap.
class CountingStubSettings : public StubSettings
{
public:
    using StubSettings::StubSettings;
    void setInnerGap(int v) override
    {
        ++setInnerGapCalls;
        lastInnerGap = v;
    }
    int setInnerGapCalls = 0;
    int lastInnerGap = -1;
};

class TestSettingsAdaptorBatch : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        m_guard = std::make_unique<IsolatedConfigGuard>();
        m_settings = new CountingStubSettings(nullptr);
        m_parent = new QObject(nullptr);
        m_adaptor = new SettingsAdaptor(m_settings, /*shaderRegistry=*/nullptr, /*profileRegistry=*/nullptr, m_parent);
    }

    void cleanup()
    {
        delete m_parent;
        m_parent = nullptr;
        m_adaptor = nullptr;
        delete m_settings;
        m_settings = nullptr;
        m_guard.reset();
    }

    // ─────────────────────────────────────────────────────────────────────
    // Happy path: every gap/overlay key the editor startup batches for.
    // ─────────────────────────────────────────────────────────────────────
    void testGetSettings_gapOverlayKeys_allReturned()
    {
        const QStringList keys{
            QStringLiteral("innerGap"),      QStringLiteral("outerGap"),           QStringLiteral("usePerSideOuterGap"),
            QStringLiteral("outerGapTop"),   QStringLiteral("outerGapBottom"),     QStringLiteral("outerGapLeft"),
            QStringLiteral("outerGapRight"), QStringLiteral("overlayDisplayMode"),
        };

        const QVariantMap result = m_adaptor->getSettings(keys);

        QCOMPARE(result.size(), keys.size());
        for (const QString& k : keys) {
            QVERIFY2(result.contains(k), qPrintable(QStringLiteral("missing key: %1").arg(k)));
            QVERIFY2(result.value(k).isValid(), qPrintable(QStringLiteral("invalid variant for: %1").arg(k)));
        }
        // Int-typed keys should land on disk as ints, not coerced strings —
        // readInt() on the editor side checks toInt(&ok) so a wrong type
        // would be silently replaced by the default. Pin the type here.
        QCOMPARE(result.value(QStringLiteral("innerGap")).metaType().id(), QMetaType::Int);
        QCOMPARE(result.value(QStringLiteral("outerGap")).metaType().id(), QMetaType::Int);
        QCOMPARE(result.value(QStringLiteral("usePerSideOuterGap")).metaType().id(), QMetaType::Bool);
        QCOMPARE(result.value(QStringLiteral("overlayDisplayMode")).metaType().id(), QMetaType::Int);
    }

    // ─────────────────────────────────────────────────────────────────────
    // Known and unknown keys mixed: known keys returned, unknowns omitted.
    // Matches the contract documented on SettingsAdaptor::getSettings and
    // relied on by querySettingsBatch(): "omit missing, caller uses its
    // own default".
    // ─────────────────────────────────────────────────────────────────────
    void testGetSettings_mixedKnownUnknown_unknownsOmitted()
    {
        const QStringList keys{
            QStringLiteral("innerGap"),
            QStringLiteral("definitelyNotARealSettingKey_xyzzy"),
            QStringLiteral("outerGap"),
        };

        const QVariantMap result = m_adaptor->getSettings(keys);

        QCOMPARE(result.size(), 2);
        QVERIFY(result.contains(QStringLiteral("innerGap")));
        QVERIFY(result.contains(QStringLiteral("outerGap")));
        QVERIFY(!result.contains(QStringLiteral("definitelyNotARealSettingKey_xyzzy")));
    }

    // ─────────────────────────────────────────────────────────────────────
    // Empty key list must return an empty map cleanly, not crash or log
    // every registered key. Editor-side querySettingsBatch short-circuits
    // on empty input, but the daemon-side guard is cheap insurance.
    // ─────────────────────────────────────────────────────────────────────
    void testGetSettings_emptyKeyList_returnsEmptyMap()
    {
        const QVariantMap result = m_adaptor->getSettings(QStringList{});
        QVERIFY(result.isEmpty());
    }

    // ─────────────────────────────────────────────────────────────────────
    // Empty strings inside a non-empty key list are skipped silently, not
    // treated as lookup errors that abort the batch. This guards against
    // accidental `QStringList{ "" }` from stray split/trim code paths on
    // the caller side.
    // ─────────────────────────────────────────────────────────────────────
    void testGetSettings_emptyStringKeysSkipped()
    {
        const QStringList keys{
            QString(), QStringLiteral("innerGap"), QString(), QStringLiteral("outerGap"), QString(),
        };

        const QVariantMap result = m_adaptor->getSettings(keys);

        QCOMPARE(result.size(), 2);
        QVERIFY(result.contains(QStringLiteral("innerGap")));
        QVERIFY(result.contains(QStringLiteral("outerGap")));
    }

    // ─────────────────────────────────────────────────────────────────────
    // All-unknown keys must not fall through to "return the whole map" or
    // error — the contract is "return empty, caller uses defaults".
    // ─────────────────────────────────────────────────────────────────────
    void testGetSettings_allUnknownKeys_returnsEmptyMap()
    {
        const QStringList keys{
            QStringLiteral("bogus_key_one"),
            QStringLiteral("bogus_key_two"),
        };
        const QVariantMap result = m_adaptor->getSettings(keys);
        QVERIFY(result.isEmpty());
    }

    // ─────────────────────────────────────────────────────────────────────
    // Value-equality guard (Phase 1.1 of refactor/dbus-performance):
    // setSetting with a value that already matches the current one must
    // return true WITHOUT invoking the underlying setter. The counter on
    // CountingStubSettings is what actually pins the short-circuit —
    // return-value-only assertions can't distinguish guard-fires from
    // setter-runs-and-returns-true.
    //
    // StubSettings::innerGap() returns 8 — supply 8 and the guard fires.
    // ─────────────────────────────────────────────────────────────────────
    void testSetSetting_unchangedScalar_guardShortCircuits()
    {
        m_settings->setInnerGapCalls = 0;
        const bool ok = m_adaptor->setSetting(QStringLiteral("innerGap"), QDBusVariant(QVariant(8)));
        QVERIFY(ok);
        QCOMPARE(m_settings->setInnerGapCalls, 0);
    }

    // Value-equality guard must NOT intercept changing writes — setter
    // must actually run. Counter proves the call path, not just the
    // return code.
    void testSetSetting_changedScalar_invokesSetter()
    {
        m_settings->setInnerGapCalls = 0;
        const bool ok = m_adaptor->setSetting(QStringLiteral("innerGap"), QDBusVariant(QVariant(42)));
        QVERIFY(ok);
        QCOMPARE(m_settings->setInnerGapCalls, 1);
        QCOMPARE(m_settings->lastInnerGap, 42);
    }

    // Empty-string matches StubSettings::defaultLayoutId() default — guard fires.
    void testSetSetting_unchangedStringScalar_returnsTrue()
    {
        const bool ok = m_adaptor->setSetting(QStringLiteral("defaultLayoutId"), QDBusVariant(QVariant(QString())));
        QVERIFY(ok);
    }

    // Composite (list-of-map) settings like dragActivationTriggers advertise
    // schema "stringlist" but actually store QVariantList of QVariantMap.
    // The guard is gated on the actual variant type (not the schema string),
    // so list-type writes always fall through to the setter — which must
    // still return true via the registered setter lambda.
    void testSetSetting_compositeListType_fallsThroughToSetter()
    {
        QVariantList triggers;
        QVariantMap one;
        one[QStringLiteral("key")] = QStringLiteral("Meta");
        triggers.append(one);
        const bool ok = m_adaptor->setSetting(QStringLiteral("dragActivationTriggers"),
                                              QDBusVariant(QVariant::fromValue(triggers)));
        QVERIFY(ok);
    }

    // ─────────────────────────────────────────────────────────────────────
    // Drag-activation toggle keys. The KWin effect fetches these three bools
    // (via loadSettingAsync → getSetting, the singular form of this batch) to
    // decide whether to keep streaming drag-cursor ticks while no trigger is
    // physically held, so the daemon's rising-edge toggle latches still fire.
    // If the adaptor registry drops any registration the effect silently reads
    // false and that toggle feature dies — zoneSpanToggleMode shipped
    // unregistered until PR #595's audit caught it, so pin the whole set here
    // to break the test gate on any future omission. Mirrors the snap-window
    // batch test above: every requested key must come back, valid, Bool-typed,
    // and wired to its own ISettings accessor.
    // ─────────────────────────────────────────────────────────────────────
    void testGetSettings_dragToggleKeys_allReturnedWithTypes()
    {
        const QStringList keys{
            QStringLiteral("toggleActivation"),
            QStringLiteral("autotileDragInsertToggle"),
            QStringLiteral("zoneSpanToggleMode"),
        };

        const QVariantMap result = m_adaptor->getSettings(keys);

        QCOMPARE(result.size(), keys.size());
        for (const QString& k : keys) {
            QVERIFY2(result.contains(k), qPrintable(QStringLiteral("missing key: %1").arg(k)));
            QVERIFY2(result.value(k).isValid(), qPrintable(QStringLiteral("invalid variant for: %1").arg(k)));
            QCOMPARE(result.value(k).metaType().id(), QMetaType::Bool);
        }

        // Each key resolves to its own ISettings accessor rather than collapsing
        // onto a neighbour.
        QCOMPARE(result.value(QStringLiteral("toggleActivation")).toBool(), m_settings->toggleActivation());
        QCOMPARE(result.value(QStringLiteral("autotileDragInsertToggle")).toBool(),
                 m_settings->autotileDragInsertToggle());
        QCOMPARE(result.value(QStringLiteral("zoneSpanToggleMode")).toBool(), m_settings->zoneSpanToggleMode());
    }

    // ─────────────────────────────────────────────────────────────────────
    // Phase 5: setPerScreenSettings batch surface.
    //
    // The contract mirrors setSettings (global batch): empty map returns
    // true as a no-op, unknown category returns false, recognized category
    // returns true regardless of backend. The per-screen setters are on
    // ISettings with default no-op bodies, so stub backends pass through
    // cleanly — deep correctness belongs to test_settings_perscreen which
    // exercises the concrete Settings type.
    // ─────────────────────────────────────────────────────────────────────
    void testSetPerScreenSettings_emptyMap_returnsTrue()
    {
        const bool ok = m_adaptor->setPerScreenSettings(QStringLiteral("DP-1"), QStringLiteral("autotile"), {});
        QVERIFY(ok);
    }

    void testSetPerScreenSettings_unknownCategory_returnsFalse()
    {
        QVariantMap values;
        values[QStringLiteral("anyKey")] = 1;
        const bool ok = m_adaptor->setPerScreenSettings(QStringLiteral("DP-1"), QStringLiteral("bogus"), values);
        QVERIFY(!ok);
    }

    void testSetPerScreenSettings_recognizedCategory_returnsTrue()
    {
        // Post-DIP fix: the per-screen batch dispatches through ISettings
        // no-op defaults when the backend is a stub, so recognized
        // categories succeed even without a concrete Settings.
        QVariantMap values;
        values[QStringLiteral("masterCount")] = 2;
        const bool ok = m_adaptor->setPerScreenSettings(QStringLiteral("DP-1"), QStringLiteral("autotile"), values);
        QVERIFY(ok);
    }

    // ─────────────────────────────────────────────────────────────────────
    // Per-event motion-profile tree surface (PR #457).
    //
    // The motionProfileTree getter and the motionProfileTreeChanged signal
    // only exist when SettingsAdaptor is constructed with a real
    // PhosphorProfileRegistry. The init() fixture passes nullptr, so the
    // getter must not be registered there at all — consumers fall back to
    // the global animation duration.
    // ─────────────────────────────────────────────────────────────────────
    void testMotionProfileTree_absentWhenNoRegistry()
    {
        QVERIFY(!m_adaptor->getSettingKeys().contains(QStringLiteral("motionProfileTree")));
    }

    // With a registry injected, the getter flattens the merged profile set
    // into a ProfileTree JSON blob: the Global profile becomes the tree
    // baseline, every other path an override. Round-tripped back through
    // ProfileTree so the assertion pins resolved durations, not wire shape.
    void testMotionProfileTree_getterSerializesBaselineAndOverrides()
    {
        PhosphorAnimation::PhosphorProfileRegistry registry;
        registry.registerProfile(PhosphorAnimation::ProfilePaths::Global, profileWithDuration(400.0));
        registry.registerProfile(QStringLiteral("window.open"), profileWithDuration(900.0));

        QObject parent;
        auto* adaptor = new SettingsAdaptor(m_settings, /*shaderRegistry=*/nullptr, &registry, &parent);

        QVERIFY(adaptor->getSettingKeys().contains(QStringLiteral("motionProfileTree")));

        const QString json = adaptor->getSetting(QStringLiteral("motionProfileTree")).variant().toString();
        QVERIFY(!json.isEmpty());

        const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
        QVERIFY(doc.isObject());

        PhosphorAnimation::CurveRegistry curves;
        const PhosphorAnimation::ProfileTree tree = PhosphorAnimation::ProfileTree::fromJson(doc.object(), curves);

        // Global landed as the baseline: a path with no override resolves
        // to the baseline duration.
        QCOMPARE(qRound(tree.resolve(QStringLiteral("window.close")).effectiveDuration()), 400);
        // window.open landed as an override and wins over the baseline.
        QVERIFY(tree.hasOverride(QStringLiteral("window.open")));
        QCOMPARE(qRound(tree.resolve(QStringLiteral("window.open")).effectiveDuration()), 900);
    }

    // A single reloadFromOwner() batch emits one profileChanged per touched
    // path plus a closing ownerReloaded — N+1 registry signals for one
    // logical change. The adaptor's debounce timer must collapse that burst
    // into exactly one motionProfileTreeChanged D-Bus emission.
    void testMotionProfileTreeChanged_coalescesRegistryBurst()
    {
        PhosphorAnimation::PhosphorProfileRegistry registry;
        registry.registerProfile(PhosphorAnimation::ProfilePaths::Global, profileWithDuration(400.0));

        QObject parent;
        auto* adaptor = new SettingsAdaptor(m_settings, /*shaderRegistry=*/nullptr, &registry, &parent);

        QSignalSpy spy(adaptor, &SettingsAdaptor::motionProfileTreeChanged);
        QVERIFY(spy.isValid());

        QHash<QString, PhosphorAnimation::Profile> batch;
        batch.insert(QStringLiteral("window.open"), profileWithDuration(100.0));
        batch.insert(QStringLiteral("window.close"), profileWithDuration(200.0));
        batch.insert(QStringLiteral("window.focus"), profileWithDuration(300.0));
        registry.reloadFromOwner(QStringLiteral("test-owner"), batch);

        // The registry signals fire synchronously; the single-shot debounce
        // timer has only been (re)started, not yet fired.
        QCOMPARE(spy.count(), 0);

        // Spin the event loop until the coalesced emission lands.
        QVERIFY(spy.wait(1000));
        QCOMPARE(spy.count(), 1);
    }

private:
    std::unique_ptr<IsolatedConfigGuard> m_guard;
    CountingStubSettings* m_settings = nullptr;
    // CountingStubSettings publicly inherits StubSettings, whose snapping*
    // getters back the value assertions in testGetSettings_snappingKeys_*.
    QObject* m_parent = nullptr;
    SettingsAdaptor* m_adaptor = nullptr;
};

QTEST_MAIN(TestSettingsAdaptorBatch)
#include "test_settings_adaptor_batch.moc"
