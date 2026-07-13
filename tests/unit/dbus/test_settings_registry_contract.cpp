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
 * The list of expected keys is not hand-maintained: the test reads the effect's own
 * source and extracts every literal it passes to loadSettingAsync(). A new setting
 * the effect fetches is therefore checked automatically, with no list to forget.
 */

#include <QTest>
#include <QFile>
#include <QObject>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>

#include <memory>

#include "../helpers/IsolatedConfigGuard.h"
#include "../helpers/StubSettings.h"
#include "dbus/settingsadaptor.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

namespace {

/// Every key the effect passes to loadSettingAsync(QStringLiteral("...")), scraped
/// from its source. Deliberately source-derived rather than duplicated here: a
/// hand-copied list is exactly the thing that drifts.
QStringList keysFetchedByEffect(QString* whyFailed)
{
    const QString path = QStringLiteral(PLASMAZONES_EFFECT_SRC_DIR "/plasmazoneseffect/daemon_bringup.cpp");
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        *whyFailed = QStringLiteral("cannot open %1").arg(path);
        return {};
    }
    const QString src = QString::fromUtf8(f.readAll());

    // loadSettingAsync(QStringLiteral("someKey"), ...)
    // Written with escapes rather than a raw string: a raw string inside the
    // QStringLiteral macro does not survive preprocessing.
    static const QRegularExpression re(QLatin1String("loadSettingAsync\\(\\s*QStringLiteral\\(\"([A-Za-z0-9_]+)\"\\)"));
    QStringList keys;
    auto it = re.globalMatch(src);
    while (it.hasNext()) {
        keys << it.next().captured(1);
    }
    if (keys.isEmpty()) {
        *whyFailed = QStringLiteral("scraped 0 keys from %1 — has the call shape changed?").arg(path);
    }
    return keys;
}

} // namespace

class TestSettingsRegistryContract : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        m_guard = std::make_unique<IsolatedConfigGuard>();
        m_settings = new StubSettings(nullptr);
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

    /**
     * The load-bearing assertion. If this fails, someone added a setting the effect
     * fetches and forgot the REGISTER_*_SETTING line — the feature is dead on the
     * effect side, however complete the rest of the wiring looks.
     */
    void testEveryKeyTheEffectFetchesIsRegistered()
    {
        QString why;
        const QStringList fetched = keysFetchedByEffect(&why);
        QVERIFY2(!fetched.isEmpty(), qPrintable(why));

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
        m_settings->setDecorationPauseWhenIdle(true);
        const QVariant v = m_adaptor->getSetting(QStringLiteral("decorationPauseWhenIdle")).variant();
        QCOMPARE(v.typeId(), QMetaType::Bool);
        QCOMPARE(v.toBool(), true);

        m_settings->setDecorationPauseWhenIdle(false);
        QCOMPARE(m_adaptor->getSetting(QStringLiteral("decorationPauseWhenIdle")).variant().toBool(), false);
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
    StubSettings* m_settings = nullptr;
    QObject* m_parent = nullptr;
    SettingsAdaptor* m_adaptor = nullptr;
};

QTEST_MAIN(TestSettingsRegistryContract)
#include "test_settings_registry_contract.moc"
