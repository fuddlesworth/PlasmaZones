// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// The Shortcuts.Scrolling family is spelled out FOUR times by hand, and
// nothing used to compare the four:
//
//   1. the config schema group Shortcuts.Scrolling (settingsschema_scrolling.cpp)
//   2. ShortcutManager's registration table          (shortcutmanager.cpp)
//   3. the cheatsheet catalog                        (shortcutmanager_catalog.cpp)
//   4. the D-Bus settings registry                   (settingsadaptor_registry_scrolling.cpp)
//
// Both drift directions are silent and user-visible: a row in 1+2 with no
// catalog entry binds correctly but never appears in Settings or on the
// cheatsheet, and a row in 1+3 with no registration entry shows in the UI and
// never binds. A stale count comment in the catalog (it claimed 20 rows for a
// 21-row group) is the evidence that hand-counting this family does drift.
//
// List 2 comes from staticShortcutIds(), which exists because the table has
// internal linkage and cheatsheetModel() is a COMPRESSED view of it: an
// opposed pair that is fully bound collapses into one row, so the pair's
// second member has no row of its own and the model cannot enumerate the
// registration surface.
//
// List 3 is pinned over the rows that do survive: every scrolling row must
// carry the catalog's scrolling mode. An id the catalog does not know still
// produces a row (it comes from the registration table) but with an empty
// mode, so the gap surfaces without needing to see the catalog table itself.
// A compressed row keeps its family's first member id, so it is checked too.
//
// List 4 is reached transitively rather than duplicated: this test pins that
// every schema key has its Settings Q_PROPERTY, and
// test_settings_registry_contract's testEveryShortcutPropertyIsRegistered
// already pins every *Shortcut property against the D-Bus getter registry.
// Asserting the registry again here would copy that canary, not extend it.

#include "daemon/controllers/shortcutmanager.h"
#include "daemon/controllers/shortcutmanager_ids.h"

#include "config/configdefaults.h"
#include "config/settings.h"
#include "config/settingsschema.h"
#include "helpers/IsolatedConfigGuard.h"

#include <PhosphorShortcuts/IBackend.h>

#include <QHash>
#include <QKeySequence>
#include <QMetaObject>
#include <QMetaProperty>
#include <QSet>
#include <QSignalSpy>
#include <QStringList>
#include <QTest>
#include <QVariantList>

#include <memory>

using PlasmaZones::ConfigDefaults;
using PlasmaZones::Settings;
using PlasmaZones::ShortcutManager;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

namespace {

/// A backend that grabs nothing, so the test needs no compositor.
///
/// It does NOT stop families from compressing: the registry reports each
/// action's bound default, so an opposed pair with two real defaults still
/// collapses. That is why list 2 is read from the registration table rather
/// than from the model — see the file comment.
class SilentBackend : public PhosphorShortcuts::IBackend
{
    Q_OBJECT
public:
    void registerShortcut(const QString& /*id*/, const QKeySequence& /*defaultSeq*/, const QKeySequence& /*currentSeq*/,
                          const QString& /*description*/, bool /*persistent*/) override
    {
    }

    void updateShortcut(const QString& /*id*/, const QKeySequence& /*defaultSeq*/,
                        const QKeySequence& /*newTrigger*/) override
    {
    }

    void unregisterShortcut(const QString& /*id*/) override
    {
    }

    void flush() override
    {
        Q_EMIT ready();
    }

    /// Drive one registered action the way the platform would: the registry
    /// listens to activated() and dispatches to the owning handler.
    void fire(const QString& id)
    {
        Q_EMIT activated(id);
    }
};

/// "FocusColumnFirst" → "focus_column_first", the one rule that maps a schema
/// key onto its ShortcutIds spelling. Note the id prefix is `scroll_`, not
/// `scrolling_` — the config keys and the action ids disagree on that stem.
QString snakeCase(const QString& camel)
{
    QString out;
    out.reserve(camel.size() + 8);
    for (int i = 0; i < camel.size(); ++i) {
        const QChar c = camel.at(i);
        if (c.isUpper() && i > 0) {
            out.append(QLatin1Char('_'));
        }
        out.append(c.toLower());
    }
    return out;
}

} // namespace

class TestScrollingShortcutParity : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void schemaKeysMatchRegistrationAndCatalog()
    {
        IsolatedConfigGuard guard;

        const auto& schema = PlasmaZones::cachedSettingsSchema();
        const auto group = schema.groups.value(ConfigDefaults::shortcutsScrollingGroup());
        // Vacuity guard: a lookup that returned nothing would make every set
        // compare below trivially pass.
        QVERIFY2(!group.isEmpty(), "Schema group Shortcuts.Scrolling is empty: the schema lookup itself is broken.");

        QSet<QString> expectedIds;
        QStringList missingProperties;
        Settings settings(nullptr);
        const QMetaObject* mo = settings.metaObject();
        for (const auto& def : group) {
            expectedIds.insert(QStringLiteral("scroll_") + snakeCase(def.key));
            // The bridge to list 4: the D-Bus registry key equals the
            // property name, and the registry contract test pins every
            // *Shortcut property against the adaptor's getters.
            const QString property = QStringLiteral("scrolling") + def.key + QStringLiteral("Shortcut");
            if (mo->indexOfProperty(property.toLatin1().constData()) < 0) {
                missingProperties.append(property);
            }
        }
        QVERIFY2(missingProperties.isEmpty(),
                 qPrintable(QStringLiteral("Shortcuts.Scrolling schema keys with no Settings Q_PROPERTY (so the "
                                           "D-Bus registry contract cannot see them either): %1")
                                .arg(missingProperties.join(QStringLiteral(", ")))));

        // ─── List 2: the registration table ────────────────────────────────
        QSet<QString> registeredIds;
        const QStringList allRegistered = ShortcutManager::staticShortcutIds();
        QVERIFY2(!allRegistered.isEmpty(), "Registration table is empty: the accessor itself is broken.");
        for (const QString& id : allRegistered) {
            if (id.startsWith(QLatin1String("scroll_"))) {
                registeredIds.insert(id);
            }
        }
        // One legacy exception to the prefix heuristic: the floating/tiling
        // focus switch was promoted out of the Scrolling family but keeps
        // its scroll_-prefixed ON-DISK id (the id is the kglobalshortcutsrc
        // record key, and the ids header forbids renaming shipped ids). Its
        // schema key lives in Shortcuts.Global now, so it is pinned by
        // globalPromotedRowKeepsFourWayParity below, not by this family.
        registeredIds.remove(QLatin1String(PlasmaZones::ShortcutIds::kIdSwitchFocusFloatTiling));

        const QStringList onlyInSchema = QStringList(QSet(expectedIds).subtract(registeredIds).values());
        const QStringList onlyInDaemon = QStringList(QSet(registeredIds).subtract(expectedIds).values());
        QVERIFY2(onlyInSchema.isEmpty(),
                 qPrintable(QStringLiteral("Schema declares these scrolling chords but nothing registers them, so "
                                           "they can never bind: %1")
                                .arg(onlyInSchema.join(QStringLiteral(", ")))));
        QVERIFY2(onlyInDaemon.isEmpty(),
                 qPrintable(QStringLiteral("The daemon registers these scrolling chords with no schema key, so they "
                                           "have no default and cannot be persisted: %1")
                                .arg(onlyInDaemon.join(QStringLiteral(", ")))));

        // ─── List 3: the cheatsheet catalog ────────────────────────────────
        ShortcutManager manager(&settings);
        manager.setBackendForTesting(std::make_unique<SilentBackend>());
        manager.registerShortcuts();

        QStringList uncatalogued;
        int scrollingRows = 0;
        const QVariantList rows = manager.cheatsheetModel();
        for (const QVariant& v : rows) {
            const QVariantMap row = v.toMap();
            const QString id = row.value(QStringLiteral("id")).toString();
            if (!id.startsWith(QLatin1String("scroll_"))) {
                continue;
            }
            // Same legacy-id exemption as list 2: the promoted mode-neutral
            // row keeps its scroll_-prefixed on-disk id but is catalogued
            // under General/"all", pinned by its own slot below.
            if (id == QLatin1String(PlasmaZones::ShortcutIds::kIdSwitchFocusFloatTiling)) {
                continue;
            }
            ++scrollingRows;
            if (row.value(QStringLiteral("mode")).toString() != QLatin1String("scrolling")) {
                uncatalogued.append(id);
            }
        }
        QVERIFY2(scrollingRows > 0, "Cheatsheet model produced no scrolling rows: the model call is broken.");
        QVERIFY2(uncatalogued.isEmpty(),
                 qPrintable(QStringLiteral("These registered scrolling chords are missing from the cheatsheet "
                                           "catalog, so they bind but never appear in Settings or on the sheet: %1")
                                .arg(uncatalogued.join(QStringLiteral(", ")))));
    }

    /// The promoted floating/tiling focus switch left the scrolling family
    /// (the only one with a 4-way parity canary) for Shortcuts.Global,
    /// which has none — and the promotion itself shipped without a
    /// Q_PROPERTY, proving the loss matters. This slot restores the four
    /// legs for that one row: schema key, Settings Q_PROPERTY (the bridge
    /// to the D-Bus registry contract test), registration table, and a
    /// catalogued cheatsheet row.
    void globalPromotedRowKeepsFourWayParity()
    {
        IsolatedConfigGuard guard;

        const auto& schema = PlasmaZones::cachedSettingsSchema();
        const auto group = schema.groups.value(ConfigDefaults::shortcutsGlobalGroup());
        QVERIFY2(!group.isEmpty(), "Schema group Shortcuts.Global is empty: the schema lookup itself is broken.");
        bool schemaHasKey = false;
        for (const auto& def : group) {
            if (def.key == QLatin1String("SwitchFocusFloatTiling")) {
                schemaHasKey = true;
                break;
            }
        }
        QVERIFY2(schemaHasKey, "Shortcuts.Global schema lost the SwitchFocusFloatTiling key.");

        Settings settings(nullptr);
        QVERIFY2(settings.metaObject()->indexOfProperty("switchFocusFloatTilingShortcut") >= 0,
                 "Settings lost the switchFocusFloatTilingShortcut Q_PROPERTY, so bulk reload/profile apply "
                 "cannot re-emit it and the D-Bus registry contract test cannot see it.");

        const QString id = QLatin1String(PlasmaZones::ShortcutIds::kIdSwitchFocusFloatTiling);
        // Pin the LITERAL on-disk spelling, not just the constant: the id is
        // the kglobalshortcutsrc record key, so a well-meaning rename of the
        // constant's VALUE would orphan every user's saved chord while every
        // constant-based check stayed green.
        QCOMPARE(id, QStringLiteral("scroll_switch_focus_float_tiling"));
        QVERIFY2(ShortcutManager::staticShortcutIds().contains(id),
                 "The registration table lost the floating/tiling focus switch row.");

        ShortcutManager manager(&settings);
        manager.setBackendForTesting(std::make_unique<SilentBackend>());
        manager.registerShortcuts();
        bool rowFound = false;
        const QVariantList rows = manager.cheatsheetModel();
        for (const QVariant& v : rows) {
            const QVariantMap row = v.toMap();
            if (row.value(QStringLiteral("id")).toString() != id) {
                continue;
            }
            rowFound = true;
            // The uncatalogued fallback yields category "Other" at order 99
            // with mode "all" and NO description, so a bare category/mode
            // check would stay green with the catalog entry deleted. Pin
            // the category and order (General is 0) and the tooltip's
            // presence instead.
            QCOMPARE(row.value(QStringLiteral("category")).toString(), QStringLiteral("General"));
            QCOMPARE(row.value(QStringLiteral("categoryOrder")).toInt(), 0);
            QVERIFY2(!row.value(QStringLiteral("description")).toString().isEmpty(),
                     "The cheatsheet row lost its catalog entry (the Other-bucket fallback carries no tooltip).");
            QCOMPARE(row.value(QStringLiteral("mode")).toString(), QStringLiteral("all"));
            break;
        }
        QVERIFY2(rowFound, "The cheatsheet model produced no row for the floating/tiling focus switch.");
    }

    /// Pins the layout-capability tag split the daemon's layoutSupportForScreen
    /// gates rely on: the four layout-selection ids plus the quick-layout
    /// digit family carry the capability tag "layouts", while the two
    /// engine-routed layout-group rows stay "all" (Reapply routes through
    /// reapplyLayout and Arrange All through snapAllWindows, which every
    /// engine implements). The "layouts" rows show whenever the bound
    /// screen's engine consumes layouts at all — Placement AND Templates
    /// (scrolling screens included, where the keys drive the template) —
    /// and hide only for a LayoutSupport::None engine, which no shipped
    /// engine reports today; the tag is still load-bearing because it is
    /// what an embedder's None engine keys off. A silent retag in either
    /// direction would make the sheet advertise a refusing key or hide a
    /// working one.
    void layoutCapabilityTagsMatchTheGates()
    {
        IsolatedConfigGuard guard;
        Settings settings(nullptr);
        ShortcutManager manager(&settings);
        manager.setBackendForTesting(std::make_unique<SilentBackend>());
        manager.registerShortcuts();

        const QSet<QString> expectLayouts{QStringLiteral("previous_layout"), QStringLiteral("next_layout"),
                                          QStringLiteral("layout_picker"), QStringLiteral("toggle_layout_lock"),
                                          QStringLiteral("quick_layout_1")};
        const QSet<QString> expectAll{QStringLiteral("resnap_to_new_layout"), QStringLiteral("snap_all_windows")};
        // Retile acts on either engine mode and is a no-op on snapping, so it
        // carries the mode-union tag; a retag back to "autotile" would hide
        // the row on every scrolling screen with nothing else failing.
        const QSet<QString> expectManaged{QStringLiteral("retile")};

        QHash<QString, QString> modeById;
        QHash<QString, int> categoryOrderById;
        QHash<QString, QString> descriptionById;
        QHash<QString, QString> templatesDescriptionById;
        const QVariantList rows = manager.cheatsheetModel();
        for (const QVariant& v : rows) {
            const QVariantMap row = v.toMap();
            const QString id = row.value(QStringLiteral("id")).toString();
            modeById.insert(id, row.value(QStringLiteral("mode")).toString());
            categoryOrderById.insert(id, row.value(QStringLiteral("categoryOrder")).toInt());
            descriptionById.insert(id, row.value(QStringLiteral("description")).toString());
            templatesDescriptionById.insert(id, row.value(QStringLiteral("templatesDescription")).toString());
        }
        // The quick-layout family compresses into one row keyed by its first
        // member, so quick_layout_1 stands in for the whole digit family.
        //
        // Accumulated rather than asserted per row: QVERIFY / QCOMPARE abort the
        // whole slot on the first failure, so one retagged shortcut would hide
        // the state of every id after it and the next run would report a
        // different single failure.
        QStringList failures;
        const auto checkTag = [&modeById, &failures](const QSet<QString>& ids, const QString& expected) {
            for (const QString& id : ids) {
                if (!modeById.contains(id)) {
                    failures.append(QStringLiteral("no cheatsheet row for %1").arg(id));
                    continue;
                }
                if (modeById.value(id) != expected) {
                    failures.append(QStringLiteral("%1 tagged %2, expected %3").arg(id, modeById.value(id), expected));
                }
            }
        };
        checkTag(expectLayouts, QStringLiteral("layouts"));
        checkTag(expectAll, QStringLiteral("all"));
        checkTag(expectManaged, QStringLiteral("managed"));
        // And the row moved to General (order 0) with the retag: a
        // mode-neutral verb filed under an Autotile heading would read as
        // autotile-only on the sheet whatever its tag says.
        if (categoryOrderById.value(QStringLiteral("retile"), -1) != 0) {
            failures.append(QStringLiteral("retile sits at category order %1, expected 0 (General)")
                                .arg(categoryOrderById.value(QStringLiteral("retile"), -1)));
        }
        // And the row says what it does on a SCROLLING screen: the sheet
        // shows templatesDescription there, so a dropped tooltip would leave
        // the autotile wording on a screen where the verb resets the strip.
        const QString retileTemplates = templatesDescriptionById.value(QStringLiteral("retile"));
        if (retileTemplates.isEmpty() || retileTemplates == descriptionById.value(QStringLiteral("retile"))) {
            failures.append(QStringLiteral("retile has no scrolling-screen wording of its own"));
        }
        QVERIFY2(failures.isEmpty(), qPrintable(failures.join(QStringLiteral("; "))));
    }

    /// The two page verbs reach ScrollEngine::scrollViewByPercent through
    /// ShortcutManager::scrollViewRequested with a literal whole viewport of
    /// travel, back being negative: the same sign convention the wheel's
    /// D-Bus path uses (delta × step), so the keyboard and the wheel agree on
    /// which way "back" is. Pinned through the backend, not by calling the
    /// handler, so the table row's wiring is what is tested.
    void pageVerbsFireAWholeViewportWithTheWheelsPolarity()
    {
        IsolatedConfigGuard guard;
        Settings settings(nullptr);
        ShortcutManager manager(&settings);
        auto backend = std::make_unique<SilentBackend>();
        SilentBackend* silent = backend.get();
        manager.setBackendForTesting(std::move(backend));
        manager.registerShortcuts();

        QSignalSpy requested(&manager, &ShortcutManager::scrollViewRequested);
        silent->fire(QLatin1String(PlasmaZones::ShortcutIds::kIdScrollViewPageBack));
        QCOMPARE(requested.count(), 1);
        QCOMPARE(requested.last().at(0).toInt(), -100);
        silent->fire(QLatin1String(PlasmaZones::ShortcutIds::kIdScrollViewPageForward));
        QCOMPARE(requested.count(), 2);
        QCOMPARE(requested.last().at(0).toInt(), 100);
    }
};

QTEST_MAIN(TestScrollingShortcutParity)
#include "test_scrolling_shortcut_parity.moc"
