// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_editor_owned_config.cpp
 * @brief The layout editor's own read and write of the Editor.* settings.
 *
 * The editor has no Settings object; it reads and writes config.json through
 * EditorOwnedConfig. These cases pin the two properties that matter to the
 * rest of the system: the editor reads a file exactly as the daemon and the
 * settings app do (same defaults, same clamps, same modifier masks), and it
 * writes only the settings it was told to, so a change another process made
 * to any other Editor.* key survives the editor's save.
 */

#include <QTest>

#include <PhosphorConfig/IBackend.h>
#include <PhosphorConfig/Schema.h>
#include <PhosphorConfig/Store.h>

#include <QFile>

#include "config/configbackends.h"
#include "config/configdefaults.h"
#include "config/settings.h"
#include "config/settingsschema.h"
#include "editor/helpers/EditorOwnedConfig.h"
#include "helpers/IsolatedConfigGuard.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;
using Key = EditorOwnedConfig::Key;

class TestEditorOwnedConfig : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    /// A fresh config reads as the ConfigDefaults values.
    void read_absentKeysAreDefaults()
    {
        IsolatedConfigGuard guard;
        auto backend = createDefaultConfigBackend();
        const EditorOwnedConfig c = EditorOwnedConfig::read(*backend);
        QCOMPARE(c.gridSnappingEnabled, ConfigDefaults::editorGridSnappingEnabled());
        QCOMPARE(c.edgeSnappingEnabled, ConfigDefaults::editorEdgeSnappingEnabled());
        QCOMPARE(c.snapIntervalX, ConfigDefaults::editorSnapIntervalX());
        QCOMPARE(c.snapIntervalY, ConfigDefaults::editorSnapIntervalY());
        QCOMPARE(c.snapOverrideModifier, ConfigDefaults::editorSnapOverrideModifier());
        QCOMPARE(c.duplicateShortcut, ConfigDefaults::editorDuplicateShortcut());
        QCOMPARE(c.splitHorizontalShortcut, ConfigDefaults::editorSplitHorizontalShortcut());
        QCOMPARE(c.splitVerticalShortcut, ConfigDefaults::editorSplitVerticalShortcut());
        QCOMPARE(c.fillShortcut, ConfigDefaults::editorFillShortcut());
        QCOMPARE(c.fillOnDropEnabled, ConfigDefaults::fillOnDropEnabled());
        QCOMPARE(c.fillOnDropModifier, ConfigDefaults::fillOnDropModifier());
        QCOMPARE(c.backgroundOpacity, ConfigDefaults::editorBackgroundOpacity());
    }

    void read_clampsOpacity_data()
    {
        // A JSON number for the ordinary rows, as a hand edit writes it. The
        // nan row is JSON text: a NaN double cannot be stored in JSON at all,
        // so a hand edit is the only way one reaches the file, as "nan".
        QTest::addColumn<QVariant>("stored");
        QTest::addColumn<double>("expected");
        QTest::newRow("above") << QVariant(1.5) << ConfigDefaults::editorBackgroundOpacityMax();
        QTest::newRow("below") << QVariant(-0.5) << ConfigDefaults::editorBackgroundOpacityMin();
        QTest::newRow("nan") << QVariant(QStringLiteral("nan")) << ConfigDefaults::editorBackgroundOpacityMin();
        QTest::newRow("inside") << QVariant(0.4) << 0.4;
    }

    /// A hand-edited opacity reads back clamped the way the schema clamps
    /// it, so the editor's veil and the settings page agree on the value.
    void read_clampsOpacity()
    {
        QFETCH(QVariant, stored);
        QFETCH(double, expected);
        IsolatedConfigGuard guard;
        {
            auto backend = createDefaultConfigBackend();
            auto group = backend->group(ConfigDefaults::editorBackgroundGroup());
            if (stored.typeId() == QMetaType::QString) {
                group->writeString(ConfigDefaults::opacityKey(), stored.toString());
            } else {
                group->writeDouble(ConfigDefaults::opacityKey(), stored.toDouble());
            }
            group.reset();
            QVERIFY(backend->sync());
        }
        auto backend = createDefaultConfigBackend();
        QCOMPARE(EditorOwnedConfig::read(*backend).backgroundOpacity, expected);
    }

    /// Every Editor.* key the schema declares is one the editor reads and
    /// writes. A key added to the schema and forgotten in EditorOwnedConfig
    /// would be invisible to the editor: never read, never written back.
    /// Each key gets a non-default value on disk, the editor reads the file
    /// and writes all of it into an empty one, and the two must agree.
    void everySchemaEditorKey_roundTrips()
    {
        const PhosphorConfig::Schema& schema = cachedSettingsSchema();
        const QString editorPrefix = ConfigDefaults::editorGroup() + QLatin1Char('.');
        IsolatedConfigGuard guard;

        QHash<QString, QVariant> written;
        {
            auto backend = createDefaultConfigBackend();
            PhosphorConfig::Store store(backend.get(), schema);
            for (auto it = schema.groups.constBegin(); it != schema.groups.constEnd(); ++it) {
                if (!it.key().startsWith(editorPrefix)) {
                    continue;
                }
                for (const PhosphorConfig::KeyDef& def : it.value()) {
                    QVariantList candidates;
                    switch (def.expectedType) {
                    case QMetaType::Bool:
                        candidates = {!def.defaultValue.toBool()};
                        break;
                    case QMetaType::Int:
                        candidates = {int(Qt::AltModifier), int(Qt::MetaModifier)};
                        break;
                    case QMetaType::Double:
                        candidates = {0.25, 0.05};
                        break;
                    case QMetaType::QString:
                        candidates = {QStringLiteral("Ctrl+Alt+Shift+F12")};
                        break;
                    default:
                        QFAIL(qPrintable(it.key() + QLatin1Char('/') + def.key + QStringLiteral(": extend this test")));
                    }
                    for (const QVariant& c : std::as_const(candidates)) {
                        store.write(it.key(), def.key, c);
                        if (store.readVariant(it.key(), def.key) != def.defaultValue) {
                            break;
                        }
                    }
                    const QVariant value = store.readVariant(it.key(), def.key);
                    QVERIFY2(value != def.defaultValue, qPrintable(def.key));
                    written.insert(it.key() + QLatin1Char('/') + def.key, value);
                }
            }
            QVERIFY(backend->sync());
        }
        QVERIFY(!written.isEmpty());

        auto source = createDefaultConfigBackend();
        const EditorOwnedConfig config = EditorOwnedConfig::read(*source);

        // Write everything the editor holds into an empty file and read it
        // back through the schema.
        QVERIFY(QFile::remove(ConfigDefaults::configFilePath()));
        {
            auto target = createDefaultConfigBackend();
            config.write(*target, EditorOwnedConfig::allKeys());
            QVERIFY(target->sync());
        }
        auto reread = createDefaultConfigBackend();
        const PhosphorConfig::Store check(reread.get(), schema);
        QStringList missed;
        for (auto it = written.constBegin(); it != written.constEnd(); ++it) {
            const QString group = it.key().section(QLatin1Char('/'), 0, 0);
            const QString key = it.key().section(QLatin1Char('/'), 1);
            if (check.readVariant(group, key) != it.value()) {
                missed.append(it.key());
            }
        }
        QVERIFY2(missed.isEmpty(), qPrintable(missed.join(QStringLiteral(", "))));
    }

    /// The modifier validator rejects bits outside Shift/Ctrl/Alt/Meta, as the
    /// schema does for the daemon. The editor used to read the raw integer.
    void read_rejectsInvalidModifier()
    {
        IsolatedConfigGuard guard;
        {
            auto backend = createDefaultConfigBackend();
            backend->group(ConfigDefaults::editorSnappingGroup())
                ->writeInt(ConfigDefaults::overrideModifierKey(), int(Qt::KeypadModifier));
            QVERIFY(backend->sync());
        }
        auto backend = createDefaultConfigBackend();
        QCOMPARE(EditorOwnedConfig::read(*backend).snapOverrideModifier, ConfigDefaults::editorSnapOverrideModifier());
    }

    /// An empty or unparseable shortcut falls back to its default and is
    /// reported, so the caller can warn about it once.
    void read_invalidShortcutFallsBack()
    {
        IsolatedConfigGuard guard;
        {
            auto backend = createDefaultConfigBackend();
            auto shortcuts = backend->group(ConfigDefaults::editorShortcutsGroup());
            shortcuts->writeString(ConfigDefaults::duplicateKey(), QStringLiteral("NotAKey+Nonsense"));
            shortcuts->writeString(ConfigDefaults::fillKey(), QString());
            shortcuts->writeString(ConfigDefaults::splitVerticalKey(), QStringLiteral("Ctrl+Alt+Y"));
            shortcuts.reset();
            QVERIFY(backend->sync());
        }
        auto backend = createDefaultConfigBackend();
        QStringList invalid;
        const EditorOwnedConfig c = EditorOwnedConfig::read(*backend, &invalid);
        QCOMPARE(c.duplicateShortcut, ConfigDefaults::editorDuplicateShortcut());
        QCOMPARE(c.fillShortcut, ConfigDefaults::editorFillShortcut());
        QCOMPARE(c.splitVerticalShortcut, QStringLiteral("Ctrl+Alt+Y"));
        invalid.sort();
        QStringList expected{ConfigDefaults::duplicateKey(), ConfigDefaults::fillKey()};
        expected.sort();
        QCOMPARE(invalid, expected);
    }

    /// The editor writes only the keys it changed. Another process's change
    /// to a different Editor.* key, made after the editor last read the
    /// file, survives the editor's write.
    void write_onlyTheNamedKeys()
    {
        IsolatedConfigGuard guard;
        auto editorView = createDefaultConfigBackend();
        EditorOwnedConfig editorState = EditorOwnedConfig::read(*editorView);

        // The settings app changes the duplicate shortcut on disk.
        {
            Settings settingsApp;
            settingsApp.setEditorDuplicateShortcut(QStringLiteral("Ctrl+Alt+D"));
            QVERIFY(settingsApp.save());
        }

        // The editor, still holding the old shortcut, writes its opacity.
        editorState.backgroundOpacity = 0.3;
        auto writer = createDefaultConfigBackend();
        editorState.write(*writer, Key::BackgroundOpacity);
        QVERIFY(writer->sync());

        Settings reread;
        QCOMPARE(reread.editorBackgroundOpacity(), 0.3);
        QCOMPARE(reread.editorDuplicateShortcut(), QStringLiteral("Ctrl+Alt+D"));
    }

    /// A value equal to its default is written as absence, the way the
    /// settings app's store writes it, so the editor never freezes a default
    /// into the file.
    void write_defaultIsStoredAsAbsence()
    {
        IsolatedConfigGuard guard;
        auto backend = createDefaultConfigBackend();
        EditorOwnedConfig c = EditorOwnedConfig::read(*backend);
        c.backgroundOpacity = 0.3;
        c.write(*backend, Key::BackgroundOpacity);
        QVERIFY(backend->group(ConfigDefaults::editorBackgroundGroup())->hasKey(ConfigDefaults::opacityKey()));
        c.backgroundOpacity = ConfigDefaults::editorBackgroundOpacity();
        c.write(*backend, Key::BackgroundOpacity);
        QVERIFY(!backend->group(ConfigDefaults::editorBackgroundGroup())->hasKey(ConfigDefaults::opacityKey()));
    }

    /// Every field survives a write of all keys and a read back.
    void writeRead_roundTripsEveryField()
    {
        IsolatedConfigGuard guard;
        EditorOwnedConfig out;
        out.gridSnappingEnabled = !ConfigDefaults::editorGridSnappingEnabled();
        out.edgeSnappingEnabled = !ConfigDefaults::editorEdgeSnappingEnabled();
        out.snapIntervalX = 0.25;
        out.snapIntervalY = 0.05;
        out.snapOverrideModifier = int(Qt::AltModifier);
        out.duplicateShortcut = QStringLiteral("Ctrl+Alt+1");
        out.splitHorizontalShortcut = QStringLiteral("Ctrl+Alt+2");
        out.splitVerticalShortcut = QStringLiteral("Ctrl+Alt+3");
        out.fillShortcut = QStringLiteral("Ctrl+Alt+4");
        out.fillOnDropEnabled = !ConfigDefaults::fillOnDropEnabled();
        out.fillOnDropModifier = int(Qt::MetaModifier);
        out.backgroundOpacity = 0.45;
        const EditorOwnedConfig::Keys all = EditorOwnedConfig::allKeys();
        {
            auto backend = createDefaultConfigBackend();
            out.write(*backend, all);
            QVERIFY(backend->sync());
        }
        auto backend = createDefaultConfigBackend();
        const EditorOwnedConfig in = EditorOwnedConfig::read(*backend);
        QCOMPARE(in.gridSnappingEnabled, out.gridSnappingEnabled);
        QCOMPARE(in.edgeSnappingEnabled, out.edgeSnappingEnabled);
        QCOMPARE(in.snapIntervalX, out.snapIntervalX);
        QCOMPARE(in.snapIntervalY, out.snapIntervalY);
        QCOMPARE(in.snapOverrideModifier, out.snapOverrideModifier);
        QCOMPARE(in.duplicateShortcut, out.duplicateShortcut);
        QCOMPARE(in.splitHorizontalShortcut, out.splitHorizontalShortcut);
        QCOMPARE(in.splitVerticalShortcut, out.splitVerticalShortcut);
        QCOMPARE(in.fillShortcut, out.fillShortcut);
        QCOMPARE(in.fillOnDropEnabled, out.fillOnDropEnabled);
        QCOMPARE(in.fillOnDropModifier, out.fillOnDropModifier);
        QCOMPARE(in.backgroundOpacity, out.backgroundOpacity);
    }
};

QTEST_MAIN(TestEditorOwnedConfig)
#include "test_editor_owned_config.moc"
