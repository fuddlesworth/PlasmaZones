// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_scrolling_zone_selector_settings.cpp
 * @brief The Scrolling.ZoneSelector settings family: the strip-mode drag popup.
 *
 * A sibling of test_scrolling_settings.cpp rather than an extension of it, per
 * that file's own sanctioned-exception note: this is a separate concern with
 * its own fixtures and its own store.
 *
 * The family is the Snapping.ZoneSelector twin minus LayoutMode, GridColumns
 * and MaxRows, which makes three things worth pinning.
 *
 * layoutMode is STAMPED, never read. The resolver hands back the shared
 * ZoneSelectorConfig so both popups can be laid out by one
 * computeZoneSelectorLayout, and the strip popup is always a single horizontal
 * card row. A regression that let the field fall through as Grid would lay the
 * strip out as a grid on every scrolling screen.
 *
 * The per-screen store REUSES the ZoneSelectorConfigKey vocabulary but admits
 * only the six-key subset. The write path shares its validator with the
 * snapping twin, which accepts a LayoutMode quite happily, so a D-Bus caller
 * could otherwise store one and displace the stamp. The rejection is asserted,
 * not assumed.
 *
 * The two stores are SEPARATE despite the shared key names. An override written
 * for one selector must not surface in the other's resolved config.
 */

#include <QSignalSpy>
#include <QTest>
#include <QVariantMap>

#include <PhosphorConfig/Schema.h>
#include <PhosphorIdentity/VirtualScreenId.h>

#include "config/configdefaults.h"
#include "config/settings.h"
#include "config/settingsschema.h"
#include "core/interfaces/settings_interfaces.h"
#include "core/types/enums.h"
#include "helpers/IsolatedConfigGuard.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

class TestScrollingZoneSelectorSettings : public QObject
{
    Q_OBJECT

private:
    static const PhosphorConfig::KeyDef* findKey(const PhosphorConfig::Schema& schema, const QString& group,
                                                 const QString& key)
    {
        const auto it = schema.groups.constFind(group);
        if (it == schema.groups.constEnd()) {
            return nullptr;
        }
        for (const PhosphorConfig::KeyDef& def : it.value()) {
            if (def.key == key) {
                return &def;
            }
        }
        return nullptr;
    }

private Q_SLOTS:

    /// Every key in the group is schema-declared with the ConfigDefaults value,
    /// so a fresh install reads the documented defaults rather than a
    /// zero-initialized husk.
    void schemaDeclaresTheDefaults()
    {
        const PhosphorConfig::Schema schema = buildSettingsSchema();
        const QString group = ConfigDefaults::scrollingZoneSelectorGroup();

        const auto* enabled = findKey(schema, group, ConfigDefaults::enabledKey());
        QVERIFY(enabled);
        QCOMPARE(enabled->defaultValue.toBool(), ConfigDefaults::scrollingZoneSelectorEnabled());

        const auto* trigger = findKey(schema, group, ConfigDefaults::triggerDistanceKey());
        QVERIFY(trigger);
        QCOMPARE(trigger->defaultValue.toInt(), ConfigDefaults::scrollingZoneSelectorTriggerDistance());

        const auto* position = findKey(schema, group, ConfigDefaults::positionKey());
        QVERIFY(position);
        QCOMPARE(position->defaultValue.toInt(), ConfigDefaults::scrollingZoneSelectorPosition());

        const auto* sizeMode = findKey(schema, group, ConfigDefaults::sizeModeKey());
        QVERIFY(sizeMode);
        QCOMPARE(sizeMode->defaultValue.toInt(), ConfigDefaults::scrollingZoneSelectorSizeMode());

        const auto* previewWidth = findKey(schema, group, ConfigDefaults::previewWidthKey());
        QVERIFY(previewWidth);
        QCOMPARE(previewWidth->defaultValue.toInt(), ConfigDefaults::scrollingZoneSelectorPreviewWidth());

        const auto* previewHeight = findKey(schema, group, ConfigDefaults::previewHeightKey());
        QVERIFY(previewHeight);
        QCOMPARE(previewHeight->defaultValue.toInt(), ConfigDefaults::scrollingZoneSelectorPreviewHeight());

        const auto* lockAspect = findKey(schema, group, ConfigDefaults::previewLockAspectKey());
        QVERIFY(lockAspect);
        QCOMPARE(lockAspect->defaultValue.toBool(), ConfigDefaults::scrollingZoneSelectorPreviewLockAspect());
    }

    /// The three grid-arrangement keys of the snapping twin have no counterpart
    /// here. Declaring one would give the settings UI a knob the strip popup
    /// silently ignores.
    void schemaOmitsTheGridArrangementKeys()
    {
        const PhosphorConfig::Schema schema = buildSettingsSchema();
        const QString group = ConfigDefaults::scrollingZoneSelectorGroup();

        QVERIFY(!findKey(schema, group, ConfigDefaults::layoutModeKey()));
        QVERIFY(!findKey(schema, group, ConfigDefaults::gridColumnsKey()));
        QVERIFY(!findKey(schema, group, ConfigDefaults::maxRowsKey()));
    }

    /// A fresh Settings reads every default back through its accessors.
    void freshSettingsReadsTheDefaults()
    {
        IsolatedConfigGuard guard;
        Settings settings;

        QCOMPARE(settings.scrollingZoneSelectorEnabled(), ConfigDefaults::scrollingZoneSelectorEnabled());
        QCOMPARE(settings.scrollingZoneSelectorTriggerDistance(),
                 ConfigDefaults::scrollingZoneSelectorTriggerDistance());
        QCOMPARE(static_cast<int>(settings.scrollingZoneSelectorPosition()),
                 ConfigDefaults::scrollingZoneSelectorPosition());
        QCOMPARE(static_cast<int>(settings.scrollingZoneSelectorSizeMode()),
                 ConfigDefaults::scrollingZoneSelectorSizeMode());
        QCOMPARE(settings.scrollingZoneSelectorPreviewWidth(), ConfigDefaults::scrollingZoneSelectorPreviewWidth());
        QCOMPARE(settings.scrollingZoneSelectorPreviewHeight(), ConfigDefaults::scrollingZoneSelectorPreviewHeight());
        QCOMPARE(settings.scrollingZoneSelectorPreviewLockAspect(),
                 ConfigDefaults::scrollingZoneSelectorPreviewLockAspect());
    }

    /// Each setter round-trips and emits exactly once, and a write of the value
    /// already stored emits nothing.
    void settersRoundTripAndEmitOnChangeOnly()
    {
        IsolatedConfigGuard guard;
        Settings settings;

        QSignalSpy enabledSpy(&settings, &Settings::scrollingZoneSelectorEnabledChanged);
        const bool flipped = !ConfigDefaults::scrollingZoneSelectorEnabled();
        settings.setScrollingZoneSelectorEnabled(flipped);
        QCOMPARE(settings.scrollingZoneSelectorEnabled(), flipped);
        QCOMPARE(enabledSpy.count(), 1);
        settings.setScrollingZoneSelectorEnabled(flipped);
        QCOMPARE(enabledSpy.count(), 1);

        QSignalSpy triggerSpy(&settings, &Settings::scrollingZoneSelectorTriggerDistanceChanged);
        settings.setScrollingZoneSelectorTriggerDistance(ConfigDefaults::scrollingZoneSelectorTriggerDistance() + 10);
        QCOMPARE(settings.scrollingZoneSelectorTriggerDistance(),
                 ConfigDefaults::scrollingZoneSelectorTriggerDistance() + 10);
        QCOMPARE(triggerSpy.count(), 1);
        settings.setScrollingZoneSelectorTriggerDistance(ConfigDefaults::scrollingZoneSelectorTriggerDistance() + 10);
        QCOMPARE(triggerSpy.count(), 1);

        QSignalSpy positionSpy(&settings, &Settings::scrollingZoneSelectorPositionChanged);
        settings.setScrollingZoneSelectorPosition(ZoneSelectorPosition::BottomRight);
        QCOMPARE(settings.scrollingZoneSelectorPosition(), ZoneSelectorPosition::BottomRight);
        QCOMPARE(positionSpy.count(), 1);
        settings.setScrollingZoneSelectorPosition(ZoneSelectorPosition::BottomRight);
        QCOMPARE(positionSpy.count(), 1);

        QSignalSpy sizeModeSpy(&settings, &Settings::scrollingZoneSelectorSizeModeChanged);
        settings.setScrollingZoneSelectorSizeMode(ZoneSelectorSizeMode::Manual);
        QCOMPARE(settings.scrollingZoneSelectorSizeMode(), ZoneSelectorSizeMode::Manual);
        QCOMPARE(sizeModeSpy.count(), 1);
        settings.setScrollingZoneSelectorSizeMode(ZoneSelectorSizeMode::Manual);
        QCOMPARE(sizeModeSpy.count(), 1);

        QSignalSpy widthSpy(&settings, &Settings::scrollingZoneSelectorPreviewWidthChanged);
        settings.setScrollingZoneSelectorPreviewWidth(ConfigDefaults::previewWidthSmall());
        QCOMPARE(settings.scrollingZoneSelectorPreviewWidth(), ConfigDefaults::previewWidthSmall());
        QCOMPARE(widthSpy.count(), 1);

        QSignalSpy heightSpy(&settings, &Settings::scrollingZoneSelectorPreviewHeightChanged);
        settings.setScrollingZoneSelectorPreviewHeight(ConfigDefaults::scrollingZoneSelectorPreviewHeight() + 20);
        QCOMPARE(settings.scrollingZoneSelectorPreviewHeight(),
                 ConfigDefaults::scrollingZoneSelectorPreviewHeight() + 20);
        QCOMPARE(heightSpy.count(), 1);

        QSignalSpy lockSpy(&settings, &Settings::scrollingZoneSelectorPreviewLockAspectChanged);
        const bool lock = !ConfigDefaults::scrollingZoneSelectorPreviewLockAspect();
        settings.setScrollingZoneSelectorPreviewLockAspect(lock);
        QCOMPARE(settings.scrollingZoneSelectorPreviewLockAspect(), lock);
        QCOMPARE(lockSpy.count(), 1);
        settings.setScrollingZoneSelectorPreviewLockAspect(lock);
        QCOMPARE(lockSpy.count(), 1);
    }

    /// Writing this family must not disturb the snapping selector's keys: the
    /// two groups share every key spelling and differ only by group.
    void writesDoNotLeakIntoTheSnappingSelector()
    {
        IsolatedConfigGuard guard;
        Settings settings;

        const int snappingTrigger = settings.zoneSelectorTriggerDistance();
        const auto snappingPosition = settings.zoneSelectorPosition();

        settings.setScrollingZoneSelectorTriggerDistance(snappingTrigger + 7);
        settings.setScrollingZoneSelectorPosition(ZoneSelectorPosition::BottomLeft);

        QCOMPARE(settings.zoneSelectorTriggerDistance(), snappingTrigger);
        QCOMPARE(settings.zoneSelectorPosition(), snappingPosition);
    }

    /// With no override, the resolved config is the global values plus the
    /// stamped Horizontal layout mode.
    void resolvedConfigStampsHorizontalAndReadsGlobals()
    {
        IsolatedConfigGuard guard;
        Settings settings;

        settings.setScrollingZoneSelectorPosition(ZoneSelectorPosition::Bottom);
        settings.setScrollingZoneSelectorSizeMode(ZoneSelectorSizeMode::Manual);
        settings.setScrollingZoneSelectorPreviewWidth(ConfigDefaults::previewWidthLarge());
        settings.setScrollingZoneSelectorTriggerDistance(77);

        const ZoneSelectorConfig config = settings.resolvedScrollingZoneSelectorConfig(QStringLiteral("test-screen-1"));

        QCOMPARE(config.layoutMode, static_cast<int>(ZoneSelectorLayoutMode::Horizontal));
        QCOMPARE(config.position, static_cast<int>(ZoneSelectorPosition::Bottom));
        QCOMPARE(config.sizeMode, static_cast<int>(ZoneSelectorSizeMode::Manual));
        QCOMPARE(config.previewWidth, ConfigDefaults::previewWidthLarge());
        QCOMPARE(config.triggerDistance, 77);
        QCOMPARE(config.previewLockAspect, settings.scrollingZoneSelectorPreviewLockAspect());
    }

    /// A per-screen override wins over the global value, and clearing it
    /// restores the global. The stamp survives both.
    void perScreenOverrideWinsAndClearRestores()
    {
        IsolatedConfigGuard guard;
        Settings settings;

        const QString screen = QStringLiteral("test-screen-1");
        settings.setScrollingZoneSelectorPosition(ZoneSelectorPosition::Top);
        const int globalTrigger = settings.scrollingZoneSelectorTriggerDistance();

        QVERIFY(!settings.hasPerScreenScrollingZoneSelectorSettings(screen));

        QSignalSpy spy(&settings, &Settings::perScreenScrollingZoneSelectorSettingsChanged);
        settings.setPerScreenScrollingZoneSelectorSetting(screen, QString::fromLatin1(ZoneSelectorConfigKey::Position),
                                                          static_cast<int>(ZoneSelectorPosition::BottomRight));
        QCOMPARE(spy.count(), 1);
        QVERIFY(settings.hasPerScreenScrollingZoneSelectorSettings(screen));
        QCOMPARE(settings.getPerScreenScrollingZoneSelectorSettings(screen)
                     .value(QString::fromLatin1(ZoneSelectorConfigKey::Position))
                     .toInt(),
                 static_cast<int>(ZoneSelectorPosition::BottomRight));

        ZoneSelectorConfig config = settings.resolvedScrollingZoneSelectorConfig(screen);
        QCOMPARE(config.position, static_cast<int>(ZoneSelectorPosition::BottomRight));
        QCOMPARE(config.layoutMode, static_cast<int>(ZoneSelectorLayoutMode::Horizontal));
        QCOMPARE(config.triggerDistance, globalTrigger);

        // Another screen is untouched by the override.
        QCOMPARE(settings.resolvedScrollingZoneSelectorConfig(QStringLiteral("test-screen-2")).position,
                 static_cast<int>(ZoneSelectorPosition::Top));

        settings.clearPerScreenScrollingZoneSelectorSettings(screen);
        QCOMPARE(spy.count(), 2);
        QVERIFY(!settings.hasPerScreenScrollingZoneSelectorSettings(screen));
        QCOMPARE(settings.resolvedScrollingZoneSelectorConfig(screen).position,
                 static_cast<int>(ZoneSelectorPosition::Top));
    }

    /// An override stored on the physical monitor resolves for its virtual
    /// sub-screens, matching the zone-selector twin.
    void perScreenOverrideResolvesForVirtualSubScreens()
    {
        IsolatedConfigGuard guard;
        Settings settings;

        const QString physical = QStringLiteral("test-screen-1");
        const QString virtualId = PhosphorIdentity::VirtualScreenId::make(physical, 0);

        const int defaultPosition = settings.resolvedScrollingZoneSelectorConfig(virtualId).position;
        QVERIFY(defaultPosition != static_cast<int>(ZoneSelectorPosition::BottomRight));

        settings.setPerScreenScrollingZoneSelectorSetting(physical,
                                                          QString::fromLatin1(ZoneSelectorConfigKey::Position),
                                                          static_cast<int>(ZoneSelectorPosition::BottomRight));

        QCOMPARE(settings.resolvedScrollingZoneSelectorConfig(virtualId).position,
                 static_cast<int>(ZoneSelectorPosition::BottomRight));

        const QString otherVirtual = PhosphorIdentity::VirtualScreenId::make(QStringLiteral("other-screen"), 0);
        QCOMPARE(settings.resolvedScrollingZoneSelectorConfig(otherVirtual).position, defaultPosition);
    }

    /// The three grid-arrangement keys are refused on the per-screen write path
    /// even though the shared validator would accept them. A stored LayoutMode
    /// would reach the resolver's merge and displace the Horizontal stamp.
    void perScreenWriteRefusesTheGridArrangementKeys()
    {
        IsolatedConfigGuard guard;
        Settings settings;

        const QString screen = QStringLiteral("test-screen-1");
        for (const char* rejected :
             {ZoneSelectorConfigKey::LayoutMode, ZoneSelectorConfigKey::GridColumns, ZoneSelectorConfigKey::MaxRows}) {
            settings.setPerScreenScrollingZoneSelectorSetting(screen, QString::fromLatin1(rejected), 2);
        }

        QVERIFY(!settings.hasPerScreenScrollingZoneSelectorSettings(screen));
        QCOMPARE(settings.resolvedScrollingZoneSelectorConfig(screen).layoutMode,
                 static_cast<int>(ZoneSelectorLayoutMode::Horizontal));
    }

    /// The two per-screen stores are disjoint: an override written for one
    /// selector never surfaces in the other's resolved config, despite the
    /// shared key vocabulary.
    void perScreenStoresAreDisjoint()
    {
        IsolatedConfigGuard guard;
        Settings settings;

        const QString screen = QStringLiteral("test-screen-1");
        const int snappingBefore = settings.resolvedZoneSelectorConfig(screen).triggerDistance;

        settings.setPerScreenScrollingZoneSelectorSetting(
            screen, QString::fromLatin1(ZoneSelectorConfigKey::TriggerDistance), snappingBefore + 11);

        QVERIFY(!settings.hasPerScreenZoneSelectorSettings(screen));
        QCOMPARE(settings.resolvedZoneSelectorConfig(screen).triggerDistance, snappingBefore);
        QCOMPARE(settings.resolvedScrollingZoneSelectorConfig(screen).triggerDistance, snappingBefore + 11);
    }

    /// Overrides survive a save/load round trip through their own group prefix,
    /// which is what keeps them off the snapping selector's groups on disk.
    void perScreenOverridesPersist()
    {
        IsolatedConfigGuard guard;

        const QString screen = QStringLiteral("test-screen-1");
        {
            Settings settings;
            settings.setPerScreenScrollingZoneSelectorSetting(
                screen, QString::fromLatin1(ZoneSelectorConfigKey::PreviewWidth), ConfigDefaults::previewWidthSmall());
            settings.setPerScreenScrollingZoneSelectorSetting(
                screen, QString::fromLatin1(ZoneSelectorConfigKey::PreviewLockAspect), false);
            QVERIFY(settings.save());
        }

        Settings reloaded;
        QVERIFY(reloaded.hasPerScreenScrollingZoneSelectorSettings(screen));
        const ZoneSelectorConfig config = reloaded.resolvedScrollingZoneSelectorConfig(screen);
        QCOMPARE(config.previewWidth, ConfigDefaults::previewWidthSmall());
        QCOMPARE(config.previewLockAspect, false);
        // The snapping store stayed empty across the round trip.
        QVERIFY(!reloaded.hasPerScreenZoneSelectorSettings(screen));
    }
};

// QTEST_MAIN (not GUILESS) for the same reason as test_scrolling_settings:
// constructing Settings reaches resolved colour getters that read
// QGuiApplication::palette().
QTEST_MAIN(TestScrollingZoneSelectorSettings)
#include "test_scrolling_zone_selector_settings.moc"
