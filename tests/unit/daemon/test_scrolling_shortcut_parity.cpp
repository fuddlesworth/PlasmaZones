// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// The Shortcuts.Scrolling family is spelled out FOUR times by hand, and
// nothing used to compare the four:
//
//   1. the config schema group Shortcuts.Scrolling (settingsschema_scrolling.cpp)
//   2. ShortcutManager's registration table          (shortcutmanager.cpp)
//   3. the cheatsheet catalog                        (shortcutmanager_catalog.cpp)
//   4. the D-Bus settings registry                   (settingsadaptor_registry.cpp)
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

    /// Pins the layout-capability tag split the daemon's engineProvidesLayouts
    /// gates rely on: the four layout-selection ids plus the quick-layout
    /// digit family carry mode "layouts" (hidden on a non-layout screen),
    /// while the two engine-routed layout-group rows stay "all" (Reapply
    /// routes through reapplyLayout and Arrange All through snapAllWindows,
    /// which every engine implements). A silent retag in either direction
    /// would make the sheet advertise a refusing key or hide a working one.
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

        QHash<QString, QString> modeById;
        const QVariantList rows = manager.cheatsheetModel();
        for (const QVariant& v : rows) {
            const QVariantMap row = v.toMap();
            modeById.insert(row.value(QStringLiteral("id")).toString(), row.value(QStringLiteral("mode")).toString());
        }
        // The quick-layout family compresses into one row keyed by its first
        // member, so quick_layout_1 stands in for the whole digit family.
        for (const QString& id : expectLayouts) {
            QVERIFY2(modeById.contains(id), qPrintable(QStringLiteral("No cheatsheet row for %1").arg(id)));
            QCOMPARE(modeById.value(id), QStringLiteral("layouts"));
        }
        for (const QString& id : expectAll) {
            QVERIFY2(modeById.contains(id), qPrintable(QStringLiteral("No cheatsheet row for %1").arg(id)));
            QCOMPARE(modeById.value(id), QStringLiteral("all"));
        }
    }
};

QTEST_MAIN(TestScrollingShortcutParity)
#include "test_scrolling_shortcut_parity.moc"
