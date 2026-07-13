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
#include "config/settings.h"
#include "core/shaderregistry.h"
#include "dbus/settingsadaptor.h"

#include <PhosphorAnimation/PhosphorProfileRegistry.h>

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

namespace {

/// Source with its COMMENTS REMOVED.
///
/// The scrape works on raw text, so a comment that merely mentions `loadSettingAsync(`
/// inflates the call-site tally and fails the test pointing at prose. That is the safe
/// direction, but a tripwire that cries wolf is a tripwire someone eventually loosens —
/// and loosening this one is how it came to be broken four times. Strip the comments and
/// it counts only code. String literals are left alone: a key IS a string literal, so they
/// are the one thing the scrape must still be able to see.
QString withoutComments(const QString& src)
{
    QString out;
    out.reserve(src.size());
    bool inLine = false;
    bool inBlock = false;
    for (qsizetype i = 0; i < src.size(); ++i) {
        const QChar c = src.at(i);
        const QChar next = (i + 1 < src.size()) ? src.at(i + 1) : QChar();
        if (inLine) {
            if (c == QLatin1Char('\n')) {
                inLine = false;
                out.append(c);
            }
            continue;
        }
        if (inBlock) {
            if (c == QLatin1Char('*') && next == QLatin1Char('/')) {
                inBlock = false;
                ++i;
            }
            continue;
        }
        if (c == QLatin1Char('/') && next == QLatin1Char('/')) {
            inLine = true;
            continue;
        }
        if (c == QLatin1Char('/') && next == QLatin1Char('*')) {
            inBlock = true;
            ++i;
            continue;
        }
        out.append(c);
    }
    return out;
}

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
    return withoutComments(content);
}

/// Every setting key the effect fetches, scraped from its own sources rather than
/// duplicated here — a hand-copied list is exactly the thing that drifts.
///
/// Every fetch must NAME its key at the call site, in one of two shapes:
///
///   loadSettingAsync(QStringLiteral("key"), …)              a literal
///   loadAudioInt / loadAudioBool(QStringLiteral("key"), …)  the audio wrappers, which
///                                                           forward to loadSettingAsync
///   loadSettingAsync(SettingProperty::X, …)                 a named constant, resolved
///                                                           from ServiceConstants.h
///
/// There used to be a third shape — bind the constant to a local `kName` first, then
/// pass the alias — and resolving an identifier back to a key is where this scrape went
/// wrong THREE separate times, each time binding a fetch to the wrong key while its own
/// self-check balanced. The aliases are gone from the effect (the constant is named where
/// it is used), and the scrape no longer tries to resolve identifiers at all: a fetch
/// whose key it cannot read off the call site is UNRESOLVED, and unresolved is LOUD.
///
/// Still not covered, and deliberately so rather than silently: a key assembled at
/// runtime, or a fetch hidden behind a macro. Neither exists, and both would have to be
/// written on purpose.
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

    // SettingProperty::Name -> "actualKey", from ServiceConstants.h — scoped to THAT
    // namespace block. The map is keyed on the bare identifier, and the header already has
    // identifiers that repeat across namespaces (ServiceName, ObjectPath, Interface). A
    // header-wide scrape would let a future SettingProperty member that shares a name with
    // any of them bind a fetch to the WRONG key, silently, which is the bug this file has
    // now had written into it four times.
    const qsizetype nsStart = constantsSrc.indexOf(QLatin1String("namespace SettingProperty {"));
    if (nsStart < 0) {
        *whyFailed = QStringLiteral("ServiceConstants.h has no SettingProperty namespace — did it move?");
        return {};
    }
    // The namespace closes on a bare `}` at column 0. It contains no nested braces (it is
    // a flat list of constants), so the first one is the end of it.
    const qsizetype nsEnd = constantsSrc.indexOf(QLatin1String("\n}"), nsStart);
    if (nsEnd < 0) {
        *whyFailed = QStringLiteral("SettingProperty namespace is not closed as expected");
        return {};
    }
    const QString settingPropertySrc = constantsSrc.mid(nsStart, nsEnd - nsStart);

    QHash<QString, QString> constants;
    static const QRegularExpression constRe(
        QLatin1String("inline constexpr QLatin1String ([A-Za-z0-9_]+)\\(\"([A-Za-z0-9_]+)\"\\)"));
    auto cit = constRe.globalMatch(settingPropertySrc);
    while (cit.hasNext()) {
        const auto m = cit.next();
        constants.insert(m.captured(1), m.captured(2));
    }

    // Any call that fetches a setting, in every shape the effect writes. Escapes
    // rather than a raw string: a raw string inside QStringLiteral does not survive
    // preprocessing.
    // ANY load*() that NAMES a key. Deliberately not a list of wrapper names: that list was
    // itself the hand-maintained part (loadAudioInt / loadAudioBool were added to it by
    // hand), and a new wrapper forgotten from it contributed to NEITHER the numerator nor
    // the denominator — the tally balanced and the key went unchecked. Silently. Now a
    // wrapper is recognised by what it does, not by what it is called.
    static const QRegularExpression fetchRe(
        QLatin1String("load[A-Za-z0-9_]*\\(\\s*(?:this,\\s*)?(?:QStringLiteral\\(\"([A-Za-z0-9_]+)\"\\)"
                      "|PhosphorProtocol::Service::SettingProperty::([A-Za-z0-9_]+))"));
    // Direct calls to the PRIMITIVE that name a key. The tally below is anchored on the
    // primitive — every fetch, through however many wrappers, funnels into it — while the
    // scrape above is deliberately broader. Two different jobs: the scrape collects keys,
    // the tally proves no call site was overlooked.
    static const QRegularExpression primitiveRe(
        QLatin1String("loadSettingAsync\\(\\s*(?:this,\\s*)?(?:QStringLiteral\\(\"[A-Za-z0-9_]+\"\\)"
                      "|PhosphorProtocol::Service::SettingProperty::[A-Za-z0-9_]+)"));
    // Call sites that do not name a key: they take it as a parameter.
    static const QRegularExpression nonFetchRe(
        QLatin1String("PlasmaZonesEffect::loadSettingAsync\\(const QString" // the definition
                      "|void loadSettingAsync\\(const QString" // its declaration, in the header
                      "|loadSettingAsync\\(this,\\s*name\\b" // its forwarding call
                      "|loadSettingAsync\\(name\\b")); // inside a wrapper body

    // EVERY effect source, RECURSIVELY, found at test time. A hand-written file list is a
    // list to forget, and so is a hand-written directory list: the effect already has
    // three source directories, and a fetch in a new one would simply never be opened —
    // no tally, no failure, no key checked.
    QStringList files;
    QDirIterator it(QStringLiteral(PLASMAZONES_EFFECT_SRC_DIR), {QStringLiteral("*.cpp"), QStringLiteral("*.h")},
                    QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        files << it.next();
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

        int scraped = 0;
        auto it = fetchRe.globalMatch(src);
        while (it.hasNext()) {
            const auto m = it.next();
            // Two shapes, both of which NAME the key at the call site: a literal, or a
            // SettingProperty constant resolved from ServiceConstants.h. Anything else
            // resolves to nothing and trips the tally below, loudly.
            const QString key = !m.captured(1).isEmpty() ? m.captured(1) : constants.value(m.captured(2));
            if (key.isEmpty()) {
                continue; // unresolvable; the tally below catches it
            }
            keys << key;
            ++scraped;
        }

        // Every DIRECT call to the primitive must be accounted for: either it names a key
        // (and was scraped), or it is one of the known shapes that takes the key as a
        // parameter. A fetch written in some shape this function does not understand shows
        // up here as unaccounted, loudly, instead of vanishing.
        const int total = static_cast<int>(src.count(QLatin1String("loadSettingAsync(")));
        int direct = 0;
        auto pit = primitiveRe.globalMatch(src);
        while (pit.hasNext()) {
            pit.next();
            ++direct;
        }
        int nonFetch = 0;
        auto nit = nonFetchRe.globalMatch(src);
        while (nit.hasNext()) {
            nit.next();
            ++nonFetch;
        }
        if (direct + nonFetch != total) {
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
