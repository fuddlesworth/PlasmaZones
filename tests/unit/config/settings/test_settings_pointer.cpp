// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_settings_pointer.cpp
 * @brief Settings — Pointer master switch + PointerProfile chain persistence.
 *
 * The pointer chain is its own config domain (group "Pointer", keys Enabled
 * and Chain), not part of the decoration tree. Pinned behaviour:
 *   - a fresh Settings reports the ConfigDefaults values (off, empty chain)
 *   - both keys round-trip through save() and a fresh Settings instance (the
 *     cross-process daemon read)
 *   - sparse persistence: a default-equal value leaves no key on disk, and
 *     resetting a written value removes the key again
 *   - the setters emit exactly once on a real change and stay silent on a
 *     same-value write
 *   - the pointerChainJson facade returns compact, round-trippable JSON;
 *     the empty string resets to the default and malformed JSON is ignored
 */

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>

#include <QTest>

#include <PhosphorPointer/PointerProfile.h>

#include "config/configdefaults.h"
#include "config/settings.h"
#include "helpers/IsolatedConfigGuard.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

namespace {

/// A two-layer chain: an enabled trail with a parameter override, and a
/// disabled halo. Exercises every PointerLayer field.
PhosphorPointerShaders::PointerProfile makeTwoLayerChain()
{
    PhosphorPointerShaders::PointerProfile chain;

    PhosphorPointerShaders::PointerLayer trail;
    trail.effectId = QStringLiteral("phosphor-trail");
    trail.parameters.insert(QStringLiteral("width"), 9);
    chain.layers.append(trail);

    PhosphorPointerShaders::PointerLayer halo;
    halo.effectId = QStringLiteral("halo");
    halo.enabled = false;
    chain.layers.append(halo);

    return chain;
}

/// The "Pointer" group as it sits on disk, or an empty object when the group
/// was never materialized.
QJsonObject storedPointerGroup(const IsolatedConfigGuard& guard)
{
    QFile file(guard.configPath() + QStringLiteral("/plasmazones/config.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QJsonDocument::fromJson(file.readAll()).object().value(QLatin1String("Pointer")).toObject();
}

} // namespace

class TestSettingsPointer : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    /// A fresh config has no Pointer group at all, so both accessors must
    /// fall back to the canonical ConfigDefaults values: the chain is off and
    /// empty. There is no seed layer here, unlike the decoration tree.
    void testPointer_defaultsAreOffAndEmpty()
    {
        IsolatedConfigGuard guard;

        Settings settings;
        QCOMPARE(settings.pointerEnabled(), ConfigDefaults::pointerEnabled());
        QVERIFY2(!settings.pointerEnabled(), "the pointer chain must be opt-in");
        QVERIFY2(settings.pointerChain().isEmpty(), "no pointer pack ships engaged");
        QCOMPARE(settings.pointerChain(), ConfigDefaults::pointerChain());
    }

    /// Both keys must survive save() and reload through a fresh Settings
    /// instance — the cross-process path where the daemon reads the file the
    /// settings app wrote. A missing schema entry would drop the blob at the
    /// Store gate and silently reset the page.
    void testPointer_roundTripsThroughDisk()
    {
        IsolatedConfigGuard guard;

        {
            Settings a;
            a.setPointerEnabled(true);
            a.setPointerChain(makeTwoLayerChain());
            a.save();

            QCOMPARE(a.pointerChain(), makeTwoLayerChain());
        }

        {
            Settings b;
            QVERIFY2(b.pointerEnabled(), "Pointer/Enabled must persist across Settings instances");
            const auto reread = b.pointerChain();
            QCOMPARE(reread.layers.size(), 2);
            QCOMPARE(reread.layers.at(0).effectId, QStringLiteral("phosphor-trail"));
            QVERIFY(reread.layers.at(0).enabled);
            QCOMPARE(reread.layers.at(0).parameters.value(QStringLiteral("width")).toInt(), 9);
            QCOMPARE(reread.layers.at(1).effectId, QStringLiteral("halo"));
            QVERIFY2(!reread.layers.at(1).enabled, "a disabled layer must persist disabled");
        }
    }

    /// Sparse persistence: a value equal to the schema default leaves no key
    /// on disk, and clearing a previously written value removes the key
    /// again rather than materializing the default.
    void testPointer_defaultEqualValuesLeaveNoKey()
    {
        IsolatedConfigGuard guard;

        {
            Settings a;
            a.setPointerEnabled(ConfigDefaults::pointerEnabled());
            a.setPointerChain(ConfigDefaults::pointerChain());
            a.save();
        }
        const QJsonObject untouched = storedPointerGroup(guard);
        QVERIFY2(!untouched.contains(QLatin1String("Enabled")),
                 "a default-equal Enabled write must not materialize the key");
        QVERIFY2(!untouched.contains(QLatin1String("Chain")),
                 "a default-equal Chain write must not materialize the key");

        // Write real values, then put both back to the defaults — the keys
        // must be gone again, not stored as default-valued entries.
        {
            Settings a;
            a.setPointerEnabled(true);
            a.setPointerChain(makeTwoLayerChain());
            a.save();
        }
        QVERIFY(storedPointerGroup(guard).contains(QLatin1String("Chain")));
        {
            Settings a;
            a.setPointerEnabled(false);
            a.setPointerChain(PhosphorPointerShaders::PointerProfile{});
            a.save();
        }
        const QJsonObject cleared = storedPointerGroup(guard);
        QVERIFY2(!cleared.contains(QLatin1String("Enabled")), "clearing Enabled must delete the key");
        QVERIFY2(!cleared.contains(QLatin1String("Chain")), "clearing Chain must delete the key");
    }

    /// The setters' value-equality gates: a real change fires the specific
    /// signal (and settingsChanged) exactly once, and writing the identical
    /// value again fires nothing. Without the gate the QML two-way binding
    /// would re-dirty the page on every refresh.
    void testPointer_settersSignalOnceAndGateEqual()
    {
        IsolatedConfigGuard guard;

        Settings settings;
        QSignalSpy enabledSpy(&settings, &Settings::pointerEnabledChanged);
        QSignalSpy chainSpy(&settings, &Settings::pointerChainChanged);
        QSignalSpy generalSpy(&settings, &Settings::settingsChanged);
        QVERIFY(enabledSpy.isValid());
        QVERIFY(chainSpy.isValid());
        QVERIFY(generalSpy.isValid());

        settings.setPointerEnabled(true);
        QCOMPARE(enabledSpy.count(), 1);
        settings.setPointerEnabled(true);
        QCOMPARE(enabledSpy.count(), 1);

        const auto chain = makeTwoLayerChain();
        settings.setPointerChain(chain);
        QCOMPARE(chainSpy.count(), 1);
        settings.setPointerChain(chain);
        QCOMPARE(chainSpy.count(), 1);

        QVERIFY(generalSpy.count() >= 2);
    }

    /// Layers with an empty effectId are dropped at the persistence boundary
    /// (PointerProfile::fromJson is the canonical filter and the setter round
    /// trips through it), so a malformed write from scripting or a test can
    /// never stamp an unresolvable layer onto disk.
    void testPointerChain_emptyEffectIdLayersAreDropped()
    {
        IsolatedConfigGuard guard;

        Settings settings;
        PhosphorPointerShaders::PointerProfile chain = makeTwoLayerChain();
        PhosphorPointerShaders::PointerLayer nameless;
        chain.layers.append(nameless);
        settings.setPointerChain(chain);

        QCOMPARE(settings.pointerChain().layers.size(), 2);
    }

    /// pointerChainJson returns a compact serialization that round-trips back
    /// through PointerProfile::fromJson to an equal chain — this is the blob
    /// the QML two-way binding and the D-Bus adaptor carry.
    void testPointerChainJson_returnsCompactRoundTrippableJson()
    {
        IsolatedConfigGuard guard;

        Settings settings;
        const auto chain = makeTwoLayerChain();
        settings.setPointerChain(chain);

        const QString json = settings.pointerChainJson();
        QVERIFY(!json.isEmpty());
        QVERIFY2(!json.contains(QLatin1Char('\n')), "pointerChainJson must be compact");

        const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
        QVERIFY(doc.isObject());
        QCOMPARE(PhosphorPointerShaders::PointerProfile::fromJson(doc.object()), chain);

        // The facade also writes: a JSON string round-trips back into the
        // typed accessor.
        Settings other;
        other.setPointerChainJson(json);
        QCOMPARE(other.pointerChain(), chain);
    }

    /// setPointerChainJson("") resets to the canonical default (the empty
    /// chain), exactly like the decoration tree facade.
    void testPointerChainJson_emptyStringResetsToDefault()
    {
        IsolatedConfigGuard guard;

        Settings settings;
        settings.setPointerChain(makeTwoLayerChain());
        QVERIFY(!settings.pointerChain().isEmpty());

        settings.setPointerChainJson(QString());
        QCOMPARE(settings.pointerChain(), ConfigDefaults::pointerChain());
        QVERIFY(settings.pointerChain().isEmpty());
    }

    /// Malformed JSON is ignored: the setter neither mutates the chain nor
    /// fires the changed signal, so a bad two-way-binding write cannot
    /// clobber the user's pointer packs.
    void testPointerChainJson_malformedIsIgnored()
    {
        IsolatedConfigGuard guard;

        Settings settings;
        const auto chain = makeTwoLayerChain();
        settings.setPointerChain(chain);

        QSignalSpy spy(&settings, &Settings::pointerChainChanged);
        settings.setPointerChainJson(QStringLiteral("{ this is not valid json"));
        QCOMPARE(spy.count(), 0);
        QCOMPARE(settings.pointerChain(), chain);
    }
};

QTEST_MAIN(TestSettingsPointer)
#include "test_settings_pointer.moc"
