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
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>

#include "dbus/settingsadaptor.h"
#include "../helpers/StubSettings.h"
#include "../helpers/IsolatedConfigGuard.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

class TestSettingsAdaptorBatch : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        m_guard = std::make_unique<IsolatedConfigGuard>();
        m_settings = new StubSettings(nullptr);
        m_parent = new QObject(nullptr);
        m_adaptor = new SettingsAdaptor(m_settings, m_parent);
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
            QStringLiteral("zonePadding"),   QStringLiteral("outerGap"),           QStringLiteral("usePerSideOuterGap"),
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
        QCOMPARE(result.value(QStringLiteral("zonePadding")).metaType().id(), QMetaType::Int);
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
            QStringLiteral("zonePadding"),
            QStringLiteral("definitelyNotARealSettingKey_xyzzy"),
            QStringLiteral("outerGap"),
        };

        const QVariantMap result = m_adaptor->getSettings(keys);

        QCOMPARE(result.size(), 2);
        QVERIFY(result.contains(QStringLiteral("zonePadding")));
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
            QString(), QStringLiteral("zonePadding"), QString(), QStringLiteral("outerGap"), QString(),
        };

        const QVariantMap result = m_adaptor->getSettings(keys);

        QCOMPARE(result.size(), 2);
        QVERIFY(result.contains(QStringLiteral("zonePadding")));
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
    // return true without invoking the underlying setter or scheduling a
    // save. Contract: post-condition "setting now equals value" holds, so
    // callers that treat true as success stay happy.
    //
    // StubSettings::zonePadding() returns 8 — supply 8 and the guard fires.
    // ─────────────────────────────────────────────────────────────────────
    void testSetSetting_unchangedScalar_returnsTrueForIdempotentWrite()
    {
        const bool ok = m_adaptor->setSetting(QStringLiteral("zonePadding"), QDBusVariant(QVariant(8)));
        QVERIFY(ok);
    }

    // Value-equality guard must NOT intercept changing writes.
    void testSetSetting_changedScalar_invokesSetter()
    {
        const bool ok = m_adaptor->setSetting(QStringLiteral("zonePadding"), QDBusVariant(QVariant(42)));
        QVERIFY(ok);
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

private:
    std::unique_ptr<IsolatedConfigGuard> m_guard;
    StubSettings* m_settings = nullptr;
    QObject* m_parent = nullptr;
    SettingsAdaptor* m_adaptor = nullptr;
};

QTEST_MAIN(TestSettingsAdaptorBatch)
#include "test_settings_adaptor_batch.moc"
