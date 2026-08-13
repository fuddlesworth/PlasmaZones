// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// The TEMPLATE channel's seed blueprint, and the consumption cursor that
// tracks it.
//
// Split out of the per-screen suite, which had grown past the file-size
// ceiling once the cursor arrived. The two groups ask different questions:
// the per-screen suite asks which of the four channels wins a default, while
// this one asks which blueprint ENTRY a materializing column takes and when
// that entry stops being available.
//
// The blueprint is a SEED, not a standing rule. Entry `i` describes the i-th
// column a strip grows, and once a column has taken it the entry is spent —
// deriving the index from the live column count instead refilled every gap,
// so closing a column handed its prescription straight back to the next open
// and a column the user had toggled to Normal came back Tabbed.
//
// Spent-ness is therefore state, and the cases below are mostly about
// keeping it across events that are NOT "the user cleared this screen out":
// a desktop switch, a template re-push, a mode round trip, and a strip that
// only transiently resolves to nothing (all columns minimized, the last
// window floated, the last window picked up by a drag). Each of those used
// to zero the cursor and hand spent entries out a second time.

#include <PhosphorScrollEngine/IScrollSettings.h>
#include <PhosphorScrollEngine/ScrollEngine.h>
#include <PhosphorScrollEngine/ScrollState.h>
#include <PhosphorScrollEngine/ScrollStrip.h>
#include <PhosphorScrollEngine/ScrollTypes.h>

#include "scrollstriptestutils.h"
#include "scrollstubsettings.h"

#include <QVariantMap>
#include <QtTest>

using namespace PhosphorScrollEngine;

using ScrollTestUtils::makeProviderEngine;

namespace {

using ScrollTestUtils::StubScrollSettings;

const QString kS1 = QStringLiteral("S1");
const QString kS2 = QStringLiteral("S2");
const QString kS3 = QStringLiteral("S3");

/// A two-entry blueprint (0.6 then 0.4, both Tabbed) over a 0.3 Normal
/// beyond-blueprint default. Shared by every cursor case so that "took an
/// entry" and "fell past the blueprint" stay one comparison apart: 0.6/0.4
/// mean entry 0/1, and 0.3 means the blueprint had nothing left to give.
QVariantMap twoEntryTemplate()
{
    QVariantMap templ;
    QVariantList blueprint;
    for (qreal width : {0.6, 0.4}) {
        QVariantMap entry;
        entry.insert(ScrollPerScreenKeys::templateColumnWidth(), width);
        entry.insert(ScrollPerScreenKeys::templateColumnDisplay(), static_cast<int>(ColumnDisplay::Tabbed));
        blueprint.append(entry);
    }
    templ.insert(ScrollPerScreenKeys::templateColumns(), blueprint);
    templ.insert(ScrollPerScreenKeys::defaultColumnWidthKind(), static_cast<int>(DefaultWidthKind::Proportion));
    templ.insert(ScrollPerScreenKeys::defaultColumnWidthValue(), 0.3);
    templ.insert(ScrollPerScreenKeys::defaultColumnDisplay(), static_cast<int>(ColumnDisplay::Normal));
    return templ;
}

} // namespace

class TestScrollEngineTemplate : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void templateBlueprintSeedsFirstColumns();
    void openRuleOutranksTemplateBlueprint();
    void openMaximizedRuleOutranksWidthRuleAndBlueprint();
    void openFocusedRuleOverridesFocusNewWindows();
    void openFocusedFalseOnAnEmptyStripStillAdoptsTheArrival();
    void openFocusedFalseSurvivesTheCompositorsOwnFocusReport();
    void openMaximizedFalseLeavesTheDefaultWidth();
    void openMaximizedIsDroppedByAConsumeOpen();
    void templateBlueprintNeverResizesExistingColumns();
    void templateBlueprintEntryWithoutDisplayKeepsTheDefault();
    void closingAColumnDoesNotHandItsBlueprintEntryBack();
    void emptyingTheStripRestartsTheBlueprintSeed();
    void anewBlueprintRestartsTheSeedInsteadOfResumingTheOldCount();
    void reApplyingTheSameTemplateKeepsSpentEntriesSpent();
    void aScreenLeavingScrollingKeepsItsSpentEntriesSpent();
    void siblingContextsDoNotResetEachOthersCursor();
    void floatingTheLastWindowDoesNotRestartTheSeed();

private:
    /// A headless engine active on the three screens, with @p settings
    /// installed and its cached globals refreshed.
    static ScrollEngine* makeEngine(QObject* parent, StubScrollSettings* settings)
    {
        ScrollEngine* engine = makeProviderEngine(parent, {kS1, kS2, kS3});
        engine->setEngineSettings(settings);
        engine->refreshConfigFromSettings();
        return engine;
    }

    /// The width intent of the column @p windowId opened on @p screenId, or a
    /// default-constructed ColumnWidth when the strip has no such column.
    /// Callers pair every read with a column-count or found-flag check — a
    /// missing column would otherwise satisfy a Proportion assertion
    /// vacuously.
    static ColumnWidth openedWidth(ScrollEngine* engine, const QString& screenId, const QString& windowId)
    {
        auto* state = static_cast<ScrollState*>(engine->stateForScreen(screenId));
        if (!state) {
            return {};
        }
        for (const Column& col : state->strip().columns()) {
            if (col.indexOfWindow(windowId) >= 0) {
                return col.width;
            }
        }
        return {};
    }

    static bool columnExists(ScrollEngine* engine, const QString& screenId, const QString& windowId)
    {
        auto* state = static_cast<ScrollState*>(engine->stateForScreen(screenId));
        if (!state) {
            return false;
        }
        for (const Column& col : state->strip().columns()) {
            if (col.indexOfWindow(windowId) >= 0) {
                return true;
            }
        }
        return false;
    }
};

void TestScrollEngineTemplate::templateBlueprintSeedsFirstColumns()
{
    // The template's blueprint shapes the first N materializing columns
    // (width AND display), and the column beyond it takes the pushed
    // beyond-blueprint default.
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    ScrollEngine* engine = makeEngine(&owner, settings);

    QVariantMap templ;
    QVariantList blueprint;
    QVariantMap first;
    first.insert(ScrollPerScreenKeys::templateColumnWidth(), 0.6);
    blueprint.append(first);
    QVariantMap second;
    second.insert(ScrollPerScreenKeys::templateColumnWidth(), 0.4);
    second.insert(ScrollPerScreenKeys::templateColumnDisplay(), 1);
    blueprint.append(second);
    templ.insert(ScrollPerScreenKeys::templateColumns(), blueprint);
    templ.insert(ScrollPerScreenKeys::defaultColumnWidthKind(), static_cast<int>(DefaultWidthKind::Proportion));
    templ.insert(ScrollPerScreenKeys::defaultColumnWidthValue(), 0.3);
    engine->applyPerScreenConfig(kS1, templ);

    engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), kS1, 0, 0);
    engine->windowOpened(QStringLiteral("app|c"), kS1, 0, 0);

    auto* state = static_cast<ScrollState*>(engine->stateForScreen(kS1));
    QVERIFY(state);
    QCOMPARE(state->strip().columns().size(), 3);
    QCOMPARE(openedWidth(engine, kS1, QStringLiteral("app|a")).kind, ColumnWidth::Proportion);
    QCOMPARE(openedWidth(engine, kS1, QStringLiteral("app|a")).proportion, 0.6);
    QCOMPARE(openedWidth(engine, kS1, QStringLiteral("app|b")).proportion, 0.4);
    // The second blueprint column opened tabbed; the strip stores display
    // per column, so find app|b's column. The found flag keeps the assertion
    // honest: without it a missing app|b column would satisfy the loop
    // vacuously, which is the same discipline columnExists enforces for the
    // width reads above.
    bool foundB = false;
    for (const Column& col : state->strip().columns()) {
        if (col.indexOfWindow(QStringLiteral("app|b")) >= 0) {
            foundB = true;
            QCOMPARE(col.display, ColumnDisplay::Tabbed);
        }
    }
    QVERIFY(foundB);
    // Beyond the blueprint: the template's declared default.
    QCOMPARE(openedWidth(engine, kS1, QStringLiteral("app|c")).proportion, 0.3);
}

void TestScrollEngineTemplate::openRuleOutranksTemplateBlueprint()
{
    // A per-window open rule pins the width; the blueprint entry the column
    // would have taken must not override it.
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    ScrollEngine* engine = makeEngine(&owner, settings);

    QVariantMap templ;
    QVariantList blueprint;
    QVariantMap first;
    first.insert(ScrollPerScreenKeys::templateColumnWidth(), 0.6);
    blueprint.append(first);
    templ.insert(ScrollPerScreenKeys::templateColumns(), blueprint);
    engine->applyPerScreenConfig(kS1, templ);

    engine->setOpenParamsResolver([](const QString&, const QString&) {
        ScrollOpenParams params;
        params.widthFraction = 0.25;
        return params;
    });
    engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
    QCOMPARE(openedWidth(engine, kS1, QStringLiteral("app|a")).kind, ColumnWidth::Proportion);
    QCOMPARE(openedWidth(engine, kS1, QStringLiteral("app|a")).proportion, 0.25);
}

void TestScrollEngineTemplate::openMaximizedRuleOutranksWidthRuleAndBlueprint()
{
    // openMaximized is the stronger width verdict: it wins over a width rule
    // on the same window AND over the blueprint entry the column would have
    // taken.
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    ScrollEngine* engine = makeEngine(&owner, settings);

    QVariantMap templ;
    QVariantList blueprint;
    QVariantMap first;
    first.insert(ScrollPerScreenKeys::templateColumnWidth(), 0.6);
    blueprint.append(first);
    templ.insert(ScrollPerScreenKeys::templateColumns(), blueprint);
    engine->applyPerScreenConfig(kS1, templ);

    engine->setOpenParamsResolver([](const QString&, const QString&) {
        ScrollOpenParams params;
        params.widthFraction = 0.25;
        params.maximized = true;
        return params;
    });
    engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
    QCOMPARE(openedWidth(engine, kS1, QStringLiteral("app|a")).kind, ColumnWidth::Proportion);
    QCOMPARE(openedWidth(engine, kS1, QStringLiteral("app|a")).proportion, 1.0);
}

void TestScrollEngineTemplate::openFocusedRuleOverridesFocusNewWindows()
{
    // Both polarities layer over the global setting: focused=false withholds
    // strip adoption under a focus-new-windows ON global, focused=true forces
    // it under an OFF one.
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    ScrollEngine* engine = makeEngine(&owner, settings);

    engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
    auto* state = static_cast<ScrollState*>(engine->stateForScreen(kS1));
    QVERIFY(state);
    QCOMPARE(state->strip().activeWindowId(), QStringLiteral("app|a"));

    // Global ON, rule false: the strip keeps the prior active column.
    engine->setOpenParamsResolver([](const QString&, const QString&) {
        ScrollOpenParams params;
        params.focused = false;
        return params;
    });
    engine->windowOpened(QStringLiteral("app|b"), kS1, 0, 0);
    QCOMPARE(state->strip().activeWindowId(), QStringLiteral("app|a"));

    // Global OFF, rule true: the arrival is adopted as the active column.
    // No refreshConfigFromSettings after this write, DELIBERATELY:
    // effectiveFocusNewWindows reads the settings object live per call
    // (engine_overrides.cpp — unlike the cached stripAxis, which the global
    // suite must refresh). If this assignment ever stops taking effect, the
    // engine started caching the read and this test is what catches it.
    settings->focusNewWindows = false;
    engine->setOpenParamsResolver([](const QString&, const QString&) {
        ScrollOpenParams params;
        params.focused = true;
        return params;
    });
    engine->windowOpened(QStringLiteral("app|c"), kS1, 0, 0);
    QCOMPARE(state->strip().activeWindowId(), QStringLiteral("app|c"));

    // Global OFF, no rule: the setting stays authoritative.
    engine->setOpenParamsResolver({});
    engine->windowOpened(QStringLiteral("app|d"), kS1, 0, 0);
    QCOMPARE(state->strip().activeWindowId(), QStringLiteral("app|c"));
}

void TestScrollEngineTemplate::openFocusedFalseSurvivesTheCompositorsOwnFocusReport()
{
    // The regression this pins was observed live, not theorised: declining
    // focus rewound the STRIP, but the compositor had already focused the
    // arriving window on its own and reported that focus back independently.
    // The report adopted the arrival and undid the rewind, so the rule read as
    // a no-op to the user. Driving windowOpened alone cannot catch that — the
    // report has to be delivered, which is what this test adds over its
    // sibling above.
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    ScrollEngine* engine = makeEngine(&owner, settings);

    engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
    auto* state = static_cast<ScrollState*>(engine->stateForScreen(kS1));
    QVERIFY(state);
    QCOMPARE(state->strip().activeWindowId(), QStringLiteral("app|a"));

    engine->setOpenParamsResolver([](const QString&, const QString&) {
        ScrollOpenParams params;
        params.focused = false;
        return params;
    });
    engine->windowOpened(QStringLiteral("app|b"), kS1, 0, 0);
    QCOMPARE(state->strip().activeWindowId(), QStringLiteral("app|a"));

    // The compositor's own report for the declined arrival. Consumed once, so
    // the rewind stands.
    engine->windowFocused(QStringLiteral("app|b"), kS1);
    QCOMPARE(state->strip().activeWindowId(), QStringLiteral("app|a"));

    // Consumed ONCE and no more: a later report for the same window is a real
    // user click and must adopt normally. A standing veto would make the
    // window unfocusable, which is why the mark is one-shot.
    engine->windowFocused(QStringLiteral("app|b"), kS1);
    QCOMPARE(state->strip().activeWindowId(), QStringLiteral("app|b"));
}

void TestScrollEngineTemplate::openFocusedFalseOnAnEmptyStripStillAdoptsTheArrival()
{
    // The rewind arm needs a prior active column to rewind TO. On an empty
    // strip there is none, so the first window becomes the active column
    // whatever the rule says — a strip whose only column were not active
    // would leave every later direction verb navigating from nowhere. The
    // rule still governs the SECOND arrival, which is what makes this a
    // guard on the empty case rather than the rule being inert.
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    ScrollEngine* engine = makeEngine(&owner, settings);
    engine->setOpenParamsResolver([](const QString&, const QString&) {
        ScrollOpenParams params;
        params.focused = false;
        return params;
    });

    engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
    auto* state = static_cast<ScrollState*>(engine->stateForScreen(kS1));
    QVERIFY(state);
    QCOMPARE(state->strip().activeWindowId(), QStringLiteral("app|a"));

    engine->windowOpened(QStringLiteral("app|b"), kS1, 0, 0);
    QCOMPARE(state->strip().activeWindowId(), QStringLiteral("app|a"));
}

void TestScrollEngineTemplate::openMaximizedFalseLeavesTheDefaultWidth()
{
    // An EXPLICIT false must read exactly like an unset optional: the rule
    // says "do not maximize", not "maximize to the default". Without this
    // leg a resolver that treated the field's mere presence as a verdict
    // would pass the whole suite.
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    settings->widthKind = static_cast<int>(DefaultWidthKind::Proportion);
    settings->widthValue = 0.25;
    ScrollEngine* engine = makeEngine(&owner, settings);
    engine->setOpenParamsResolver([](const QString&, const QString&) {
        ScrollOpenParams params;
        params.maximized = false;
        return params;
    });

    engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
    QVERIFY(columnExists(engine, kS1, QStringLiteral("app|a")));
    const ColumnWidth width = openedWidth(engine, kS1, QStringLiteral("app|a"));
    QCOMPARE(width.kind, ColumnWidth::Proportion);
    QCOMPARE(width.proportion, 0.25);
}

void TestScrollEngineTemplate::openMaximizedIsDroppedByAConsumeOpen()
{
    // A consume open joins an existing column, and a joining tile carries NO
    // width verdict — resizing the host would resize every sibling in the
    // stack. So openMaximized (like openColumnWidth) reaches only a column
    // the open CREATES. The header documents that on ScrollOpenParams; this
    // pins it, because the drop is silent.
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    settings->widthKind = static_cast<int>(DefaultWidthKind::Proportion);
    settings->widthValue = 0.25;
    ScrollEngine* engine = makeEngine(&owner, settings);

    engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
    QCOMPARE(openedWidth(engine, kS1, QStringLiteral("app|a")).proportion, 0.25);

    engine->setOpenParamsResolver([](const QString&, const QString&) {
        ScrollOpenParams params;
        params.consume = true;
        params.maximized = true;
        return params;
    });
    engine->windowOpened(QStringLiteral("app|b"), kS1, 0, 0);

    auto* state = static_cast<ScrollState*>(engine->stateForScreen(kS1));
    QVERIFY(state);
    // One column holding both windows, still at the host's width.
    QCOMPARE(state->strip().columns().size(), 1);
    QVERIFY(state->strip().columns().first().indexOfWindow(QStringLiteral("app|b")) >= 0);
    QCOMPARE(state->strip().columns().first().width.kind, ColumnWidth::Proportion);
    QCOMPARE(state->strip().columns().first().width.proportion, 0.25);
}

void TestScrollEngineTemplate::templateBlueprintNeverResizesExistingColumns()
{
    // Applying a template reshapes nothing that already exists: existing
    // columns keep their widths, and only columns created AFTER the apply
    // consume blueprint entries (from the current column count onward).
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    settings->widthKind = static_cast<int>(DefaultWidthKind::Proportion);
    settings->widthValue = 0.5;
    ScrollEngine* engine = makeEngine(&owner, settings);

    engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
    QCOMPARE(openedWidth(engine, kS1, QStringLiteral("app|a")).proportion, 0.5);

    QVariantMap templ;
    QVariantList blueprint;
    for (qreal width : {0.7, 0.2}) {
        QVariantMap entry;
        entry.insert(ScrollPerScreenKeys::templateColumnWidth(), width);
        blueprint.append(entry);
    }
    templ.insert(ScrollPerScreenKeys::templateColumns(), blueprint);
    engine->applyPerScreenConfig(kS1, templ);
    // Drain the retile the apply scheduled: without it this leg would assert
    // against a strip the relayout has not touched yet, so a regression that
    // reshaped existing columns AT RELAYOUT would still read green here.
    QCoreApplication::processEvents();

    // The existing column is untouched.
    QCOMPARE(openedWidth(engine, kS1, QStringLiteral("app|a")).proportion, 0.5);
    // The next column materializes at index 1 and takes blueprint[1].
    engine->windowOpened(QStringLiteral("app|b"), kS1, 0, 0);
    QCOMPARE(openedWidth(engine, kS1, QStringLiteral("app|b")).proportion, 0.2);
}

void TestScrollEngineTemplate::templateBlueprintEntryWithoutDisplayKeepsTheDefault()
{
    // A blueprint entry may carry a width only. Its column must then keep the
    // EFFECTIVE default display rather than falling to Normal: reading the
    // absent key as 0 silently overrode a Tabbed default for exactly the
    // first N columns, which is the stretch a template is most likely to
    // shape. The in-tree daemon always writes both keys on every entry, so
    // this covers the public-API belt for embedder-supplied maps rather than
    // a shipped bug.
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    ScrollEngine* engine = makeEngine(&owner, settings);

    QVariantMap templ;
    templ.insert(ScrollPerScreenKeys::defaultColumnDisplay(), static_cast<int>(ColumnDisplay::Tabbed));
    QVariantList blueprint;
    QVariantMap widthOnly;
    widthOnly.insert(ScrollPerScreenKeys::templateColumnWidth(), 0.6);
    blueprint.append(widthOnly);
    templ.insert(ScrollPerScreenKeys::templateColumns(), blueprint);
    engine->applyPerScreenConfig(kS1, templ);

    engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);

    auto* state = static_cast<ScrollState*>(engine->stateForScreen(kS1));
    QVERIFY(state);
    QCOMPARE(state->strip().columns().size(), 1);
    // The entry's width still lands, so the blueprint really was consumed.
    QCOMPARE(openedWidth(engine, kS1, QStringLiteral("app|a")).proportion, 0.6);
    QCOMPARE(state->strip().columns().first().display, ColumnDisplay::Tabbed);

    // An entry that DOES carry a display still wins over the same default.
    QVariantMap explicitNormal;
    explicitNormal.insert(ScrollPerScreenKeys::defaultColumnDisplay(), static_cast<int>(ColumnDisplay::Tabbed));
    QVariantList twoEntries;
    twoEntries.append(widthOnly);
    QVariantMap normalEntry;
    normalEntry.insert(ScrollPerScreenKeys::templateColumnWidth(), 0.4);
    normalEntry.insert(ScrollPerScreenKeys::templateColumnDisplay(), static_cast<int>(ColumnDisplay::Normal));
    twoEntries.append(normalEntry);
    explicitNormal.insert(ScrollPerScreenKeys::templateColumns(), twoEntries);
    engine->applyPerScreenConfig(kS2, explicitNormal);

    engine->windowOpened(QStringLiteral("app|b"), kS2, 0, 0);
    engine->windowOpened(QStringLiteral("app|c"), kS2, 0, 0);

    auto* other = static_cast<ScrollState*>(engine->stateForScreen(kS2));
    QVERIFY(other);
    QCOMPARE(other->strip().columns().size(), 2);
    QCOMPARE(other->strip().columns().at(0).display, ColumnDisplay::Tabbed);
    QCOMPARE(other->strip().columns().at(1).display, ColumnDisplay::Normal);
}

void TestScrollEngineTemplate::closingAColumnDoesNotHandItsBlueprintEntryBack()
{
    // The blueprint is a SEED, not a standing rule: an entry a column already
    // took is spent, so closing that column must not prescribe the next open.
    // Deriving the entry from the live column count did exactly that — it
    // refilled any gap, so a column the user had toggled to Normal came back
    // Tabbed and the toggle read as broken.
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    ScrollEngine* engine = makeEngine(&owner, settings);

    QVariantMap templ;
    QVariantList blueprint;
    for (qreal width : {0.6, 0.4}) {
        QVariantMap entry;
        entry.insert(ScrollPerScreenKeys::templateColumnWidth(), width);
        entry.insert(ScrollPerScreenKeys::templateColumnDisplay(), static_cast<int>(ColumnDisplay::Tabbed));
        blueprint.append(entry);
    }
    templ.insert(ScrollPerScreenKeys::templateColumns(), blueprint);
    templ.insert(ScrollPerScreenKeys::defaultColumnWidthKind(), static_cast<int>(DefaultWidthKind::Proportion));
    templ.insert(ScrollPerScreenKeys::defaultColumnWidthValue(), 0.3);
    templ.insert(ScrollPerScreenKeys::defaultColumnDisplay(), static_cast<int>(ColumnDisplay::Normal));
    engine->applyPerScreenConfig(kS1, templ);

    engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), kS1, 0, 0);
    QCOMPARE(openedWidth(engine, kS1, QStringLiteral("app|b")).proportion, 0.4);

    // Close the second column. The strip is back to one column, but both
    // entries are spent.
    engine->windowClosed(QStringLiteral("app|b"));
    QCoreApplication::processEvents();

    auto* state = static_cast<ScrollState*>(engine->stateForScreen(kS1));
    QVERIFY(state);
    QCOMPARE(state->strip().columns().size(), 1);

    // The replacement takes the BEYOND-blueprint defaults, not entry 1 again.
    engine->windowOpened(QStringLiteral("app|c"), kS1, 0, 0);
    QCOMPARE(openedWidth(engine, kS1, QStringLiteral("app|c")).proportion, 0.3);
    bool foundC = false;
    for (const Column& col : state->strip().columns()) {
        if (col.indexOfWindow(QStringLiteral("app|c")) >= 0) {
            foundC = true;
            QCOMPARE(col.display, ColumnDisplay::Normal);
        }
    }
    QVERIFY(foundC);
}

void TestScrollEngineTemplate::emptyingTheStripRestartsTheBlueprintSeed()
{
    // The other half of the spent-entry contract: a screen cleared out has no
    // column standing for any entry, so the next window opens from the top of
    // the blueprint again. This is what makes the template describe the
    // STARTING shape of a screen rather than a one-time event in its history.
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    ScrollEngine* engine = makeEngine(&owner, settings);

    QVariantMap templ;
    QVariantList blueprint;
    QVariantMap first;
    first.insert(ScrollPerScreenKeys::templateColumnWidth(), 0.6);
    blueprint.append(first);
    templ.insert(ScrollPerScreenKeys::templateColumns(), blueprint);
    templ.insert(ScrollPerScreenKeys::defaultColumnWidthKind(), static_cast<int>(DefaultWidthKind::Proportion));
    templ.insert(ScrollPerScreenKeys::defaultColumnWidthValue(), 0.3);
    engine->applyPerScreenConfig(kS1, templ);

    engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
    QCOMPARE(openedWidth(engine, kS1, QStringLiteral("app|a")).proportion, 0.6);

    engine->windowClosed(QStringLiteral("app|a"));
    // Drained: the reset rides applyLayout's empty branch, which the close
    // only SCHEDULES. Without this the next open would still see the spent
    // cursor and the assertion below would pass for the wrong reason.
    QCoreApplication::processEvents();

    engine->windowOpened(QStringLiteral("app|b"), kS1, 0, 0);
    QCOMPARE(openedWidth(engine, kS1, QStringLiteral("app|b")).proportion, 0.6);
}

void TestScrollEngineTemplate::anewBlueprintRestartsTheSeedInsteadOfResumingTheOldCount()
{
    // Assigning a different template is an explicit act, so its blueprint
    // seeds from its own first entry rather than resuming a cursor that
    // counted the previous one's. The live column count still floors the
    // result — the new blueprint shapes what comes NEXT and never reaches
    // back to reshape the column already on screen.
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    ScrollEngine* engine = makeEngine(&owner, settings);

    QVariantMap oldTempl;
    QVariantList oldBlueprint;
    for (qreal width : {0.6, 0.4}) {
        QVariantMap entry;
        entry.insert(ScrollPerScreenKeys::templateColumnWidth(), width);
        oldBlueprint.append(entry);
    }
    oldTempl.insert(ScrollPerScreenKeys::templateColumns(), oldBlueprint);
    engine->applyPerScreenConfig(kS1, oldTempl);

    engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), kS1, 0, 0);
    engine->windowClosed(QStringLiteral("app|b"));
    QCoreApplication::processEvents();
    // One column left, both of the old blueprint's entries spent.

    QVariantMap newTempl;
    QVariantList newBlueprint;
    for (qreal width : {0.9, 0.8, 0.7}) {
        QVariantMap entry;
        entry.insert(ScrollPerScreenKeys::templateColumnWidth(), width);
        newBlueprint.append(entry);
    }
    newTempl.insert(ScrollPerScreenKeys::templateColumns(), newBlueprint);
    engine->applyPerScreenConfig(kS1, newTempl);
    QCoreApplication::processEvents();

    // Entry 1 of the NEW blueprint: the seed restarted (a resumed cursor
    // would have reached entry 2) and the one existing column floors it.
    engine->windowOpened(QStringLiteral("app|c"), kS1, 0, 0);
    QCOMPARE(openedWidth(engine, kS1, QStringLiteral("app|c")).proportion, 0.8);
}

void TestScrollEngineTemplate::reApplyingTheSameTemplateKeepsSpentEntriesSpent()
{
    // Re-pushing an UNCHANGED template is not a template change. The daemon
    // re-resolves and re-pushes on every context change, so treating the
    // write itself as a swap restarted the seed for events the user never
    // made. Invalidation compares the blueprint VALUE instead.
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    ScrollEngine* engine = makeEngine(&owner, settings);

    const QVariantMap templ = twoEntryTemplate();
    engine->applyPerScreenConfig(kS1, templ);

    engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), kS1, 0, 0);
    QCOMPARE(openedWidth(engine, kS1, QStringLiteral("app|b")).proportion, 0.4);

    engine->windowClosed(QStringLiteral("app|b"));
    QCoreApplication::processEvents();
    auto* state = static_cast<ScrollState*>(engine->stateForScreen(kS1));
    QVERIFY(state);
    QCOMPARE(state->strip().columns().size(), 1);

    // The same map again, as the daemon would push it.
    engine->applyPerScreenConfig(kS1, templ);

    // Both entries are still spent, so the replacement takes the
    // beyond-blueprint default rather than entry 1 a second time.
    engine->windowOpened(QStringLiteral("app|c"), kS1, 0, 0);
    QCOMPARE(openedWidth(engine, kS1, QStringLiteral("app|c")).proportion, 0.3);
}

void TestScrollEngineTemplate::aScreenLeavingScrollingKeepsItsSpentEntriesSpent()
{
    // A screen that leaves scrolling and comes back is the commonest way the
    // overrides are dropped and re-pushed — every desktop switch to a
    // non-scrolling desktop does it. The strip survives, so its columns still
    // stand for the entries they took; zeroing the cursor on the clear handed
    // those entries straight back out.
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    ScrollEngine* engine = makeEngine(&owner, settings);

    const QVariantMap templ = twoEntryTemplate();
    engine->applyPerScreenConfig(kS1, templ);

    engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), kS1, 0, 0);
    engine->windowClosed(QStringLiteral("app|b"));
    QCoreApplication::processEvents();

    auto* state = static_cast<ScrollState*>(engine->stateForScreen(kS1));
    QVERIFY(state);
    QCOMPARE(state->strip().columns().size(), 1);

    // Away and back, with the same template re-resolved on return.
    engine->clearPerScreenConfig(kS1);
    engine->applyPerScreenConfig(kS1, templ);

    engine->windowOpened(QStringLiteral("app|c"), kS1, 0, 0);
    QCOMPARE(openedWidth(engine, kS1, QStringLiteral("app|c")).proportion, 0.3);
}

void TestScrollEngineTemplate::siblingContextsDoNotResetEachOthersCursor()
{
    // Blueprints resolve per (screen, desktop, activity) but used to be
    // stored per screen, so two desktops on one monitor could not hold
    // different templates: the last resolve won for both and each switch
    // reset the other's cursor. The override map is keyed per context now,
    // and this pins that a sibling context's template leaves this one's
    // spent-ness alone.
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    ScrollEngine* engine = makeEngine(&owner, settings);

    engine->applyPerScreenConfig(kS1, twoEntryTemplate());
    engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), kS1, 0, 0);
    engine->windowClosed(QStringLiteral("app|b"));
    QCoreApplication::processEvents();

    // A different template arrives for the OTHER desktop, with a
    // beyond-blueprint default of its own so the two are distinguishable by
    // the width a column takes.
    engine->setCurrentDesktop(2);
    QVariantMap other = twoEntryTemplate();
    QVariantList otherBlueprint;
    QVariantMap otherEntry;
    otherEntry.insert(ScrollPerScreenKeys::templateColumnWidth(), 0.9);
    otherEntry.insert(ScrollPerScreenKeys::templateColumnDisplay(), static_cast<int>(ColumnDisplay::Normal));
    otherBlueprint.append(otherEntry);
    other.insert(ScrollPerScreenKeys::templateColumns(), otherBlueprint);
    other.insert(ScrollPerScreenKeys::defaultColumnWidthValue(), 0.15);
    engine->applyPerScreenConfig(kS1, other);

    // Back to desktop 1 WITHOUT re-pushing. The engine has to still hold this
    // context's own overrides — keyed per screen, desktop 2's template would
    // have overwritten them and desktop 1 would resolve against the wrong
    // template entirely.
    engine->setCurrentDesktop(1);

    engine->windowOpened(QStringLiteral("app|c"), kS1, 0, 0);
    QVERIFY(columnExists(engine, kS1, QStringLiteral("app|c")));
    // Desktop 1's own beyond-blueprint default, not desktop 2's 0.15.
    QCOMPARE(openedWidth(engine, kS1, QStringLiteral("app|c")).proportion, 0.3);
}

void TestScrollEngineTemplate::floatingTheLastWindowDoesNotRestartTheSeed()
{
    // A strip can resolve to no columns while still standing for its
    // entries: floating the last tiled window re-applies the layout
    // synchronously with an empty resolve, and the window returns through a
    // path that consumes nothing. The reset is gated on the STRIP being
    // empty, not on the resolve, so a float-and-unfloat round trip must not
    // refill the blueprint.
    QObject owner;
    auto* settings = new StubScrollSettings(&owner);
    ScrollEngine* engine = makeEngine(&owner, settings);

    engine->applyPerScreenConfig(kS1, twoEntryTemplate());
    engine->windowOpened(QStringLiteral("app|a"), kS1, 0, 0);
    engine->windowOpened(QStringLiteral("app|b"), kS1, 0, 0);
    QCOMPARE(openedWidth(engine, kS1, QStringLiteral("app|b")).proportion, 0.4);

    auto* state = static_cast<ScrollState*>(engine->stateForScreen(kS1));
    QVERIFY(state);

    // Float both, emptying the strip transitively while the windows are still
    // on the screen.
    engine->setWindowFloat(QStringLiteral("app|b"), true, kS1);
    engine->setWindowFloat(QStringLiteral("app|a"), true, kS1);
    QCoreApplication::processEvents();
    QVERIFY(state->strip().isEmpty());
    QCOMPARE(state->floatingWindows().size(), 2);

    // Both entries stay spent: the screen was never cleared out, it just has
    // nothing tiled at this instant.
    engine->windowOpened(QStringLiteral("app|c"), kS1, 0, 0);
    QCOMPARE(openedWidth(engine, kS1, QStringLiteral("app|c")).proportion, 0.3);
}

QTEST_GUILESS_MAIN(TestScrollEngineTemplate)
#include "test_scrollengine_template.moc"
