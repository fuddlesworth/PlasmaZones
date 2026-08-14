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

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
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
        // The value assertion alone cannot catch a MISSING defaultValue: the
        // SizeMode default is Auto = 0, exactly what an absent QVariant
        // coerces to, so pin validity first.
        QVERIFY(sizeMode->defaultValue.isValid());
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
        settings.setScrollingZoneSelectorPreviewWidth(ConfigDefaults::previewWidthSmall());
        QCOMPARE(widthSpy.count(), 1);

        QSignalSpy heightSpy(&settings, &Settings::scrollingZoneSelectorPreviewHeightChanged);
        settings.setScrollingZoneSelectorPreviewHeight(ConfigDefaults::scrollingZoneSelectorPreviewHeight() + 20);
        QCOMPARE(settings.scrollingZoneSelectorPreviewHeight(),
                 ConfigDefaults::scrollingZoneSelectorPreviewHeight() + 20);
        QCOMPARE(heightSpy.count(), 1);
        settings.setScrollingZoneSelectorPreviewHeight(ConfigDefaults::scrollingZoneSelectorPreviewHeight() + 20);
        QCOMPARE(heightSpy.count(), 1);

        QSignalSpy lockSpy(&settings, &Settings::scrollingZoneSelectorPreviewLockAspectChanged);
        const bool lock = !ConfigDefaults::scrollingZoneSelectorPreviewLockAspect();
        settings.setScrollingZoneSelectorPreviewLockAspect(lock);
        QCOMPARE(settings.scrollingZoneSelectorPreviewLockAspect(), lock);
        QCOMPARE(lockSpy.count(), 1);
        settings.setScrollingZoneSelectorPreviewLockAspect(lock);
        QCOMPARE(lockSpy.count(), 1);
    }

    /// The QML-facing *Int adapters silently drop an out-of-range write —
    /// they are the Q_PROPERTY WRITE path, so a widened or inverted guard
    /// would let an out-of-domain enum reach the store and the popup.
    void intAdaptersDropOutOfRangeWrites()
    {
        IsolatedConfigGuard guard;
        Settings settings;

        // The ACCEPT arm first: these adapters are the Q_PROPERTY WRITE path
        // and have no other caller, so without an in-range write the reject
        // probes below could not tell a working guard from an adapter whose
        // forwarding call was deleted outright.
        settings.setScrollingZoneSelectorPositionInt(static_cast<int>(ZoneSelectorPosition::BottomRight));
        QCOMPARE(settings.scrollingZoneSelectorPosition(), ZoneSelectorPosition::BottomRight);
        settings.setScrollingZoneSelectorSizeModeInt(static_cast<int>(ZoneSelectorSizeMode::Manual));
        QCOMPARE(settings.scrollingZoneSelectorSizeMode(), ZoneSelectorSizeMode::Manual);

        const int position = settings.scrollingZoneSelectorPositionInt();
        settings.setScrollingZoneSelectorPositionInt(-1);
        settings.setScrollingZoneSelectorPositionInt(static_cast<int>(ZoneSelectorPosition::BottomRight) + 1);
        QCOMPARE(settings.scrollingZoneSelectorPositionInt(), position);

        const int sizeMode = settings.scrollingZoneSelectorSizeModeInt();
        settings.setScrollingZoneSelectorSizeModeInt(-1);
        settings.setScrollingZoneSelectorSizeModeInt(static_cast<int>(ZoneSelectorSizeMode::Manual) + 1);
        QCOMPARE(settings.scrollingZoneSelectorSizeModeInt(), sizeMode);
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

        // Non-default values for BOTH remaining fields too, so no assertion
        // below compares a pass-through against the same getter it reads
        // (which is correct by construction and carries no signal).
        settings.setScrollingZoneSelectorPreviewHeight(ConfigDefaults::scrollingZoneSelectorPreviewHeight() + 13);
        settings.setScrollingZoneSelectorPreviewLockAspect(!ConfigDefaults::scrollingZoneSelectorPreviewLockAspect());

        const ZoneSelectorConfig config = settings.resolvedScrollingZoneSelectorConfig(QStringLiteral("test-screen-1"));

        QCOMPARE(config.layoutMode, static_cast<int>(ZoneSelectorLayoutMode::Horizontal));
        QCOMPARE(config.position, static_cast<int>(ZoneSelectorPosition::Bottom));
        QCOMPARE(config.sizeMode, static_cast<int>(ZoneSelectorSizeMode::Manual));
        QCOMPARE(config.previewWidth, ConfigDefaults::previewWidthLarge());
        QCOMPARE(config.previewHeight, ConfigDefaults::scrollingZoneSelectorPreviewHeight() + 13);
        QCOMPARE(config.triggerDistance, 77);
        QCOMPARE(config.previewLockAspect, !ConfigDefaults::scrollingZoneSelectorPreviewLockAspect());
        // The interface promises the two grid fields stay at the struct
        // defaults under Horizontal (a single row consults neither).
        QCOMPARE(config.maxRows, ZoneSelectorConfig{}.maxRows);
        QCOMPARE(config.gridColumns, ZoneSelectorConfig{}.gridColumns);
    }

    /// A per-screen override wins over the global value, and clearing it
    /// restores the global. The stamp survives both.
    void perScreenOverrideWinsAndClearRestores()
    {
        IsolatedConfigGuard guard;
        Settings settings;

        const QString screen = QStringLiteral("test-screen-1");
        // A NON-default global (the default position is Top), so the
        // restores-the-global assertions below cannot pass against a
        // resolver that ignored the stored global entirely.
        settings.setScrollingZoneSelectorPosition(ZoneSelectorPosition::Left);
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
                 static_cast<int>(ZoneSelectorPosition::Left));

        settings.clearPerScreenScrollingZoneSelectorSettings(screen);
        QCOMPARE(spy.count(), 2);
        QVERIFY(!settings.hasPerScreenScrollingZoneSelectorSettings(screen));
        QCOMPARE(settings.resolvedScrollingZoneSelectorConfig(screen).position,
                 static_cast<int>(ZoneSelectorPosition::Left));

        // The two per-screen keys no other slot drives: SizeMode and
        // PreviewHeight are admitted members of kStripSelectorKeys with their
        // own validator and merge arms, so a subset-table typo dropping
        // either would silently downgrade them to global-only.
        settings.setPerScreenScrollingZoneSelectorSetting(screen, QString::fromLatin1(ZoneSelectorConfigKey::SizeMode),
                                                          static_cast<int>(ZoneSelectorSizeMode::Manual));
        settings.setPerScreenScrollingZoneSelectorSetting(screen,
                                                          QString::fromLatin1(ZoneSelectorConfigKey::PreviewHeight),
                                                          ConfigDefaults::scrollingZoneSelectorPreviewHeight() + 30);
        const ZoneSelectorConfig overridden = settings.resolvedScrollingZoneSelectorConfig(screen);
        QCOMPARE(overridden.sizeMode, static_cast<int>(ZoneSelectorSizeMode::Manual));
        QCOMPARE(overridden.previewHeight, ConfigDefaults::scrollingZoneSelectorPreviewHeight() + 30);
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

    /// The per-card scope predicates PARTITION the store: clearing one card's
    /// sub-domain must leave the other card's overrides intact, in both
    /// families — a one-key typo in the predicates would silently cross-wipe
    /// the sibling card on a scope-chip reset.
    void perCardScopeClearsArePartitioned()
    {
        IsolatedConfigGuard guard;
        Settings settings;
        const QString screen = QStringLiteral("test-screen-1");

        // Strip family: Position card = Position + TriggerDistance;
        // Size card = SizeMode + PreviewWidth + PreviewHeight +
        // PreviewLockAspect.
        settings.setPerScreenScrollingZoneSelectorSetting(screen, QString::fromLatin1(ZoneSelectorConfigKey::Position),
                                                          static_cast<int>(ZoneSelectorPosition::BottomRight));
        settings.setPerScreenScrollingZoneSelectorSetting(
            screen, QString::fromLatin1(ZoneSelectorConfigKey::TriggerDistance), 66);
        settings.setPerScreenScrollingZoneSelectorSetting(screen, QString::fromLatin1(ZoneSelectorConfigKey::SizeMode),
                                                          static_cast<int>(ZoneSelectorSizeMode::Manual));
        settings.setPerScreenScrollingZoneSelectorSetting(screen,
                                                          QString::fromLatin1(ZoneSelectorConfigKey::PreviewHeight),
                                                          ConfigDefaults::scrollingZoneSelectorPreviewHeight() + 30);
        QVERIFY(settings.hasPerScreenScrollingZoneSelectorPositionSettings(screen));
        QVERIFY(settings.hasPerScreenScrollingZoneSelectorSizeSettings(screen));

        settings.clearPerScreenScrollingZoneSelectorPositionSettings(screen);
        QVERIFY(!settings.hasPerScreenScrollingZoneSelectorPositionSettings(screen));
        QVERIFY(settings.hasPerScreenScrollingZoneSelectorSizeSettings(screen));
        const QVariantMap afterPositionClear = settings.getPerScreenScrollingZoneSelectorSettings(screen);
        QVERIFY(!afterPositionClear.contains(QString::fromLatin1(ZoneSelectorConfigKey::Position)));
        QVERIFY(!afterPositionClear.contains(QString::fromLatin1(ZoneSelectorConfigKey::TriggerDistance)));
        QCOMPARE(afterPositionClear.value(QString::fromLatin1(ZoneSelectorConfigKey::PreviewHeight)).toInt(),
                 ConfigDefaults::scrollingZoneSelectorPreviewHeight() + 30);

        // The reverse direction.
        settings.setPerScreenScrollingZoneSelectorSetting(
            screen, QString::fromLatin1(ZoneSelectorConfigKey::TriggerDistance), 66);
        settings.clearPerScreenScrollingZoneSelectorSizeSettings(screen);
        QVERIFY(settings.hasPerScreenScrollingZoneSelectorPositionSettings(screen));
        QVERIFY(!settings.hasPerScreenScrollingZoneSelectorSizeSettings(screen));
        const QVariantMap afterSizeClear = settings.getPerScreenScrollingZoneSelectorSettings(screen);
        QCOMPARE(afterSizeClear.value(QString::fromLatin1(ZoneSelectorConfigKey::TriggerDistance)).toInt(), 66);
        QVERIFY(!afterSizeClear.contains(QString::fromLatin1(ZoneSelectorConfigKey::SizeMode)));
        QVERIFY(!afterSizeClear.contains(QString::fromLatin1(ZoneSelectorConfigKey::PreviewHeight)));

        // Snapping family, same contract (Position / Arrangement / Size).
        settings.setPerScreenZoneSelectorSetting(screen, QString::fromLatin1(ZoneSelectorConfigKey::Position),
                                                 static_cast<int>(ZoneSelectorPosition::BottomLeft));
        settings.setPerScreenZoneSelectorSetting(screen, QString::fromLatin1(ZoneSelectorConfigKey::LayoutMode),
                                                 static_cast<int>(ZoneSelectorLayoutMode::Vertical));
        settings.setPerScreenZoneSelectorSetting(screen, QString::fromLatin1(ZoneSelectorConfigKey::PreviewWidth),
                                                 ConfigDefaults::previewWidthSmall());
        QVERIFY(settings.hasPerScreenZoneSelectorPositionSettings(screen));
        QVERIFY(settings.hasPerScreenZoneSelectorArrangementSettings(screen));
        QVERIFY(settings.hasPerScreenZoneSelectorSizeSettings(screen));
        settings.clearPerScreenZoneSelectorArrangementSettings(screen);
        QVERIFY(settings.hasPerScreenZoneSelectorPositionSettings(screen));
        QVERIFY(!settings.hasPerScreenZoneSelectorArrangementSettings(screen));
        QVERIFY(settings.hasPerScreenZoneSelectorSizeSettings(screen));
    }

    /// The LOAD path is filtered like the write path: a grid-arrangement key
    /// hand-edited into the on-disk strip group (or reintroduced by a
    /// load-table swap regression) must not enter the store or displace the
    /// Horizontal stamp.
    void loadPathRefusesTheGridArrangementKeys()
    {
        IsolatedConfigGuard guard;
        const QString screen = QStringLiteral("test-screen-1");
        {
            Settings settings;
            settings.setPerScreenScrollingZoneSelectorSetting(screen,
                                                              QString::fromLatin1(ZoneSelectorConfigKey::Position),
                                                              static_cast<int>(ZoneSelectorPosition::BottomRight));
            QVERIFY(settings.save());
        }

        // Smuggle a LayoutMode into the stored strip group the way a
        // hand-edited config would (PerScreen -> ScrollingZoneSelector ->
        // <screen> is the resolver's JSON path for the group prefix).
        const QString configFile = guard.configPath() + QStringLiteral("/plasmazones/config.json");
        QFile file(configFile);
        QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(configFile));
        QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
        file.close();
        QJsonObject perScreen = root.value(QStringLiteral("PerScreen")).toObject();
        QJsonObject category = perScreen.value(QStringLiteral("ScrollingZoneSelector")).toObject();
        QJsonObject entry = category.value(screen).toObject();
        QVERIFY2(!entry.isEmpty(), "the saved override did not land where this test expects — fix the path");
        entry.insert(QString::fromLatin1(ZoneSelectorConfigKey::LayoutMode),
                     static_cast<int>(ZoneSelectorLayoutMode::Grid));
        category.insert(screen, entry);
        perScreen.insert(QStringLiteral("ScrollingZoneSelector"), category);
        root.insert(QStringLiteral("PerScreen"), perScreen);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(QJsonDocument(root).toJson());
        file.close();

        Settings reloaded;
        const QVariantMap stored = reloaded.getPerScreenScrollingZoneSelectorSettings(screen);
        QVERIFY(!stored.contains(QString::fromLatin1(ZoneSelectorConfigKey::LayoutMode)));
        QCOMPARE(stored.value(QString::fromLatin1(ZoneSelectorConfigKey::Position)).toInt(),
                 static_cast<int>(ZoneSelectorPosition::BottomRight));
        QCOMPARE(reloaded.resolvedScrollingZoneSelectorConfig(screen).layoutMode,
                 static_cast<int>(ZoneSelectorLayoutMode::Horizontal));
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
