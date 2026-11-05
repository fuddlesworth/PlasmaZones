// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_editor_page_controller.cpp
 * @brief The Editor settings page's property surface over Settings.
 *
 * EditorPageController is how the Editor page reaches the Editor.* settings,
 * which have no Q_PROPERTY on Settings. These cases pin what the page relies
 * on: a change made under it (a reload, another page) reaches its NOTIFY and
 * the generic changed() that drives dirty tracking; the slider bounds are the
 * schema's; and the Keyboard Shortcuts card's reset touches only shortcuts.
 */

#include <QSignalSpy>
#include <QTest>

#include "config/configdefaults.h"
#include "config/settings.h"
#include "helpers/IsolatedConfigGuard.h"
#include "settings/pages/editorpagecontroller.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

class TestEditorPageController : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    /// A Settings-side change reaches the page's NOTIFY and changed().
    void settingsChange_reachesPageSignals()
    {
        IsolatedConfigGuard guard;
        Settings settings;
        EditorPageController page(settings);
        QSignalSpy opacitySpy(&page, &EditorPageController::backgroundOpacityChanged);
        QSignalSpy changedSpy(&page, &EditorPageController::changed);

        settings.setEditorBackgroundOpacity(0.4);

        QCOMPARE(page.backgroundOpacity(), 0.4);
        QCOMPARE(opacitySpy.count(), 1);
        QCOMPARE(changedSpy.count(), 1);
    }

    /// The page writes through to Settings, clamped by the schema.
    void setter_writesThroughClamped()
    {
        IsolatedConfigGuard guard;
        Settings settings;
        EditorPageController page(settings);
        page.setBackgroundOpacity(1.7);
        QCOMPARE(settings.editorBackgroundOpacity(), ConfigDefaults::editorBackgroundOpacityMax());
        QCOMPARE(page.backgroundOpacity(), ConfigDefaults::editorBackgroundOpacityMax());
    }

    /// The slider bounds the page binds to are the ranges the schema actually
    /// clamps to: an out-of-range write through Settings lands on the bound
    /// the page reports, at both ends of both ranges.
    void bounds_matchSchemaClamp()
    {
        IsolatedConfigGuard guard;
        Settings settings;
        EditorPageController page(settings);
        settings.setEditorBackgroundOpacity(page.backgroundOpacityMax() + 1.0);
        QCOMPARE(settings.editorBackgroundOpacity(), page.backgroundOpacityMax());
        settings.setEditorBackgroundOpacity(page.backgroundOpacityMin() - 1.0);
        QCOMPARE(settings.editorBackgroundOpacity(), page.backgroundOpacityMin());
        settings.setEditorSnapIntervalX(page.snapIntervalMax() + 1.0);
        QCOMPARE(settings.editorSnapIntervalX(), page.snapIntervalMax());
        settings.setEditorSnapIntervalX(page.snapIntervalMin() / 2.0);
        QCOMPARE(settings.editorSnapIntervalX(), page.snapIntervalMin());
        settings.setEditorSnapIntervalY(page.snapIntervalMax() + 1.0);
        QCOMPARE(settings.editorSnapIntervalY(), page.snapIntervalMax());
        settings.setEditorSnapIntervalY(page.snapIntervalMin() / 2.0);
        QCOMPARE(settings.editorSnapIntervalY(), page.snapIntervalMin());
    }

    /// "Reset shortcuts" restores the four shortcuts and nothing else. It used
    /// to reset every editor setting, opacity included, from a row that says
    /// it restores shortcuts.
    void resetShortcutDefaults_touchesOnlyShortcuts()
    {
        IsolatedConfigGuard guard;
        Settings settings;
        EditorPageController page(settings);
        page.setDuplicateShortcut(QStringLiteral("Ctrl+Alt+1"));
        page.setFillShortcut(QStringLiteral("Ctrl+Alt+2"));
        page.setBackgroundOpacity(0.3);
        page.setGridSnappingEnabled(!ConfigDefaults::editorGridSnappingEnabled());

        page.resetShortcutDefaults();

        QCOMPARE(page.duplicateShortcut(), ConfigDefaults::editorDuplicateShortcut());
        QCOMPARE(page.fillShortcut(), ConfigDefaults::editorFillShortcut());
        QCOMPARE(page.backgroundOpacity(), 0.3);
        QCOMPARE(page.gridSnappingEnabled(), !ConfigDefaults::editorGridSnappingEnabled());
    }
};

QTEST_MAIN(TestEditorPageController)
#include "test_editor_page_controller.moc"
