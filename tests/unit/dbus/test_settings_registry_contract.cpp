// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_settings_registry_contract.cpp
 * @brief Every setting the KWin effect fetches over D-Bus must be registered in
 *        SettingsAdaptor's getter registry.
 *
 * This is a tripwire for a silent-failure class that has already shipped once.
 *
 * SettingsAdaptor::getSetting resolves keys through m_getters — a HAND-MAINTAINED
 * map populated by the REGISTER_*_SETTING block — not through Qt property
 * reflection. Adding a setting to ISettings / Settings / the schema and wiring the
 * effect to fetch it therefore compiles, links, and runs perfectly while the fetch
 * silently misses.
 *
 * It used to miss quietly: getSetting answered an unknown key with a VALID,
 * empty-string variant, so `QVariant("").toBool()` gave false and the effect wrote
 * that straight into its member — which not only disabled the feature but INVERTED
 * any default-true setting. getSetting now sends a D-Bus error instead, so the
 * effect's callback never runs and the caller keeps its own default. That makes the
 * failure safe, but it does not make it visible. This test does.
 *
 * Nothing here is hand-maintained. The test scans EVERY effect source it finds on disk
 * and extracts every key each one fetches, in each of the shapes the effect writes them
 * (see keysFetchedByEffect below). A new setting, in a new file, is therefore checked
 * automatically, with no list to forget — and a fetch written in a shape the scrape does
 * NOT recognise fails the test loudly rather than vanishing from it, which would have
 * been the same silent hole all over again.
 *
 * That last property is the one to defend. This scrape has twice been written in a way
 * that quietly resolved a fetch to the wrong key while its own self-check still
 * balanced, which is the very failure it is here to prevent. If you change it, make the
 * unrecognised case LOUD.
 */

#include <QTest>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QObject>
#include <QRegularExpression>
#include <QList>
#include <QPair>
#include <QSet>
#include <QString>
#include <QStringList>

#include <memory>

#include "../helpers/IsolatedConfigGuard.h"
#include "../helpers/SettingsFetchScraper.h"
#include "config/settings.h"
#include "core/interfaces/shaderregistry.h"
#include "dbus/settingsadaptor.h"

#include <PhosphorAnimation/PhosphorProfileRegistry.h>

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;
using PlasmaZones::TestHelpers::keysFetchedByEditor;
using PlasmaZones::TestHelpers::keysFetchedByEffect;

class TestSettingsRegistryContract : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    /// The fixture has to be able to SEE every key production registers, or this test
    /// reports a correctly-registered key as missing and invites someone to "fix" it by
    /// deleting the assertion. initializeRegistry() gates its getters behind TWO things,
    /// and both must be satisfied here:
    ///   - a qobject_cast to the concrete Settings (so a StubSettings is not enough),
    ///   - a non-null profile registry (motionProfileTree hangs off this one).
    /// The shader registry is passed for completeness; nothing in initializeRegistry
    /// gates on it. IsolatedConfigGuard keeps the real Settings off the developer's own
    /// config.
    void init()
    {
        m_guard = std::make_unique<IsolatedConfigGuard>();
        m_settings = new Settings(nullptr);
        m_shaderRegistry = new ShaderRegistry(nullptr);
        m_profileRegistry = new PhosphorAnimation::PhosphorProfileRegistry(nullptr);
        m_parent = new QObject(nullptr);
        m_adaptor = new SettingsAdaptor(m_settings, m_shaderRegistry, m_profileRegistry, m_parent);
    }

    void cleanup()
    {
        delete m_parent;
        m_parent = nullptr;
        m_adaptor = nullptr;
        delete m_profileRegistry;
        m_profileRegistry = nullptr;
        delete m_shaderRegistry;
        m_shaderRegistry = nullptr;
        delete m_settings;
        m_settings = nullptr;
        m_guard.reset();
    }

    /**
     * The load-bearing assertion. If this fails, someone added a setting the effect
     * fetches and forgot the REGISTER_*_SETTING line — the feature is dead on the
     * effect side, however complete the rest of the wiring looks.
     */
    void testEveryKeyTheEffectFetchesIsRegistered()
    {
        QString why;
        QStringList unaccounted;
        const QStringList fetched = keysFetchedByEffect(&why, &unaccounted);
        QVERIFY2(!fetched.isEmpty(), qPrintable(why));

        // Every fetch call site must be either a key we scraped or a known non-fetch. A
        // fetch written in a shape this test does not recognise would otherwise be
        // invisible to it, and the test would pass green while the key it fetches went
        // unregistered — the exact failure this file prevents.
        QVERIFY2(unaccounted.isEmpty(),
                 qPrintable(QStringLiteral("Unrecognised setting-fetch call shape: %1. Teach keysFetchedByEffect() "
                                           "the new shape, or the key it fetches goes unchecked.")
                                .arg(unaccounted.join(QStringLiteral("; ")))));

        // One named temporary: iterators taken from two separate getSettingKeys()
        // calls would belong to different lists.
        const QStringList registeredKeys = m_adaptor->getSettingKeys();
        const QSet<QString> registered(registeredKeys.cbegin(), registeredKeys.cend());

        QStringList missing;
        for (const QString& key : fetched) {
            if (!registered.contains(key)) {
                missing << key;
            }
        }

        QVERIFY2(missing.isEmpty(),
                 qPrintable(QStringLiteral("The KWin effect fetches these settings, but SettingsAdaptor does not "
                                           "register them, so getSetting answers with an error and the effect "
                                           "silently keeps its built-in default: %1. Add a REGISTER_*_SETTING line "
                                           "for each in settingsadaptor.cpp.")
                                .arg(missing.join(QStringLiteral(", ")))));
    }

    /// The editor is a SECOND process reading the SAME registry over the same bus, and it
    /// fails the same way: an unregistered key answers with an error and the editor keeps
    /// its built-in default, silently. The effect had a tripwire and the editor did not, so
    /// its two dozen keys were guarded by nothing at all.
    void testEveryKeyTheEditorFetchesIsRegistered()
    {
        QString why;
        QStringList unaccounted;
        const QStringList fetched = keysFetchedByEditor(&why, &unaccounted);
        QVERIFY2(!fetched.isEmpty(), qPrintable(why));

        QVERIFY2(unaccounted.isEmpty(),
                 qPrintable(QStringLiteral("Unrecognised setting-fetch call shape in the editor: %1. Teach "
                                           "keysFetchedByEditor() the new shape, or the key it fetches goes "
                                           "unchecked.")
                                .arg(unaccounted.join(QStringLiteral("; ")))));

        const QStringList registeredKeys = m_adaptor->getSettingKeys();
        const QSet<QString> registered(registeredKeys.cbegin(), registeredKeys.cend());

        QStringList missing;
        for (const QString& key : fetched) {
            if (!registered.contains(key)) {
                missing << key;
            }
        }

        QVERIFY2(missing.isEmpty(),
                 qPrintable(QStringLiteral("The editor fetches these settings, but SettingsAdaptor does not register "
                                           "them, so getSetting answers with an error and the editor silently keeps "
                                           "its built-in default: %1. Add a REGISTER_*_SETTING line for each in "
                                           "settingsadaptor.cpp.")
                                .arg(missing.join(QStringLiteral(", ")))));
    }

    /**
     * The Decorations.Performance keys specifically, spelled out. The scrape above
     * would catch a regression on these too, but naming them pins the exact bug that
     * shipped: all three were absent from the registry, and PauseWhenIdle's default
     * of true was being inverted to false on every startup.
     */
    void testDecorationPerformanceKeysAreRegistered()
    {
        const QStringList keys = m_adaptor->getSettingKeys();
        QVERIFY(keys.contains(QStringLiteral("decorationAnimateFocusedOnly")));
        QVERIFY(keys.contains(QStringLiteral("decorationPauseWhenIdle")));
        QVERIFY(keys.contains(QStringLiteral("decorationIdleTimeoutSec")));
    }

    /**
     * A registered bool must come back as a real bool. The dead-wire bug was invisible
     * precisely because the miss returned a valid empty STRING, which coerces to false
     * without complaint — so type, not just presence, is the thing worth pinning.
     */
    void testRegisteredBoolRoundTripsAsBool()
    {
        // FALSE first. The setting defaults to true on a fresh config, so writing true
        // and reading it back would assert the default rather than the write, and the
        // true leg would pass even if the setter did nothing at all.
        m_settings->setDecorationPauseWhenIdle(false);
        const QVariant off = m_adaptor->getSetting(QStringLiteral("decorationPauseWhenIdle")).variant();
        QCOMPARE(off.typeId(), QMetaType::Bool);
        QCOMPARE(off.toBool(), false);

        m_settings->setDecorationPauseWhenIdle(true);
        const QVariant on = m_adaptor->getSetting(QStringLiteral("decorationPauseWhenIdle")).variant();
        QCOMPARE(on.typeId(), QMetaType::Bool);
        QCOMPARE(on.toBool(), true);
    }

    /**
     * An EMPTY key is a caller bug in exactly the way an unknown one is, and must answer
     * the same way. It used to return a valid empty string, which is the silent shape
     * this whole file exists to stamp out.
     */
    void testEmptyKeyDoesNotReturnAValidEmptyValue()
    {
        const QVariant v = m_adaptor->getSetting(QString()).variant();
        QVERIFY2(!v.isValid(), "An empty key must answer like an unknown one: no valid value.");
    }

    /**
     * An UNKNOWN key must not answer with a valid empty value. Called directly (not
     * over the bus) there is no error reply to send, so the contract is an invalid
     * variant — which QVariant::toBool() still turns into false, but which a caller
     * can at least detect. Over the bus this path sends a real D-Bus error and
     * loadSettingAsync's callback never runs.
     */
    void testUnknownKeyDoesNotReturnAValidEmptyValue()
    {
        const QVariant v = m_adaptor->getSetting(QStringLiteral("thisSettingDoesNotExist")).variant();
        QVERIFY2(!v.isValid(),
                 "An unknown key must not answer with a valid value: that is what made a forgotten "
                 "registration indistinguishable from a legitimately empty setting.");
    }

private:
    std::unique_ptr<IsolatedConfigGuard> m_guard;
    Settings* m_settings = nullptr;
    ShaderRegistry* m_shaderRegistry = nullptr;
    PhosphorAnimation::PhosphorProfileRegistry* m_profileRegistry = nullptr;
    QObject* m_parent = nullptr;
    SettingsAdaptor* m_adaptor = nullptr;
};

QTEST_MAIN(TestSettingsRegistryContract)
#include "test_settings_registry_contract.moc"
