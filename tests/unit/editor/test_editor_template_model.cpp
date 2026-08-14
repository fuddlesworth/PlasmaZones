// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * EditorTemplateModel + UpdateTemplateCommand coverage.
 *
 * Drives the controller-free surface (snapshot/applyState/resetState and the
 * wire round trip) plus the undo command against a raw QUndoStack, pinning:
 *  - the snapshot/applyState round trip and per-field signal discipline
 *    (applyState diffs and emits only what changed; resetState emits all
 *    four data signals unconditionally, the load-path contract);
 *  - toWirePayload parsing back through ScrollingTemplate::fromJson with the
 *    values intact, i.e. the payload speaks the library's wire schema;
 *  - undo/redo round trips, gesture-scoped merging (same key merges, a
 *    different gesture token opens a new entry), and the obsolete collapse
 *    when a merge lands back on its origin state;
 *  - ScrollingTemplate::normalizePresetList's floor/sort/dedupe/cap contract.
 *
 * The model is constructed with a null controller: applyState guards its
 * markUnsaved call for exactly this use. The two link stubs at the bottom
 * satisfy relocations from pushCommand (never entered here — mutations go
 * through hand-built commands) and from applyState's guarded markUnsaved
 * call, which the null controller skips at runtime.
 */

#include <PhosphorZones/ScrollingTemplate.h>
#include <QSignalSpy>
#include <QUndoStack>
#include <QtTest>

#include "editor/EditorController.h"
#include "editor/EditorTemplateModel.h"
#include "editor/undo/commands/UpdateTemplateCommand.h"

using PlasmaZones::EditorTemplateModel;
using PlasmaZones::UpdateTemplateCommand;

namespace {

/// A minimal but non-trivial template covering every field the model owns.
PhosphorZones::ScrollingTemplate makeTemplate()
{
    PhosphorZones::ScrollingTemplate templ;
    templ.id = QUuid::createUuid();
    templ.name = QStringLiteral("Test Template");
    templ.description = QStringLiteral("desc");
    PhosphorZones::ScrollingTemplateColumn a;
    a.width = 0.25;
    a.display = 0;
    PhosphorZones::ScrollingTemplateColumn b;
    b.width = 0.5;
    b.display = 1;
    templ.columns = {a, b};
    templ.defaultColumnWidthKind = PhosphorZones::DefaultWidthKindProportion;
    templ.defaultColumnWidthValue = 0.4;
    templ.defaultColumnWidthPresetIndex = 1;
    templ.defaultColumnDisplay = 0;
    templ.presetColumnWidths = {1.0 / 3.0, 0.5, 2.0 / 3.0};
    templ.presetWindowHeights = {0.5};
    return templ;
}

} // namespace

class TestEditorTemplateModel : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void snapshotApplyRoundTrip();
    void applyStateEmitsOnlyChanges();
    void resetStateEmitsUnconditionally();
    void wirePayloadRoundTrip();
    void undoRedoRoundTrip();
    void gestureScopedMerging();
    void mergeCollapseToOriginDropsEntry();
    void normalizePresetListContract();
};

void TestEditorTemplateModel::snapshotApplyRoundTrip()
{
    EditorTemplateModel model(nullptr, nullptr);
    model.resetState(EditorTemplateModel::stateFromTemplate(makeTemplate()), /*isSystem*/ false);
    const QVariantMap snap = model.snapshot();

    EditorTemplateModel other(nullptr, nullptr);
    other.applyState(snap);
    QCOMPARE(other.snapshot(), snap);
    QCOMPARE(other.columns().size(), 2);
    QCOMPARE(other.description(), QStringLiteral("desc"));
    QCOMPARE(other.defaultWidthKind(), PhosphorZones::DefaultWidthKindProportion);
    QCOMPARE(other.presetWidths().size(), 3);
    QCOMPARE(other.presetHeights().size(), 1);
}

void TestEditorTemplateModel::applyStateEmitsOnlyChanges()
{
    EditorTemplateModel model(nullptr, nullptr);
    model.resetState(EditorTemplateModel::stateFromTemplate(makeTemplate()), false);

    QSignalSpy descSpy(&model, &EditorTemplateModel::descriptionChanged);
    QSignalSpy columnsSpy(&model, &EditorTemplateModel::columnsChanged);
    QSignalSpy defaultsSpy(&model, &EditorTemplateModel::defaultsChanged);
    QSignalSpy presetsSpy(&model, &EditorTemplateModel::presetsChanged);

    // Re-applying the identical state must be silent on every channel.
    model.applyState(model.snapshot());
    QCOMPARE(descSpy.count(), 0);
    QCOMPARE(columnsSpy.count(), 0);
    QCOMPARE(defaultsSpy.count(), 0);
    QCOMPARE(presetsSpy.count(), 0);

    // A description-only change touches exactly the description channel.
    QVariantMap changed = model.snapshot();
    changed[QLatin1String("description")] = QStringLiteral("new text");
    model.applyState(changed);
    QCOMPARE(descSpy.count(), 1);
    QCOMPARE(columnsSpy.count(), 0);
    QCOMPARE(defaultsSpy.count(), 0);
    QCOMPARE(presetsSpy.count(), 0);
}

void TestEditorTemplateModel::resetStateEmitsUnconditionally()
{
    EditorTemplateModel model(nullptr, nullptr);
    model.resetState(EditorTemplateModel::stateFromTemplate(makeTemplate()), false);

    QSignalSpy descSpy(&model, &EditorTemplateModel::descriptionChanged);
    QSignalSpy columnsSpy(&model, &EditorTemplateModel::columnsChanged);
    QSignalSpy defaultsSpy(&model, &EditorTemplateModel::defaultsChanged);
    QSignalSpy presetsSpy(&model, &EditorTemplateModel::presetsChanged);

    // Even an identical reload announces every channel — the canvas latches
    // its reload handling on the surrounding layoutIdChanged, and the panel
    // re-syncs from these.
    model.resetState(model.snapshot(), false);
    QCOMPARE(descSpy.count(), 1);
    QCOMPARE(columnsSpy.count(), 1);
    QCOMPARE(defaultsSpy.count(), 1);
    QCOMPARE(presetsSpy.count(), 1);
}

void TestEditorTemplateModel::wirePayloadRoundTrip()
{
    const PhosphorZones::ScrollingTemplate source = makeTemplate();
    EditorTemplateModel model(nullptr, nullptr);
    model.resetState(EditorTemplateModel::stateFromTemplate(source), false);

    const QString id = source.id.toString();
    const QJsonObject payload = model.toWirePayload(id, QStringLiteral("  Wire Name  "));
    const PhosphorZones::ScrollingTemplate parsed = PhosphorZones::ScrollingTemplate::fromJson(payload);

    QVERIFY(parsed.isValid());
    QCOMPARE(parsed.id, source.id);
    QCOMPARE(parsed.name, QStringLiteral("Wire Name")); // trimmed at build time
    QCOMPARE(parsed.description, source.description);
    QCOMPARE(parsed.columns.size(), source.columns.size());
    QCOMPARE(parsed.columns[1].display, 1);
    QCOMPARE(parsed.defaultColumnWidthKind, source.defaultColumnWidthKind);
    QCOMPARE(parsed.defaultColumnWidthValue, source.defaultColumnWidthValue);
    QCOMPARE(parsed.defaultColumnWidthPresetIndex, source.defaultColumnWidthPresetIndex);
    QCOMPARE(parsed.presetColumnWidths, source.presetColumnWidths);
    QCOMPARE(parsed.presetWindowHeights, source.presetWindowHeights);
}

void TestEditorTemplateModel::undoRedoRoundTrip()
{
    EditorTemplateModel model(nullptr, nullptr);
    model.resetState(EditorTemplateModel::stateFromTemplate(makeTemplate()), false);

    const QVariantMap before = model.snapshot();
    QVariantMap after = before;
    after[QLatin1String("description")] = QStringLiteral("edited");

    QUndoStack stack;
    stack.push(new UpdateTemplateCommand(&model, before, after, QStringLiteral("edit")));
    QCOMPARE(model.description(), QStringLiteral("edited"));

    stack.undo();
    QCOMPARE(model.snapshot(), before);
    stack.redo();
    QCOMPARE(model.snapshot(), after);
}

void TestEditorTemplateModel::gestureScopedMerging()
{
    EditorTemplateModel model(nullptr, nullptr);
    model.resetState(EditorTemplateModel::stateFromTemplate(makeTemplate()), false);
    const QVariantMap origin = model.snapshot();

    auto withDescription = [&origin](const QString& text) {
        QVariantMap s = origin;
        s[QLatin1String("description")] = text;
        return s;
    };

    QUndoStack stack;
    const QString gestureA = QStringLiteral("field:1");
    stack.push(new UpdateTemplateCommand(&model, origin, withDescription(QStringLiteral("a")), QStringLiteral("edit"),
                                         gestureA));
    stack.push(new UpdateTemplateCommand(&model, withDescription(QStringLiteral("a")),
                                         withDescription(QStringLiteral("ab")), QStringLiteral("edit"), gestureA));
    // Same gesture token: one merged entry whose undo returns to the origin.
    QCOMPARE(stack.count(), 1);
    stack.undo();
    QCOMPARE(model.snapshot(), origin);
    stack.redo();

    // A different token opens a second entry instead of merging.
    const QString gestureB = QStringLiteral("field:2");
    stack.push(new UpdateTemplateCommand(&model, withDescription(QStringLiteral("ab")),
                                         withDescription(QStringLiteral("abc")), QStringLiteral("edit"), gestureB));
    QCOMPARE(stack.count(), 2);

    // An empty merge key does not join the previous gesture's entry.
    stack.push(new UpdateTemplateCommand(&model, withDescription(QStringLiteral("abc")),
                                         withDescription(QStringLiteral("abcd")), QStringLiteral("edit")));
    QCOMPARE(stack.count(), 3);
}

void TestEditorTemplateModel::mergeCollapseToOriginDropsEntry()
{
    EditorTemplateModel model(nullptr, nullptr);
    model.resetState(EditorTemplateModel::stateFromTemplate(makeTemplate()), false);
    const QVariantMap origin = model.snapshot();
    QVariantMap step = origin;
    step[QLatin1String("description")] = QStringLiteral("transient");

    QUndoStack stack;
    const QString gesture = QStringLiteral("field:9");
    stack.push(new UpdateTemplateCommand(&model, origin, step, QStringLiteral("edit"), gesture));
    QCOMPARE(stack.count(), 1);
    // The gesture returns exactly to its origin: the merge marks itself
    // obsolete and push() drops the entry rather than keeping a no-op step.
    stack.push(new UpdateTemplateCommand(&model, step, origin, QStringLiteral("edit"), gesture));
    QCOMPARE(stack.count(), 0);
    QCOMPARE(model.snapshot(), origin);
    QVERIFY(!stack.canUndo());
}

void TestEditorTemplateModel::normalizePresetListContract()
{
    // Floor drops sub-minimum entries, values clamp to 1.0, the list sorts
    // ascending, near-duplicates within the epsilon collapse, and the cap
    // keeps the smallest N distinct fractions.
    QList<qreal> raw{0.5, 0.01, 1.5, 0.5 + PhosphorZones::FractionDedupeEpsilon / 2.0, 0.25};
    const QList<qreal> out = PhosphorZones::ScrollingTemplate::normalizePresetList(raw);
    QCOMPARE(out, (QList<qreal>{0.25, 0.5, 1.0}));

    QList<qreal> overflow;
    for (int i = 0; i < PhosphorZones::MaxTemplateColumns + 4; ++i) {
        overflow.append(0.05 + i * 0.02);
    }
    QCOMPARE(PhosphorZones::ScrollingTemplate::normalizePresetList(overflow).size(), PhosphorZones::MaxTemplateColumns);
}

// Link stubs: EditorTemplateModel's mutation path (pushCommand) references
// these two EditorController members, so the linker needs them even though
// this test never enters that path — every mutation here goes through a
// hand-built UpdateTemplateCommand. Returning null / doing nothing matches
// the "no controller" construction the model documents for tests.
namespace PlasmaZones {
UndoController* EditorController::undoController() const
{
    return nullptr;
}
void EditorController::markUnsaved()
{
}
} // namespace PlasmaZones

QTEST_MAIN(TestEditorTemplateModel)
#include "test_editor_template_model.moc"
