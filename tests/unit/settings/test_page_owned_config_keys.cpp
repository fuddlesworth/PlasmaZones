// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_page_owned_config_keys.cpp
 * @brief Ownership invariants for SettingsController::pageOwnedConfigKeys.
 *
 * The manifest maps each settings leaf page to the (group, key) pairs it
 * edits, and it drives the breadcrumb kebab's per-page Reset and Discard plus
 * the per-page dirty indicator. It is maintained by hand, and both ways of
 * getting it wrong are silent:
 *
 *   - a key MISSING from every page's list drops out of per-page Reset and
 *     never marks its page dirty, so an edit to it looks unsaved-free and
 *     survives a Reset the user believes cleared the page, and
 *   - a key listed by TWO pages lets one page's Discard revert an edit made on
 *     the other (the shared-tree failure this repo has already shipped once).
 *
 * The manifest's own header states both invariants and notes that a guard has
 * to link the whole SettingsController topology TU. This file is that guard.
 * It is scoped to what can be checked cheaply and exactly: the one-owner rule
 * across the WHOLE manifest, and complete coverage of a swept subset of
 * schema groups. The swept set is the four scrolling groups (Scrolling,
 * Scrolling.Behavior, Scrolling.TabIndicator and Scrolling.DropIndicator,
 * whose page manifests are the newest and least exercised) plus Rendering,
 * whose Gpu key once shipped with no manifest owner.
 */

#include <QSet>
#include <QString>
#include <QStringList>
#include <QTest>

#include <PhosphorConfig/Schema.h>

#include "config/configdefaults.h"
#include "config/settings.h"
#include "config/settingsschema.h"
#include "settings/controller/settingscontroller.h"

using namespace PlasmaZones;

namespace {

QString qualify(const QString& group, const QString& key)
{
    return group + QLatin1Char('/') + key;
}

/// Schema keys under @p group that no page manifest owns ON PURPOSE. Anything
/// not listed here has to have exactly one owner.
const QSet<QString>& deliberatelyUnowned()
{
    static const QSet<QString> kSet{
        // The scrolling master switch is NOT here any more: it is owned by
        // scrolling-columns (its pending sidebar flip has to participate in
        // value-based dirtiness or it is silently lost on exit), and the
        // page-Reset hazard that used to justify unowning it is closed by
        // resetExemptModeEnableKeys() instead — pinned by the exemption slot
        // below.
        // The two global preset lists are NOT here any more: the Columns page
        // carries their editors again, because they are the vocabulary the
        // cycling shortcuts walk on a screen with no template — reachable
        // now that a screen can be set to no template explicitly. An editor
        // means an owner, so they moved into that page's manifest.
        // The default template is NOT here any more either: a template is
        // made default from its card's context menu on Scrolling → Templates,
        // and the library pages are never dirty (isLibraryPage), so the
        // context menu attributes the write to scrolling-columns — the owner
        // of the rest of the Scrolling group — through an external-edit
        // envelope, and the key moved into that page's manifest with it.
        // The set is empty today; it stays so a future genuinely-unowned key
        // has a place to be declared rather than failing the sweep.
    };
    return kSet;
}

} // namespace

class TestPageOwnedConfigKeys : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    /// No (group, key) pair may appear in two pages' lists. A shared pair
    /// means one page's Discard silently reverts the other page's edit.
    void everyKeyHasAtMostOneOwner()
    {
        const auto& manifest = SettingsController::pageOwnedConfigKeys();
        QHash<QString, QString> owner;
        QStringList duplicates;
        for (auto it = manifest.constBegin(); it != manifest.constEnd(); ++it) {
            for (const Settings::ConfigKey& pair : it.value()) {
                const QString qualified = qualify(pair.first, pair.second);
                const auto existing = owner.constFind(qualified);
                if (existing != owner.constEnd()) {
                    duplicates.append(qualified + QLatin1String(": ") + existing.value() + QLatin1String(" and ")
                                      + it.key());
                } else {
                    owner.insert(qualified, it.key());
                }
            }
        }
        duplicates.sort();
        QVERIFY2(duplicates.isEmpty(), qPrintable(duplicates.join(QLatin1String("; "))));
    }

    /// Every pair any page claims must be a key the schema actually declares.
    /// A pair naming a key that no longer exists is dead weight in Reset and
    /// never reports dirty, which is the "missing key" failure wearing a
    /// disguise: the page looks covered while the setting is not.
    ///
    /// Per-screen groups are excluded, not by allowlist but structurally: they
    /// are dynamic (one group per monitor, named from the screen id) and so
    /// have no static schema declaration to match against.
    void everyOwnedKeyIsSchemaDeclared()
    {
        const PhosphorConfig::Schema schema = buildSettingsSchema();
        const auto& manifest = SettingsController::pageOwnedConfigKeys();
        QStringList undeclared;
        for (auto it = manifest.constBegin(); it != manifest.constEnd(); ++it) {
            for (const Settings::ConfigKey& pair : it.value()) {
                const auto group = schema.groups.constFind(pair.first);
                if (group == schema.groups.constEnd()) {
                    undeclared.append(it.key() + QLatin1String(" -> ") + qualify(pair.first, pair.second)
                                      + QLatin1String(" (no such group)"));
                    continue;
                }
                bool found = false;
                for (const PhosphorConfig::KeyDef& def : *group) {
                    if (def.key == pair.second) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    undeclared.append(it.key() + QLatin1String(" -> ") + qualify(pair.first, pair.second));
                }
            }
        }
        undeclared.sort();
        QVERIFY2(undeclared.isEmpty(), qPrintable(undeclared.join(QLatin1String("; "))));
    }

    /// The other direction, over the swept groups: every key the schema
    /// declares under Scrolling, Scrolling.Behavior, Scrolling.TabIndicator,
    /// Scrolling.DropIndicator or Rendering must be owned by exactly one
    /// page, except the entries listed in deliberatelyUnowned().
    ///
    /// The scrolling pages also SHOW two settings they deliberately do not
    /// own — Tiling.Gaps/SmartGaps, forwarded from the shared gaps group and
    /// owned by tiling-behavior, and the per-monitor width and height
    /// overrides, which live in the per-screen scrolling store rather than in
    /// flat config keys. Neither is a Scrolling* schema key, so neither is in
    /// scope here; they are called out so a future reader does not "fix" their
    /// absence by adding them to a scrolling page's list and breaking the
    /// one-owner rule.
    void everySweptSchemaKeyIsOwned()
    {
        const PhosphorConfig::Schema schema = buildSettingsSchema();
        const auto& manifest = SettingsController::pageOwnedConfigKeys();

        QSet<QString> owned;
        for (auto it = manifest.constBegin(); it != manifest.constEnd(); ++it) {
            for (const Settings::ConfigKey& pair : it.value()) {
                owned.insert(qualify(pair.first, pair.second));
            }
        }

        QStringList unowned;
        int checked = 0;
        // All FIVE scrolling schema groups. The two indicator groups were
        // outside this sweep, so a key added to either could drop out of
        // per-page Reset with nothing failing — which is the exact class of
        // regression this file exists to catch, and the DropIndicator group
        // was added while the gap was open. Rendering rides along for the
        // same reason: its Gpu key shipped without a manifest owner (no
        // dirty mark, per-page Reset and Discard silently skipped it)
        // precisely because this sweep did not cover the group.
        for (const QString& group :
             {ConfigDefaults::scrollingGroup(), ConfigDefaults::scrollingBehaviorGroup(),
              ConfigDefaults::scrollingTabIndicatorGroup(), ConfigDefaults::scrollingDropIndicatorGroup(),
              ConfigDefaults::scrollingZoneSelectorGroup(), ConfigDefaults::renderingGroup()}) {
            const auto it = schema.groups.constFind(group);
            QVERIFY2(it != schema.groups.constEnd(), qPrintable(group));
            for (const PhosphorConfig::KeyDef& def : *it) {
                const QString qualified = qualify(group, def.key);
                if (deliberatelyUnowned().contains(qualified)) {
                    continue;
                }
                ++checked;
                if (!owned.contains(qualified)) {
                    unowned.append(qualified);
                }
            }
        }
        unowned.sort();
        QVERIFY2(unowned.isEmpty(), qPrintable(unowned.join(QLatin1String("; "))));
        // The sweep must actually have swept something. Without this the whole
        // slot passes vacuously if the group accessors are ever renamed and
        // the schema stops declaring these groups under these names.
        QVERIFY(checked > 0);
    }

    /// The allowlist itself is a schema key, not a stale string. An entry that
    /// no longer names a declared key would silently excuse nothing while
    /// reading as a live exemption.
    void deliberateExclusionsAreRealKeys()
    {
        const PhosphorConfig::Schema schema = buildSettingsSchema();
        QSet<QString> declared;
        for (auto git = schema.groups.constBegin(); git != schema.groups.constEnd(); ++git) {
            for (const PhosphorConfig::KeyDef& def : git.value()) {
                declared.insert(qualify(git.key(), def.key));
            }
        }
        QStringList stale;
        for (const QString& qualified : deliberatelyUnowned()) {
            if (!declared.contains(qualified)) {
                stale.append(qualified);
            }
        }
        stale.sort();
        QVERIFY2(stale.isEmpty(), qPrintable(stale.join(QLatin1String("; "))));
    }

    /// The reset-exemption contract for the mode enable master switches:
    /// every exempt key must be manifest-owned (a dangling exemption means the
    /// dirty-tracking fix regressed back to unowned), and all three placement
    /// switches must be exempt (an enable key missing here would let a page
    /// Reset flip its mode off).
    void resetExemptEnableKeysAreOwnedAndComplete()
    {
        QSet<QString> owned;
        const auto& manifest = SettingsController::pageOwnedConfigKeys();
        for (auto it = manifest.constBegin(); it != manifest.constEnd(); ++it) {
            for (const auto& gk : it.value()) {
                owned.insert(qualify(gk.first, gk.second));
            }
        }

        const auto& exempt = SettingsController::resetExemptModeEnableKeys();
        QCOMPARE(exempt.size(), 3);
        QSet<QString> exemptQualified;
        // Collect-then-assert, matching the rest of this file: a single
        // dangling exemption must not hide the state of the other two.
        QStringList dangling;
        for (const auto& gk : exempt) {
            const QString qualified = qualify(gk.first, gk.second);
            exemptQualified.insert(qualified);
            if (!owned.contains(qualified)) {
                dangling.append(qualified);
            }
        }
        dangling.sort();
        QVERIFY2(dangling.isEmpty(), qPrintable(dangling.join(QLatin1String("; "))));
        QVERIFY(exemptQualified.contains(qualify(ConfigDefaults::snappingGroup(), ConfigDefaults::enabledKey())));
        QVERIFY(exemptQualified.contains(qualify(ConfigDefaults::tilingGroup(), ConfigDefaults::enabledKey())));
        QVERIFY(exemptQualified.contains(qualify(ConfigDefaults::scrollingGroup(), ConfigDefaults::enabledKey())));
    }

    /// Same contract for the cross-page default selections: both keys must be
    /// manifest-owned (a dangling exemption means the dirty attribution
    /// regressed back to unowned), and both defaults set from the library
    /// pages' context menus must be exempt (an entry missing here would let a
    /// Reset on the owner page silently clear a selection the page never
    /// shows an editor row for).
    void resetExemptDefaultSelectionKeysAreOwnedAndComplete()
    {
        QSet<QString> owned;
        const auto& manifest = SettingsController::pageOwnedConfigKeys();
        for (auto it = manifest.constBegin(); it != manifest.constEnd(); ++it) {
            for (const auto& gk : it.value()) {
                owned.insert(qualify(gk.first, gk.second));
            }
        }

        const auto& exempt = SettingsController::resetExemptDefaultSelectionKeys();
        QCOMPARE(exempt.size(), 2);
        QSet<QString> exemptQualified;
        QStringList dangling;
        for (const auto& gk : exempt) {
            const QString qualified = qualify(gk.first, gk.second);
            exemptQualified.insert(qualified);
            if (!owned.contains(qualified)) {
                dangling.append(qualified);
            }
        }
        dangling.sort();
        QVERIFY2(dangling.isEmpty(), qPrintable(dangling.join(QLatin1String("; "))));
        QVERIFY(exemptQualified.contains(
            qualify(ConfigDefaults::snappingBehaviorWindowHandlingGroup(), ConfigDefaults::defaultLayoutIdKey())));
        QVERIFY(
            exemptQualified.contains(qualify(ConfigDefaults::scrollingGroup(), ConfigDefaults::defaultTemplateKey())));
        // The tiling default is deliberately absent: the Algorithm page
        // carries a real picker for it, so its Reset is honest.
        QVERIFY(
            !exemptQualified.contains(qualify(ConfigDefaults::tilingAlgorithmGroup(), ConfigDefaults::defaultKey())));
    }
};

QTEST_MAIN(TestPageOwnedConfigKeys)
#include "test_page_owned_config_keys.moc"
