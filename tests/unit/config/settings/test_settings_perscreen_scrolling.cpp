// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_settings_perscreen_scrolling.cpp
 * @brief The settings-store half of the per-monitor scrolling override channel.
 *
 * The New-columns card lets a monitor override the strip's sizing defaults, and
 * those overrides live in their own store rather than in flat config keys. The
 * store's guards are all this layer has: the daemon's merge is a plain copy, so
 * anything that gets past validation here reaches the engine verbatim.
 *
 * Two failure modes are pinned in particular.
 *
 * The first is per-key validation being the only guard. Every key is bounded
 * independently, so a {Kind, Value} pair that is individually legal and jointly
 * impossible — Fixed holding a proportion, Proportion holding a pixel count —
 * survives validation on both the write path and the load path. Left unrepaired
 * it opens every column on that monitor one pixel wide (or at 100% of the work
 * area) for the whole session. Both repair sites are covered: the re-seed the
 * setter performs on a kind write, and the sweep the load path performs over
 * every screen's map.
 *
 * The second is the closed-set validators' numeric check. Every scrolling enum's
 * legal set contains 0, so a bare toInt() on a non-numeric payload would yield a
 * legal-looking 0 override — a D-Bus or QML caller passing a string would
 * silently pin the monitor to the first enumerator instead of being rejected.
 *
 * Virtual sub-screens are deliberately NOT resolved here, unlike the
 * zone-selector and gap channels: the daemon resolves the virtual→physical
 * fallback in its own scrolling path, and doing it here too would resolve it
 * twice. That absence is asserted rather than assumed.
 */

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSignalSpy>
#include <QTest>
#include <QVariantMap>

#include "config/configdefaults.h"
#include "config/settings.h"
#include "core/interfaces/settings_interfaces.h"
#include "helpers/IsolatedConfigGuard.h"

#include <PhosphorIdentity/VirtualScreenId.h>

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

namespace {

namespace K = PerScreenScrollingKey;

QString key(const char* name)
{
    return QString::fromLatin1(name);
}

/// Rewrite @p leafKey inside the object the per-screen path resolver created
/// for @p screen. The search anchors on the SCREEN ID rather than on a
/// hardcoded path, because the nesting is the resolver's business and not what
/// these tests are about — but it must not anchor on the leaf key alone: the
/// global Scrolling group spells its keys identically, so a bare "find this
/// key" walk would happily patch the app-wide value instead and the repair
/// assertions would pass without the per-screen sweep existing at all.
bool patchScreenValue(QJsonObject& object, const QString& screen, const QString& leafKey, const QJsonValue& value)
{
    for (const QString& child : object.keys()) {
        if (!object.value(child).isObject()) {
            continue;
        }
        QJsonObject nested = object.value(child).toObject();
        const bool patched = (child == screen && nested.contains(leafKey))
            ? (nested[leafKey] = value, true)
            : patchScreenValue(nested, screen, leafKey, value);
        if (patched) {
            object[child] = nested;
            return true;
        }
    }
    return false;
}

} // namespace

class TestSettingsPerScreenScrolling : public QObject
{
    Q_OBJECT

private:
    static QString configPathFor(const IsolatedConfigGuard& guard)
    {
        return guard.configPath() + QStringLiteral("/plasmazones/config.json");
    }

private Q_SLOTS:
    /// Every key's validator, both directions: a legal value is stored as
    /// given, an illegal one is rejected outright (no override materializes),
    /// and an out-of-range numeric value on a BOUNDED key is clamped rather
    /// than rejected. Enum keys reject instead of clamping, matching the global
    /// schema's validIntOr convention — clamping an enum silently reinterprets
    /// it as the nearest enumerator.
    void validatorAcceptsLegalAndRejectsIllegal()
    {
        IsolatedConfigGuard guard;
        Settings settings;
        const QString screen = QStringLiteral("DP-validate");

        struct Row
        {
            const char* name;
            QString key;
            QVariant write;
            bool accepted;
            QVariant expected; // only read when accepted
        };
        const Row rows[] = {
            // Closed-set enums: legal values pass, one past the set is refused.
            {"width kind proportion", key(K::DefaultColumnWidthKind), ConfigDefaults::scrollingWidthKindProportion(),
             true, ConfigDefaults::scrollingWidthKindProportion()},
            {"width kind preset", key(K::DefaultColumnWidthKind), ConfigDefaults::scrollingWidthKindPreset(), true,
             ConfigDefaults::scrollingWidthKindPreset()},
            {"width kind past the set",
             key(K::DefaultColumnWidthKind),
             ConfigDefaults::scrollingWidthKindPreset() + 1,
             false,
             {}},
            {"height kind fixed", key(K::DefaultWindowHeightKind), ConfigDefaults::scrollingHeightKindFixed(), true,
             ConfigDefaults::scrollingHeightKindFixed()},
            {"height kind past the set",
             key(K::DefaultWindowHeightKind),
             ConfigDefaults::scrollingHeightKindPreset() + 1,
             false,
             {}},
            {"display tabbed", key(K::DefaultColumnDisplay), ConfigDefaults::scrollingColumnDisplayTabbed(), true,
             ConfigDefaults::scrollingColumnDisplayTabbed()},
            {"display past the set",
             key(K::DefaultColumnDisplay),
             ConfigDefaults::scrollingColumnDisplayTabbed() + 1,
             false,
             {}},
            {"strip axis horizontal", key(K::StripAxis), ConfigDefaults::scrollingStripAxisHorizontal(), true,
             ConfigDefaults::scrollingStripAxisHorizontal()},
            {"strip axis vertical", key(K::StripAxis), ConfigDefaults::scrollingStripAxisVertical(), true,
             ConfigDefaults::scrollingStripAxisVertical()},
            {"strip axis past the set", key(K::StripAxis), ConfigDefaults::scrollingStripAxisVertical() + 1, false, {}},
            // Non-numeric payloads on the closed-set keys. Every one of those
            // sets contains 0, so without the numeric check a string would be
            // accepted AS the first enumerator instead of refused.
            {"width kind non-numeric", key(K::DefaultColumnWidthKind), QStringLiteral("proportion"), false, {}},
            {"height kind non-numeric", key(K::DefaultWindowHeightKind), QStringLiteral("fixed"), false, {}},
            {"display non-numeric", key(K::DefaultColumnDisplay), QStringLiteral("tabbed"), false, {}},
            // The axis set's zero is Auto, so a string payload would pin the
            // monitor to "follow the screen shape" instead of being refused —
            // the same silent-first-enumerator trap as the three above.
            {"strip axis non-numeric", key(K::StripAxis), QStringLiteral("vertical"), false, {}},
            // Bounded numerics clamp instead of rejecting: a slider drag wants
            // the nearest legal value.
            {"width value in range", key(K::DefaultColumnWidthValue), 0.35, true, 0.35},
            {"width value over the union ceiling", key(K::DefaultColumnWidthValue), 99999.0, true,
             ConfigDefaults::scrollingDefaultColumnWidthFixedMax()},
            {"width value under the union floor", key(K::DefaultColumnWidthValue), 0.001, true,
             ConfigDefaults::scrollingDefaultColumnWidthProportionMin()},
            {"height value over the ceiling", key(K::DefaultWindowHeightValue), 99999.0, true,
             ConfigDefaults::scrollingDefaultWindowHeightMax()},
            {"height value under the floor", key(K::DefaultWindowHeightValue), 1.0, true,
             ConfigDefaults::scrollingDefaultWindowHeightMin()},
            {"width preset index over the ceiling", key(K::DefaultColumnWidthPresetIndex), 99, true,
             ConfigDefaults::scrollingPresetIndexMax()},
            {"width preset index under the floor", key(K::DefaultColumnWidthPresetIndex), -1, true, 0},
            {"height preset index over the ceiling", key(K::DefaultWindowHeightPresetIndex), 99, true,
             ConfigDefaults::scrollingPresetIndexMax()},
            {"height preset index under the floor", key(K::DefaultWindowHeightPresetIndex), -1, true, 0},
            // An unknown key has no validator arm and must not be stored: the
            // ladder returns an invalid QVariant for anything it does not name.
            {"unknown key", QStringLiteral("DefaultColumnWidthWhatever"), 1, false, {}},
        };

        // Failures accumulate rather than aborting in-loop, so one bad row
        // cannot hide every row after it.
        QStringList failures;
        for (const Row& row : rows) {
            settings.clearPerScreenScrollingSettings(screen);
            settings.setPerScreenScrollingSetting(screen, row.key, row.write);
            const QVariantMap stored = settings.getPerScreenScrollingSettings(screen);
            if (!row.accepted) {
                if (stored.contains(row.key)) {
                    failures.append(QStringLiteral("%1: accepted, expected reject (stored %2)")
                                        .arg(QLatin1String(row.name), stored.value(row.key).toString()));
                }
                continue;
            }
            if (!stored.contains(row.key)) {
                failures.append(QStringLiteral("%1: rejected, expected accept").arg(QLatin1String(row.name)));
                continue;
            }
            if (stored.value(row.key) != row.expected) {
                failures.append(
                    QStringLiteral("%1: stored %2, expected %3")
                        .arg(QLatin1String(row.name), stored.value(row.key).toString(), row.expected.toString()));
            }
        }
        QVERIFY2(failures.isEmpty(), qPrintable(failures.join(QLatin1String("; "))));
    }

    /// A rejected write is a full no-op: it must not create an empty override
    /// entry for the screen. A husk entry would light the card's override dot
    /// and offer a Clear action for a monitor that overrides nothing.
    void rejectedWriteLeavesNoHusk()
    {
        IsolatedConfigGuard guard;
        Settings settings;
        const QString screen = QStringLiteral("DP-husk");

        QSignalSpy changedSpy(&settings, &Settings::perScreenScrollingSettingsChanged);
        settings.setPerScreenScrollingSetting(screen, key(K::DefaultColumnWidthKind), QStringLiteral("nonsense"));
        QVERIFY(!settings.hasPerScreenScrollingSettings(screen));
        QCOMPARE(changedSpy.count(), 0);

        // An empty screen id or key is refused before validation even runs.
        settings.setPerScreenScrollingSetting(QString(), key(K::DefaultColumnWidthKind),
                                              ConfigDefaults::scrollingWidthKindFixed());
        settings.setPerScreenScrollingSetting(screen, QString(), ConfigDefaults::scrollingWidthKindFixed());
        QVERIFY(!settings.hasPerScreenScrollingSettings(screen));
        QCOMPARE(changedSpy.count(), 0);
    }

    /// A kind write re-seeds a value the new kind cannot hold, the per-monitor
    /// twin of the global kind setter's coercion. Both directions, plus the
    /// case where the value CAN belong to the new kind and must survive: the
    /// re-seed is a preservation test, not a kind sniff.
    void kindWriteReseedsStrandedValue()
    {
        IsolatedConfigGuard guard;
        Settings settings;
        const QString screen = QStringLiteral("DP-reseed");
        const QString kindKey = key(K::DefaultColumnWidthKind);
        const QString valueKey = key(K::DefaultColumnWidthValue);

        // Proportion holding 0.35, flipped to Fixed: 0.35 is not a pixel width.
        settings.setPerScreenScrollingSetting(screen, kindKey, ConfigDefaults::scrollingWidthKindProportion());
        settings.setPerScreenScrollingSetting(screen, valueKey, 0.35);
        settings.setPerScreenScrollingSetting(screen, kindKey, ConfigDefaults::scrollingWidthKindFixed());
        QCOMPARE(settings.getPerScreenScrollingSettings(screen).value(valueKey).toDouble(),
                 ConfigDefaults::scrollingDefaultColumnWidthFixedPx());

        // Fixed holding 1200px, flipped to Proportion: pixels are not a
        // fraction. The seed is deliberately not the 800 re-seed value, so an
        // unconditional re-seed to the default cannot pass this.
        settings.clearPerScreenScrollingSettings(screen);
        settings.setPerScreenScrollingSetting(screen, kindKey, ConfigDefaults::scrollingWidthKindFixed());
        settings.setPerScreenScrollingSetting(screen, valueKey, 1200.0);
        settings.setPerScreenScrollingSetting(screen, kindKey, ConfigDefaults::scrollingWidthKindProportion());
        QCOMPARE(settings.getPerScreenScrollingSettings(screen).value(valueKey).toDouble(),
                 ConfigDefaults::scrollingDefaultColumnWidthValue());

        // A pixel width parked through a Preset hop comes back intact: Preset
        // owns no width of its own, so nothing about the round trip may
        // disturb the stored one.
        settings.clearPerScreenScrollingSettings(screen);
        settings.setPerScreenScrollingSetting(screen, kindKey, ConfigDefaults::scrollingWidthKindFixed());
        settings.setPerScreenScrollingSetting(screen, valueKey, 1200.0);
        settings.setPerScreenScrollingSetting(screen, kindKey, ConfigDefaults::scrollingWidthKindPreset());
        QCOMPARE(settings.getPerScreenScrollingSettings(screen).value(valueKey).toDouble(), 1200.0);
        settings.setPerScreenScrollingSetting(screen, kindKey, ConfigDefaults::scrollingWidthKindFixed());
        QCOMPARE(settings.getPerScreenScrollingSettings(screen).value(valueKey).toDouble(), 1200.0);
    }

    /// Every accepted override survives a save and a reload unchanged. The
    /// round trip covers all eight keys at once, because a key missing from
    /// either the save list or the load ladder is invisible until it is the one
    /// the user set. StripAxis is written as Vertical rather than the default
    /// Auto on purpose: a default-equal write is dropped on save, so an Auto
    /// row would round-trip through an absent key and prove nothing.
    void overridesRoundTripThroughDisk()
    {
        IsolatedConfigGuard guard;
        const QString screen = QStringLiteral("DP-roundtrip");
        const QVariantMap expected{
            {key(K::DefaultColumnWidthKind), ConfigDefaults::scrollingWidthKindFixed()},
            {key(K::DefaultColumnWidthValue), 1200.0},
            {key(K::DefaultColumnWidthPresetIndex), 3},
            {key(K::DefaultColumnDisplay), ConfigDefaults::scrollingColumnDisplayTabbed()},
            {key(K::DefaultWindowHeightKind), ConfigDefaults::scrollingHeightKindFixed()},
            {key(K::DefaultWindowHeightValue), 720.0},
            {key(K::DefaultWindowHeightPresetIndex), 2},
            {key(K::StripAxis), ConfigDefaults::scrollingStripAxisVertical()},
        };
        {
            Settings settings;
            // The kind goes first so the value write is validated under it.
            settings.setPerScreenScrollingSetting(screen, key(K::DefaultColumnWidthKind),
                                                  expected.value(key(K::DefaultColumnWidthKind)));
            for (auto it = expected.constBegin(); it != expected.constEnd(); ++it) {
                settings.setPerScreenScrollingSetting(screen, it.key(), it.value());
            }
            settings.save();
        }

        Settings reloaded;
        const QVariantMap stored = reloaded.getPerScreenScrollingSettings(screen);
        QCOMPARE(stored.size(), expected.size());
        QStringList mismatches;
        for (auto it = expected.constBegin(); it != expected.constEnd(); ++it) {
            if (stored.value(it.key()).toDouble() != it.value().toDouble()) {
                mismatches.append(it.key() + QLatin1String(": ") + stored.value(it.key()).toString()
                                  + QLatin1String(" != ") + it.value().toString());
            }
        }
        mismatches.sort();
        QVERIFY2(mismatches.isEmpty(), qPrintable(mismatches.join(QLatin1String("; "))));
    }

    /// The load path repairs a jointly-impossible pair that never passed the
    /// setter — a hand edit, a config import, or a staged profile blob. Per-key
    /// validation cannot catch it, because each half is in range on its own.
    void loadRepairsJointlyImpossiblePair()
    {
        IsolatedConfigGuard guard;
        const QString screen = QStringLiteral("DP-repair");
        {
            Settings settings;
            settings.setPerScreenScrollingSetting(screen, key(K::DefaultColumnWidthKind),
                                                  ConfigDefaults::scrollingWidthKindFixed());
            settings.setPerScreenScrollingSetting(screen, key(K::DefaultColumnWidthValue), 1200.0);
            settings.save();
        }

        // Hand-edit the stored pixel width into a proportion while the stored
        // kind stays Fixed. Both halves remain in the union range, so the
        // per-key validators pass it straight through on reload.
        const QString path = configPathFor(guard);
        QFile file(path);
        QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(path));
        QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
        file.close();
        QVERIFY(patchScreenValue(root, screen, key(K::DefaultColumnWidthValue), QJsonValue(0.5)));
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(QJsonDocument(root).toJson());
        file.close();

        Settings reloaded;
        const QVariantMap stored = reloaded.getPerScreenScrollingSettings(screen);
        QCOMPARE(stored.value(key(K::DefaultColumnWidthKind)).toInt(), ConfigDefaults::scrollingWidthKindFixed());
        QCOMPARE(stored.value(key(K::DefaultColumnWidthValue)).toDouble(),
                 ConfigDefaults::scrollingDefaultColumnWidthFixedPx());
    }

    /// A screen that overrides the VALUE alone is left UNTOUCHED by the load
    /// repair: the engine gates the whole per-screen width channel on the
    /// per-screen Kind key's presence (effectiveDefaultColumnWidth returns
    /// the global width wholesale without it), so the lone value never
    /// applies — and "repairing" it against the global kind would destroy
    /// the retained figure the engine would use the moment the user re-adds
    /// a per-screen kind.
    void loadRetainsValueOnlyOverrideUntouched()
    {
        IsolatedConfigGuard guard;
        const QString screen = QStringLiteral("DP-globalkind");
        {
            Settings settings;
            // Global kind stays Proportion (the shipped default); the screen
            // overrides the value alone, at a legal proportion.
            QCOMPARE(settings.scrollingDefaultColumnWidthKind(), ConfigDefaults::scrollingWidthKindProportion());
            settings.setPerScreenScrollingSetting(screen, key(K::DefaultColumnWidthValue), 0.35);
            settings.save();
        }

        const QString path = configPathFor(guard);
        QFile file(path);
        QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(path));
        QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
        file.close();
        QVERIFY(patchScreenValue(root, screen, key(K::DefaultColumnWidthValue), QJsonValue(1200.0)));
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(QJsonDocument(root).toJson());
        file.close();

        Settings reloaded;
        // The pixel-magnitude figure survives verbatim: no per-screen Kind
        // key exists, so the repair must not touch it.
        QCOMPARE(reloaded.getPerScreenScrollingSettings(screen).value(key(K::DefaultColumnWidthValue)).toDouble(),
                 1200.0);
    }

    /// The whole-domain Clear removes the entry outright, across both
    /// sub-domains, and has() tracks it. A clear on a screen with nothing
    /// stored is a no-op that announces nothing. The sizing sub-domain's own
    /// Clear is the narrower one, pinned by axisIsASubDomainOfItsOwn below.
    void clearRemovesTheEntryAndHasTracksIt()
    {
        IsolatedConfigGuard guard;
        Settings settings;
        const QString screen = QStringLiteral("DP-clear");

        QVERIFY(!settings.hasPerScreenScrollingSettings(screen));
        settings.setPerScreenScrollingSetting(screen, key(K::DefaultColumnDisplay),
                                              ConfigDefaults::scrollingColumnDisplayTabbed());
        settings.setPerScreenScrollingSetting(screen, key(K::DefaultWindowHeightPresetIndex), 4);
        QVERIFY(settings.hasPerScreenScrollingSettings(screen));

        QSignalSpy changedSpy(&settings, &Settings::perScreenScrollingSettingsChanged);
        settings.clearPerScreenScrollingSettings(screen);
        QVERIFY(!settings.hasPerScreenScrollingSettings(screen));
        QVERIFY(settings.getPerScreenScrollingSettings(screen).isEmpty());
        QCOMPARE(changedSpy.count(), 1);

        // Clearing again changes nothing and must not announce.
        settings.clearPerScreenScrollingSettings(screen);
        QCOMPARE(changedSpy.count(), 1);
    }

    /// The strip axis shares the screen's entry with the sizing defaults but is
    /// its OWN sub-domain, because the two are surfaced by different cards on
    /// different pages. The New-columns card reports and clears the sizing keys
    /// only, so an axis override must be invisible to its scope chip and must
    /// survive its Clear — otherwise resetting a monitor's column widths would
    /// silently rotate its strip back to the app-wide direction.
    void axisIsASubDomainOfItsOwn()
    {
        IsolatedConfigGuard guard;
        Settings settings;
        const QString screen = QStringLiteral("DP-axisdomain");

        settings.setPerScreenScrollingSetting(screen, key(K::StripAxis), ConfigDefaults::scrollingStripAxisVertical());
        QVERIFY(settings.hasPerScreenScrollingSettings(screen));
        QVERIFY2(!settings.hasPerScreenScrollingSizingSettings(screen),
                 "an axis-only override must not light the New-columns card's chip");

        // A sizing key added beside it does light that chip, so the negative
        // above is the axis being excluded rather than the predicate never
        // answering true.
        settings.setPerScreenScrollingSetting(screen, key(K::DefaultColumnWidthPresetIndex), 3);
        QVERIFY(settings.hasPerScreenScrollingSizingSettings(screen));

        QSignalSpy changedSpy(&settings, &Settings::perScreenScrollingSettingsChanged);
        settings.clearPerScreenScrollingSizingSettings(screen);
        QCOMPARE(changedSpy.count(), 1);
        QVERIFY(!settings.hasPerScreenScrollingSizingSettings(screen));
        // The axis survives the sizing Clear, entry and value both.
        QVERIFY(settings.hasPerScreenScrollingSettings(screen));
        QCOMPARE(settings.getPerScreenScrollingSettings(screen).value(key(K::StripAxis)).toInt(),
                 ConfigDefaults::scrollingStripAxisVertical());
        QVERIFY(!settings.getPerScreenScrollingSettings(screen).contains(key(K::DefaultColumnWidthPresetIndex)));

        // The whole-domain Clear still takes everything, axis included: it is
        // the D-Bus category surface, not a card's chip.
        settings.clearPerScreenScrollingSettings(screen);
        QVERIFY(!settings.hasPerScreenScrollingSettings(screen));
        QVERIFY(settings.getPerScreenScrollingSettings(screen).isEmpty());
    }

    /// The virtual→physical fallback is the DAEMON's job on this channel, not
    /// this store's: a query for a virtual sub-screen with no overrides of its
    /// own returns empty even when its physical monitor has some. Resolving it
    /// here as well would resolve it twice. This is the deliberate difference
    /// from the zone-selector and gap channels, which do fall back.
    void virtualScreenInheritanceIsTheDaemonsJob()
    {
        IsolatedConfigGuard guard;
        Settings settings;
        const QString physical = QStringLiteral("DP-virtscroll");
        const QString virtualId = PhosphorIdentity::VirtualScreenId::make(physical, 0);

        settings.setPerScreenScrollingSetting(physical, key(K::DefaultColumnDisplay),
                                              ConfigDefaults::scrollingColumnDisplayTabbed());
        QVERIFY(settings.hasPerScreenScrollingSettings(physical));
        QVERIFY(settings.getPerScreenScrollingSettings(virtualId).isEmpty());
        QVERIFY(!settings.hasPerScreenScrollingSettings(virtualId));

        // A virtual sub-screen can still carry overrides of its OWN, and those
        // stay distinct from the physical monitor's.
        settings.setPerScreenScrollingSetting(virtualId, key(K::DefaultColumnWidthPresetIndex), 5);
        QCOMPARE(settings.getPerScreenScrollingSettings(virtualId).value(key(K::DefaultColumnWidthPresetIndex)).toInt(),
                 5);
        QVERIFY(!settings.getPerScreenScrollingSettings(physical).contains(key(K::DefaultColumnWidthPresetIndex)));
    }
};

// QTEST_MAIN (not GUILESS): Settings' load path reads QGuiApplication::palette().
QTEST_MAIN(TestSettingsPerScreenScrolling)
#include "test_settings_perscreen_scrolling.moc"
