// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_settings_fullscreen_suppression.cpp
 * @brief Round trip for Decorations.Performance/SuppressWhileFullscreen.
 *
 * The setting draws no decoration on an output while a window on it is
 * fullscreen (windows, the shell surfaces and the pointer chain alike). Its
 * per-output scoping and the effect-side gate live in the KWin effect and are
 * not reachable from a unit test; what IS reachable, and what silently breaks
 * a setting in this tree, is the persistence chain underneath it. So this pins
 * that chain end to end:
 *
 *  1. Absent key yields the default. The key is default-TRUE, which puts it in
 *     the same inversion risk class as its group-mates AnimateFocusedOnly and
 *     PauseWhenIdle: a reader that treats the absent key as a value rather
 *     than falling back reads an empty QVariant as false, and the setting
 *     ships INVERTED rather than merely disabled. That is not hypothetical in
 *     this group, it is what happened once.
 *  2. Setter round trip, change-gated signal, and no signal on a same-value
 *     write.
 *  3. Save and reload through the JSON backend, in BOTH directions, so a
 *     persisted false survives and is not re-defaulted to true on load.
 *  4. Independence from its group-mates. All three are bools in one config
 *     group, so a copy-pasted key accessor in the storescalars macros compiles
 *     and passes any test that moves them together.
 *  5. reset() restores the default.
 *  6. The schema entry exists, carries the accessor's default, and is a Bool.
 */

#include <QSignalSpy>
#include <QTest>

#include "config/configdefaults.h"
#include "config/settings.h"
#include "config/settingsschema.h"
#include "helpers/IsolatedConfigGuard.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

class TestSettingsFullscreenSuppression : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void absentKeyYieldsDefault()
    {
        IsolatedConfigGuard guard;

        Settings settings;
        QVERIFY2(settings.decorationSuppressWhileFullscreen(),
                 "SuppressWhileFullscreen defaults to TRUE. A false here means the absent key is being read as a "
                 "value instead of falling back to the default, which does not disable the setting, it inverts it.");
        // The literal pin above cannot catch a regression that flips the
        // ConfigDefaults accessor itself; the symbolic compare below cannot
        // catch a reader that ignores the accessor. Both are needed.
        QCOMPARE(settings.decorationSuppressWhileFullscreen(), ConfigDefaults::decorationSuppressWhileFullscreen());
    }

    void setterRoundTripEmitsOnlyOnChange()
    {
        IsolatedConfigGuard guard;

        Settings settings;
        QSignalSpy spy(&settings, &Settings::decorationSuppressWhileFullscreenChanged);
        QVERIFY(spy.isValid());

        // Writing the value it already holds must not emit.
        settings.setDecorationSuppressWhileFullscreen(ConfigDefaults::decorationSuppressWhileFullscreen());
        QCOMPARE(spy.count(), 0);

        settings.setDecorationSuppressWhileFullscreen(false);
        QCOMPARE(settings.decorationSuppressWhileFullscreen(), false);
        QCOMPARE(spy.count(), 1);
        settings.setDecorationSuppressWhileFullscreen(false);
        QCOMPARE(spy.count(), 1);

        settings.setDecorationSuppressWhileFullscreen(true);
        QCOMPARE(settings.decorationSuppressWhileFullscreen(), true);
        QCOMPARE(spy.count(), 2);
    }

    /**
     * Persisted in both directions.
     *
     * The false leg is the load-bearing one, since the default is true and a
     * load that dropped the stored value entirely would still read back true.
     * The true leg then proves the false leg was not simply a write that never
     * landed: this config store is sparse (a default-equal value deletes the
     * key), so a reload after writing true legitimately finds no key at all
     * and must still answer true.
     */
    void persistsAcrossReload()
    {
        IsolatedConfigGuard guard;

        {
            Settings settings;
            settings.setDecorationSuppressWhileFullscreen(false);
            QVERIFY(settings.save());
        }
        {
            Settings reloaded;
            QCOMPARE(reloaded.decorationSuppressWhileFullscreen(), false);
            reloaded.setDecorationSuppressWhileFullscreen(true);
            QVERIFY(reloaded.save());
        }
        Settings reloadedAgain;
        QCOMPARE(reloadedAgain.decorationSuppressWhileFullscreen(), true);
    }

    /**
     * The three Decorations.Performance bools are independent slots.
     *
     * They share a group and a type and all three default to true, so a key
     * accessor pasted from the wrong sibling into the storescalars macros
     * compiles and round-trips perfectly as long as a test moves them
     * together. Holding each at a DIFFERENT value across a save and reload is
     * what turns that into a failure.
     */
    void doesNotShareStorageWithItsGroupMates()
    {
        IsolatedConfigGuard guard;

        {
            Settings settings;
            settings.setDecorationSuppressWhileFullscreen(false);
            QCOMPARE(settings.decorationSuppressWhileFullscreen(), false);
            QCOMPARE(settings.decorationAnimateFocusedOnly(), true);
            QCOMPARE(settings.decorationPauseWhenIdle(), true);

            settings.setDecorationAnimateFocusedOnly(false);
            QCOMPARE(settings.decorationSuppressWhileFullscreen(), false);
            QCOMPARE(settings.decorationPauseWhenIdle(), true);

            // Flipped back on its own: a shared slot would drag one of the two
            // false siblings back to true with it.
            settings.setDecorationSuppressWhileFullscreen(true);
            QCOMPARE(settings.decorationSuppressWhileFullscreen(), true);
            QCOMPARE(settings.decorationAnimateFocusedOnly(), false);
            QCOMPARE(settings.decorationPauseWhenIdle(), true);
            QVERIFY(settings.save());
        }
        Settings reloaded;
        QCOMPARE(reloaded.decorationSuppressWhileFullscreen(), true);
        QCOMPARE(reloaded.decorationAnimateFocusedOnly(), false);
        QCOMPARE(reloaded.decorationPauseWhenIdle(), true);
    }

    void resetRestoresDefault()
    {
        IsolatedConfigGuard guard;

        Settings settings;
        settings.setDecorationSuppressWhileFullscreen(false);
        QCOMPARE(settings.decorationSuppressWhileFullscreen(), false);

        settings.reset();
        QCOMPARE(settings.decorationSuppressWhileFullscreen(), ConfigDefaults::decorationSuppressWhileFullscreen());
    }

    /**
     * The schema carries the key, seeded from its own accessor.
     *
     * A store-backed getter resolves through the schema, so a missing or
     * mistyped entry here is the difference between the setting working and
     * the getter answering an invalid QVariant (false, for a bool) forever.
     */
    void schemaEntryIsPresentAndSeededFromTheAccessor()
    {
        const PhosphorConfig::Schema schema = PlasmaZones::buildSettingsSchema();
        const auto group = schema.groups.constFind(ConfigDefaults::decorationsPerformanceGroup());
        QVERIFY2(group != schema.groups.constEnd(), "no Decorations.Performance group in the schema");

        const PhosphorConfig::KeyDef* def = nullptr;
        for (const PhosphorConfig::KeyDef& candidate : *group) {
            if (candidate.key == ConfigDefaults::suppressWhileFullscreenKey()) {
                def = &candidate;
                break;
            }
        }
        QVERIFY2(def, "no SuppressWhileFullscreen entry in Decorations.Performance");
        QCOMPARE(def->expectedType, QMetaType::Bool);
        QCOMPARE(def->defaultValue.toBool(), ConfigDefaults::decorationSuppressWhileFullscreen());
    }
};

QTEST_MAIN(TestSettingsFullscreenSuppression)
#include "test_settings_fullscreen_suppression.moc"
