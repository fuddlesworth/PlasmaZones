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
#include "config/settings.h"
#include "core/shaderregistry.h"
#include "dbus/settingsadaptor.h"

#include <PhosphorAnimation/PhosphorProfileRegistry.h>

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

namespace {

QString readSource(const QString& path, QString* whyFailed)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        *whyFailed = QStringLiteral("cannot open %1").arg(path);
        return {};
    }
    const QString content = QString::fromUtf8(f.readAll());
    if (content.isEmpty()) {
        // An empty-but-readable file returns the same {} as an unopenable one, so it
        // has to explain itself too or the caller's failure message comes out blank.
        *whyFailed = QStringLiteral("%1 is empty").arg(path);
    }
    return content;
}

/// Every setting key the effect fetches, scraped from its own sources rather than
/// duplicated here — a hand-copied list is exactly the thing that drifts.
///
/// The effect fetches in FOUR shapes, and all of them must be understood or the key
/// silently escapes this test:
///
///   loadSettingAsync(QStringLiteral("key"), …)                        inline
///   loadAudioInt / loadAudioBool(QStringLiteral("key"), …)            audio wrappers,
///                                                                     which forward to
///                                                                     loadSettingAsync
///   loadSettingAsync(SettingProperty::X, …)                           named constant
///   constexpr QLatin1String kName = SettingProperty::X;               local alias,
///   loadSettingAsync(this, kName, …)                                    resolved by NAME
///                                                                       and POSITION
///
/// The constants are resolved by scraping ServiceConstants.h, so the named forms are
/// checked as strictly as the literal ones.
///
/// @p unaccounted receives any fetch call site that matches none of the above and is
/// not a known non-fetch (the loadSettingAsync definition, its forwarding call, and
/// the audio wrappers' own bodies, all of which take the key as a PARAMETER). The
/// caller asserts it is empty: a fetch written in a shape this function does not know
/// would otherwise simply not be seen, and the test would pass green on precisely the
/// bug it exists to catch. It has already caught itself doing that once.
QStringList keysFetchedByEffect(QString* whyFailed, QStringList* unaccounted)
{
    const QString constantsSrc =
        readSource(QStringLiteral(PLASMAZONES_PROTOCOL_INC_DIR "/PhosphorProtocol/ServiceConstants.h"), whyFailed);
    if (constantsSrc.isEmpty()) {
        return {};
    }

    // SettingProperty::Name -> "actualKey", from ServiceConstants.h.
    QHash<QString, QString> constants;
    static const QRegularExpression constRe(
        QLatin1String("inline constexpr QLatin1String ([A-Za-z0-9_]+)\\(\"([A-Za-z0-9_]+)\"\\)"));
    auto cit = constRe.globalMatch(constantsSrc);
    while (cit.hasNext()) {
        const auto m = cit.next();
        constants.insert(m.captured(1), m.captured(2));
    }

    // Any call that fetches a setting, in every shape the effect writes. Escapes
    // rather than a raw string: a raw string inside QStringLiteral does not survive
    // preprocessing.
    static const QRegularExpression fetchRe(QLatin1String(
        "load(?:SettingAsync|AudioInt|AudioBool)\\(\\s*(?:this,\\s*)?(?:QStringLiteral\\(\"([A-Za-z0-9_]+)\"\\)"
        "|PhosphorProtocol::Service::SettingProperty::([A-Za-z0-9_]+)"
        "|([A-Za-z][A-Za-z0-9_]*)\\s*,)"));
    // A local alias: constexpr QLatin1String kName = SettingProperty::X;
    static const QRegularExpression aliasRe(
        QLatin1String("QLatin1String ([A-Za-z0-9_]+) = PhosphorProtocol::Service::SettingProperty::([A-Za-z0-9_]+)"));
    // Call sites that do not name a key: they take it as a parameter.
    static const QRegularExpression nonFetchRe(
        QLatin1String("PlasmaZonesEffect::loadSettingAsync\\(const QString" // the definition
                      "|loadSettingAsync\\(this,\\s*name\\b" // its forwarding call
                      "|loadSettingAsync\\(name\\b")); // inside the audio wrappers

    // EVERY effect source, found at test time. A hand-written file list is a list to
    // forget: a new .cpp that fetches a setting would simply not be looked at, and the
    // test would keep passing while the key went unchecked — which is the whole failure
    // this file exists to make impossible. The two directories are where the effect's
    // translation units live.
    QStringList files;
    for (const QString& dir : {QStringLiteral(PLASMAZONES_EFFECT_SRC_DIR),
                               QStringLiteral(PLASMAZONES_EFFECT_SRC_DIR "/plasmazoneseffect")}) {
        const QFileInfoList entries = QDir(dir).entryInfoList({QStringLiteral("*.cpp")}, QDir::Files);
        for (const QFileInfo& entry : entries) {
            files << entry.absoluteFilePath();
        }
    }
    if (files.isEmpty()) {
        *whyFailed = QStringLiteral("found no effect sources under %1").arg(QLatin1String(PLASMAZONES_EFFECT_SRC_DIR));
        return {};
    }

    QStringList keys;
    for (const QString& path : files) {
        const QString src = readSource(path, whyFailed);
        if (src.isEmpty()) {
            return {};
        }

        // Local aliases, resolved by NAME **and** POSITION. Both halves are load-bearing,
        // and each was got wrong once:
        //
        //   By name alone: every alias in shader_transitions.cpp is called `kName`, each
        //   in its own function scope, naming a DIFFERENT setting. A name-keyed map
        //   collapses all three into whichever was written last, so two real keys stop
        //   being checked — silently, because the tally still balances.
        //
        //   By position alone: an identifier that is NOT one of the aliases at all (a
        //   member constant, a differently-spelled local, a new fetch appended after the
        //   last alias in the file) binds to whichever alias happens to precede it. The
        //   key checked is then not the key fetched — again silently, again with a
        //   balanced tally.
        //
        // So: bind an identifier to the nearest alias DEFINED BEFORE IT that has THAT
        // NAME. Anything else resolves to nothing and trips the tally below, loudly.
        // This test exists to catch exactly the failure it kept committing itself.
        struct Alias
        {
            qsizetype definedAt;
            QString name;
            QString key;
        };
        QList<Alias> aliases;
        auto ait = aliasRe.globalMatch(src);
        while (ait.hasNext()) {
            const auto m = ait.next();
            aliases.append({m.capturedStart(0), m.captured(1), constants.value(m.captured(2))});
        }
        const auto aliasAt = [&aliases](const QString& name, qsizetype offset) -> QString {
            QString found;
            for (const Alias& alias : aliases) {
                if (alias.definedAt < offset && alias.name == name) {
                    found = alias.key;
                }
            }
            return found;
        };

        int scraped = 0;
        auto it = fetchRe.globalMatch(src);
        while (it.hasNext()) {
            const auto m = it.next();
            QString key;
            if (!m.captured(1).isEmpty()) {
                key = m.captured(1); // QStringLiteral("…")
            } else if (!m.captured(2).isEmpty()) {
                key = constants.value(m.captured(2)); // SettingProperty::X
            } else {
                key = aliasAt(m.captured(3), m.capturedStart(0)); // a local alias, by name
            }
            if (key.isEmpty()) {
                continue; // an identifier we could not resolve; the tally below catches it
            }
            keys << key;
            ++scraped;
        }

        const int total = static_cast<int>(src.count(QLatin1String("loadSettingAsync(")))
            + static_cast<int>(src.count(QLatin1String("loadAudioInt(")))
            + static_cast<int>(src.count(QLatin1String("loadAudioBool(")));
        int nonFetch = 0;
        auto nit = nonFetchRe.globalMatch(src);
        while (nit.hasNext()) {
            nit.next();
            ++nonFetch;
        }
        if (scraped + nonFetch != total) {
            *unaccounted << QStringLiteral(
                                "%1: %2 fetch call sites, %3 scraped, %4 known non-fetches — %5 "
                                "unaccounted for")
                                .arg(QFileInfo(path).fileName())
                                .arg(total)
                                .arg(scraped)
                                .arg(nonFetch)
                                .arg(total - scraped - nonFetch);
        }
    }

    if (keys.isEmpty()) {
        *whyFailed = QStringLiteral("scraped 0 keys — has the call shape changed?");
    }
    return keys;
}

} // namespace

class TestSettingsRegistryContract : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    /// The fixture has to be able to SEE every key production registers, or this test
    /// reports a correctly-registered key as missing and invites someone to "fix" it by
    /// deleting the assertion. initializeRegistry() gates its getters behind THREE
    /// things, and all three must be satisfied here:
    ///   - a qobject_cast to the concrete Settings (so a StubSettings is not enough),
    ///   - a non-null shader registry,
    ///   - a non-null profile registry (motionProfileTree hangs off this one).
    /// IsolatedConfigGuard keeps the real Settings off the developer's own config.
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
