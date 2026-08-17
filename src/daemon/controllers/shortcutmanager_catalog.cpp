// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// ═══════════════════════════════════════════════════════════════════════════════
// ShortcutManager — cheatsheet catalog metadata + model builder
//
// Presentation-only concern split out of shortcutmanager.cpp: the per-id
// category / mode classification, the QVariantList model the cheatsheet
// overlay consumes, and the family compression that collapses the numbered
// and directional shortcut families into single rows.
// ═══════════════════════════════════════════════════════════════════════════════

#include "shortcutmanager.h"
#include "shortcutmanager_ids.h"

#include "config/configdefaults.h"
#include "core/platform/logging.h"
#include "phosphor_i18n.h"

#include <PhosphorShortcuts/Registry.h>

#include <QHash>
#include <QKeySequence>
#include <QSet>
#include <QVariantMap>

#include <algorithm>

namespace PlasmaZones {

namespace {

using namespace ShortcutIds;

// ─── Cheatsheet catalog metadata ────────────────────────────────────────────
// Display category + mode applicability per shortcut id, consumed by
// cheatsheetModel(). Kept as a separate id-keyed table (rather than columns
// on kStaticEntries) so registration stays independent of presentation and
// the whole classification reads in one place. Category labels are
// untranslated keys; cheatsheetModel() runs them through PhosphorI18n::tr.
//
// Mode classification contract (maintainer-decided). A shortcut is tagged
// with the modes it actually DOES something in, so the sheet never hides a
// working key and never advertises a dead one:
//  - "all" for everything each engine implements in its own address space:
//    the directional move/focus/swap quads, rotate, the cycle pair, restore
//    size, the snap-to-zone digits (moveFocusedToPosition addresses
//    zones in snapping, layout slots in autotile, visible tile slots in
//    scrolling), and the virtual-screen swap and rotate family (virtual
//    screens exist in every mode)
//  - "snapping" for ops that are hard no-ops off snapping: the multi-zone
//    span quad and the empty-zone push
//  - "autotile" for the master-stack ops, hard no-ops off autotile
//  - "scrolling" for column/strip ops (consume/expel, column widths, tab
//    display, the windowed-fullscreen presentation toggle), hard no-ops off
//    scrolling
//  - "layouts" is a CAPABILITY tag, not a mode name: rows shown whenever
//    the bound screen's engine consumes user-selectable layouts
//    (IPlacementEngine::layoutSupport, pushed to the sheet as
//    layoutsAvailable). Covers the layout cycle pair, the picker, the
//    layout lock, and the quick-layout digits — on a non-layout screen
//    those keys answer with a "not available" OSD, so the sheet hides them.
//  - toggle_autotile is the doorway INTO autotile → all modes, always shown
// A row tagged "all" also has to READ mode-neutrally. A label that names
// zones on a key which addresses columns in scrolling misinforms the reader
// exactly as much as the wrong tag would.
struct CatalogMeta
{
    const char* category;
    // Sort key for the category block, NOT an index: only the relative order
    // matters, so the values are deliberately sparse (2 and 5-7 are
    // unallocated) to leave room for a new category between two existing ones
    // without renumbering the table. Gaps are not removed categories.
    int categoryOrder;
    // "all" | "snapping" | "autotile" | "scrolling" | "layouts" — string form
    // matches what the QML filter consumes; no enum round-trip needed. The
    // last value is a capability tag (engine layoutSupport), not a mode;
    // see the contract block above.
    const char* mode;
    // Optional tr() disambiguation for the category word (e.g. the mode
    // name "Scrolling", whose bare source would otherwise inherit the
    // scrollbar-sense translation lupdate merges by source text).
    const char* categoryDisambiguation = nullptr;
    // Optional cheatsheet display label. The registration description must
    // stand alone (System Settings lists it without context), but on the
    // sheet the group heading already carries the context, so rows that
    // repeat it ("Rotate Virtual Screens Clockwise" under "Virtual
    // Screens") overflow the column for no information. nullptr = use the
    // registration description.
    const char* shortLabel = nullptr;
    // Optional plain-prose explanation of what the action does, surfaced as
    // a tooltip on the cheatsheet row. The System Settings KCM cannot show
    // it (KGlobalAccel carries only the action name), so the registration
    // description still has to stand alone without this. nullptr = no
    // tooltip; reserved for actions whose name alone does not tell a reader
    // what will happen (the scrolling column vocabulary, and the layouts
    // rows whose meaning shifts per capability).
    const char* explanation = nullptr;
    // Optional Templates-capability variant of `explanation`: shown instead
    // of it when the bound screen's engine consumes layouts as sizing
    // templates (a scrolling screen), where the same key picks a TEMPLATE
    // rather than a placement layout. The same slot also serves plain
    // scrolling-screen wording for mode-neutral rows whose vocabulary
    // shifts there (the layer-focus switch names columns instead of the
    // placed layout). nullptr = `explanation` serves both.
    const char* templatesExplanation = nullptr;
};

CatalogMeta catalogMetaForId(const QString& id)
{
    static const QHash<QString, CatalogMeta> kMeta = [] {
        QHash<QString, CatalogMeta> m;
        const auto add = [&m](const char* id, const char* category, int order, const char* mode,
                              const char* categoryDisambiguation = nullptr, const char* shortLabel = nullptr,
                              const char* explanation = nullptr, const char* templatesExplanation = nullptr) {
            m.insert(QLatin1String(id),
                     {category, order, mode, categoryDisambiguation, shortLabel, explanation, templatesExplanation});
        };
        // The scrolling category word needs the "tiling mode name"
        // disambiguation or it inherits the scrollbar-sense translation.
        // QT_TRANSLATE_NOOP3 makes the extraction self-contained: relying
        // on another file's live tr() call would silently orphan the whole
        // category translation if that call were reworded or removed.
        static constexpr struct
        {
            const char* source;
            const char* comment;
        } kScrollingCategory = QT_TRANSLATE_NOOP3("plasmazones", "Scrolling", "tiling mode name");
        constexpr const char* kModeNameContext = kScrollingCategory.comment;
        add(kIdOpenEditor, QT_TRANSLATE_NOOP("plasmazones", "General"), 0, "all");
        add(kIdOpenSettings, QT_TRANSLATE_NOOP("plasmazones", "General"), 0, "all");
        add(kIdToggleCheatsheet, QT_TRANSLATE_NOOP("plasmazones", "General"), 0, "all");
        add(kIdToggleWindowFloat, QT_TRANSLATE_NOOP("plasmazones", "General"), 0, "all");
        // Mode-all row, so the label avoids "Tiled": snap users know their
        // layer as "snapped" (the OSD splits the token for exactly that
        // reason), and "placed" reads correctly in all three modes. The
        // tooltip promises the remembered window only conditionally — the
        // fallback scan is sorted, not recency-ordered.
        add(kIdSwitchFocusFloatTiling, QT_TRANSLATE_NOOP("plasmazones", "General"), 0, "all", nullptr,
            QT_TRANSLATE_NOOP("plasmazones", "Switch Floating and Placed Focus"),
            QT_TRANSLATE_NOOP("plasmazones",
                              "Moves focus between the floating windows and the placed layout. It returns to "
                              "the window that last had focus there when that window is still available."),
            QT_TRANSLATE_NOOP("plasmazones",
                              "Moves focus between the floating windows and the scrolling columns. It returns to "
                              "the window that last had focus there when that window is still available."));
        // "all", not a per-mode tag, and deliberately not gated on how many
        // modes are enabled. The row advertises the mode CYCLE, which is a
        // hard no-op only in the one configuration where every mode but the
        // current one is switched off — and in that configuration the honest
        // thing is still to show the key, because what fixes it is turning a
        // mode back on in settings, not a missing row.
        add(kIdToggleAutotile, QT_TRANSLATE_NOOP("plasmazones", "General"), 0, "all");
        // "layouts" is a capability tag, not a mode name: the rows show
        // whenever the bound screen's engine consumes user-selectable
        // layouts (IPlacementEngine::layoutSupport, pushed to the sheet
        // as layoutsAvailable). That now includes scrolling screens, where
        // the same keys pick/cycle the TEMPLATE layout (LayoutSupport::
        // Templates); the rows hide only on a LayoutSupport::None engine.
        add(kIdPreviousLayout, QT_TRANSLATE_NOOP("plasmazones", "Layouts"), 1, "layouts", nullptr, nullptr,
            QT_TRANSLATE_NOOP("plasmazones", "Switches this screen to the previous layout in the list."),
            QT_TRANSLATE_NOOP("plasmazones", "Switches this screen to the previous column template."));
        add(kIdNextLayout, QT_TRANSLATE_NOOP("plasmazones", "Layouts"), 1, "layouts", nullptr, nullptr,
            QT_TRANSLATE_NOOP("plasmazones", "Switches this screen to the next layout in the list."),
            QT_TRANSLATE_NOOP("plasmazones", "Switches this screen to the next column template."));
        add(kIdLayoutPicker, QT_TRANSLATE_NOOP("plasmazones", "Layouts"), 1, "layouts", nullptr, nullptr,
            QT_TRANSLATE_NOOP("plasmazones", "Opens a picker to choose this screen's layout."),
            QT_TRANSLATE_NOOP("plasmazones",
                              "Opens a picker to choose this screen's column template. Its column widths "
                              "become the widths columns cycle through."));
        add(kIdToggleLayoutLock, QT_TRANSLATE_NOOP("plasmazones", "Layouts"), 1, "layouts", nullptr, nullptr,
            QT_TRANSLATE_NOOP("plasmazones", "Locks this screen's layout so nothing switches it until unlocked."),
            QT_TRANSLATE_NOOP("plasmazones",
                              "Locks this screen's column template so nothing switches it until unlocked."));
        // Resnap stays "all": it routes through the engine's reapplyLayout
        // intent, which every engine implements (scrolling re-lays the strip).
        add(kIdResnapToNewLayout, QT_TRANSLATE_NOOP("plasmazones", "Layouts"), 1, "all");
        // Mode-neutral sheet label: the registration description names zones
        // (System Settings lists it standalone), but the row is tagged "all"
        // and the action addresses zones only in snapping.
        add(kIdSnapAllWindows, QT_TRANSLATE_NOOP("plasmazones", "Layouts"), 1, "all", nullptr,
            QT_TRANSLATE_NOOP("plasmazones", "Arrange All Windows"));
        // Heading is "Zones", not "Snap to Zone": the group holds mode-neutral
        // rows (restore size, the numbered slots) whose keys address a zone in
        // snapping, a layout slot in tiling and a visible strip tile in
        // scrolling, so a snapping-vocabulary heading misreads them exactly as
        // a snapping-only mode tag would.
        add(kIdPushToEmptyZone, QT_TRANSLATE_NOOP("plasmazones", "Zones"), 3, "snapping");
        // Restore size works in every mode (off snapping it is the float-back
        // half of toggle_window_float), so hiding it on the sheet outside
        // snapping hid a key that does something.
        add(kIdRestoreWindowSize, QT_TRANSLATE_NOOP("plasmazones", "Zones"), 3, "all");
        // Directional families compress to one row each in cheatsheetModel(),
        // so Move/Focus/Swap/rotate/cycle all fit one "Windows" group instead
        // of four near-empty ones.
        add(kIdMoveWindowLeft, QT_TRANSLATE_NOOP("plasmazones", "Windows"), 4, "all");
        add(kIdMoveWindowRight, QT_TRANSLATE_NOOP("plasmazones", "Windows"), 4, "all");
        add(kIdMoveWindowUp, QT_TRANSLATE_NOOP("plasmazones", "Windows"), 4, "all");
        add(kIdMoveWindowDown, QT_TRANSLATE_NOOP("plasmazones", "Windows"), 4, "all");
        // Directional focus is "Move Focus" on the sheet, not "Focus Zone":
        // the keys move focus to the neighbour in that direction, which is a
        // zone in snapping, a layout slot in tiling and a strip tile in
        // scrolling. The registration descriptions keep the zone wording —
        // System Settings lists them without the sheet's context — so only
        // the sheet labels are neutralized here.
        add(kIdFocusZoneLeft, QT_TRANSLATE_NOOP("plasmazones", "Windows"), 4, "all", nullptr,
            QT_TRANSLATE_NOOP("plasmazones", "Move Focus Left"));
        add(kIdFocusZoneRight, QT_TRANSLATE_NOOP("plasmazones", "Windows"), 4, "all", nullptr,
            QT_TRANSLATE_NOOP("plasmazones", "Move Focus Right"));
        add(kIdFocusZoneUp, QT_TRANSLATE_NOOP("plasmazones", "Windows"), 4, "all", nullptr,
            QT_TRANSLATE_NOOP("plasmazones", "Move Focus Up"));
        add(kIdFocusZoneDown, QT_TRANSLATE_NOOP("plasmazones", "Windows"), 4, "all", nullptr,
            QT_TRANSLATE_NOOP("plasmazones", "Move Focus Down"));
        add(kIdSwapWindowLeft, QT_TRANSLATE_NOOP("plasmazones", "Windows"), 4, "all");
        add(kIdSwapWindowRight, QT_TRANSLATE_NOOP("plasmazones", "Windows"), 4, "all");
        add(kIdSwapWindowUp, QT_TRANSLATE_NOOP("plasmazones", "Windows"), 4, "all");
        add(kIdSwapWindowDown, QT_TRANSLATE_NOOP("plasmazones", "Windows"), 4, "all");
        // Span grows/shrinks a multi-zone snap — a hard no-op off snapping.
        add(kIdSpanWindowLeft, QT_TRANSLATE_NOOP("plasmazones", "Windows"), 4, "snapping");
        add(kIdSpanWindowRight, QT_TRANSLATE_NOOP("plasmazones", "Windows"), 4, "snapping");
        add(kIdSpanWindowUp, QT_TRANSLATE_NOOP("plasmazones", "Windows"), 4, "snapping");
        add(kIdSpanWindowDown, QT_TRANSLATE_NOOP("plasmazones", "Windows"), 4, "snapping");
        // Rotate is meaningful in every mode, and the unit it rotates differs
        // per mode by design: zones in snapping, layout slots in tiling, and
        // whole COLUMNS in scrolling. That last one is deliberately coarser
        // than the visible TILE the scroll zone numbers and the digits
        // address, so the two are not the same space and the wording here
        // must not be aligned with the tile-numbering wording elsewhere.
        add(kIdRotateWindowsCW, QT_TRANSLATE_NOOP("plasmazones", "Windows"), 4, "all", nullptr,
            QT_TRANSLATE_NOOP("plasmazones", "Rotate Clockwise"));
        add(kIdRotateWindowsCCW, QT_TRANSLATE_NOOP("plasmazones", "Windows"), 4, "all", nullptr,
            QT_TRANSLATE_NOOP("plasmazones", "Rotate Counterclockwise"));
        // Every engine implements the focus cycle and the router does not gate
        // it, so tagging it snapping-only dropped a working shortcut off the
        // sheet in the other two modes. The label drops "in Zone" with the
        // tag: what it cycles through is a zone in snapping, a layout slot in
        // tiling and a column's tiles in scrolling.
        add(kIdCycleWindowForward, QT_TRANSLATE_NOOP("plasmazones", "Windows"), 4, "all", nullptr,
            QT_TRANSLATE_NOOP("plasmazones", "Cycle Focus Forward"));
        add(kIdCycleWindowBackward, QT_TRANSLATE_NOOP("plasmazones", "Windows"), 4, "all", nullptr,
            QT_TRANSLATE_NOOP("plasmazones", "Cycle Focus Backward"));
        add(kIdSwapVirtualScreenLeft, QT_TRANSLATE_NOOP("plasmazones", "Virtual Screens"), 8, "all", nullptr,
            QT_TRANSLATE_NOOP("plasmazones", "Swap Screen Left"));
        add(kIdSwapVirtualScreenRight, QT_TRANSLATE_NOOP("plasmazones", "Virtual Screens"), 8, "all", nullptr,
            QT_TRANSLATE_NOOP("plasmazones", "Swap Screen Right"));
        add(kIdSwapVirtualScreenUp, QT_TRANSLATE_NOOP("plasmazones", "Virtual Screens"), 8, "all", nullptr,
            QT_TRANSLATE_NOOP("plasmazones", "Swap Screen Up"));
        add(kIdSwapVirtualScreenDown, QT_TRANSLATE_NOOP("plasmazones", "Virtual Screens"), 8, "all", nullptr,
            QT_TRANSLATE_NOOP("plasmazones", "Swap Screen Down"));
        add(kIdRotateVirtualScreensCW, QT_TRANSLATE_NOOP("plasmazones", "Virtual Screens"), 8, "all", nullptr,
            QT_TRANSLATE_NOOP("plasmazones", "Rotate Clockwise"));
        add(kIdRotateVirtualScreensCCW, QT_TRANSLATE_NOOP("plasmazones", "Virtual Screens"), 8, "all", nullptr,
            QT_TRANSLATE_NOOP("plasmazones", "Rotate Counterclockwise"));
        add(kIdFocusMaster, QT_TRANSLATE_NOOP("plasmazones", "Autotile"), 9, "autotile");
        add(kIdSwapMaster, QT_TRANSLATE_NOOP("plasmazones", "Autotile"), 9, "autotile");
        add(kIdIncreaseMasterRatio, QT_TRANSLATE_NOOP("plasmazones", "Autotile"), 9, "autotile");
        add(kIdDecreaseMasterRatio, QT_TRANSLATE_NOOP("plasmazones", "Autotile"), 9, "autotile");
        add(kIdIncreaseMasterCount, QT_TRANSLATE_NOOP("plasmazones", "Autotile"), 9, "autotile");
        add(kIdDecreaseMasterCount, QT_TRANSLATE_NOOP("plasmazones", "Autotile"), 9, "autotile");
        add(kIdRetile, QT_TRANSLATE_NOOP("plasmazones", "Autotile"), 9, "autotile");
        // Every scrolling row carries an explanation: the column vocabulary
        // (consume, expel, grow) is opaque to anyone who has not used a
        // scrolling tiler before, and the sheet is where they look it up.
        add(kIdScrollFocusColumnFirst, kScrollingCategory.source, 10, "scrolling", kModeNameContext, nullptr,
            QT_TRANSLATE_NOOP("plasmazones", "Moves focus to the first column."));
        add(kIdScrollFocusColumnLast, kScrollingCategory.source, 10, "scrolling", kModeNameContext, nullptr,
            QT_TRANSLATE_NOOP("plasmazones", "Moves focus to the last column."));
        add(kIdScrollMoveColumnToFirst, kScrollingCategory.source, 10, "scrolling", kModeNameContext, nullptr,
            QT_TRANSLATE_NOOP("plasmazones", "Moves the focused column to the first position."));
        add(kIdScrollMoveColumnToLast, kScrollingCategory.source, 10, "scrolling", kModeNameContext, nullptr,
            QT_TRANSLATE_NOOP("plasmazones", "Moves the focused column to the last position."));
        add(kIdScrollConsumeWindow, kScrollingCategory.source, 10, "scrolling", kModeNameContext,
            QT_TRANSLATE_NOOP("plasmazones", "Consume Window"),
            QT_TRANSLATE_NOOP("plasmazones",
                              "Pulls a window from the next column into the focused column, stacking them."));
        add(kIdScrollExpelWindow, kScrollingCategory.source, 10, "scrolling", kModeNameContext,
            QT_TRANSLATE_NOOP("plasmazones", "Expel Window"),
            QT_TRANSLATE_NOOP("plasmazones",
                              "Moves the focused window out of a shared column into a new column after it."));
        add(kIdScrollConsumeOrExpelLeft, kScrollingCategory.source, 10, "scrolling", kModeNameContext, nullptr,
            QT_TRANSLATE_NOOP("plasmazones",
                              "Splits the focused window out of a shared column toward the start of the strip. "
                              "A window alone in its column merges into the previous column instead."));
        add(kIdScrollConsumeOrExpelRight, kScrollingCategory.source, 10, "scrolling", kModeNameContext, nullptr,
            QT_TRANSLATE_NOOP("plasmazones",
                              "Splits the focused window out of a shared column toward the end of the strip. "
                              "A window alone in its column merges into the next column instead."));
        add(kIdScrollCenterColumn, kScrollingCategory.source, 10, "scrolling", kModeNameContext, nullptr,
            QT_TRANSLATE_NOOP("plasmazones", "Scrolls the view so the focused column sits centered on the screen."));
        add(kIdScrollToggleColumnTabbed, kScrollingCategory.source, 10, "scrolling", kModeNameContext, nullptr,
            QT_TRANSLATE_NOOP("plasmazones", "Switches the focused column between stacked windows and tabs."));
        add(kIdScrollToggleWindowedFullscreen, kScrollingCategory.source, 10, "scrolling", kModeNameContext, nullptr,
            QT_TRANSLATE_NOOP("plasmazones",
                              "Puts the focused window into its fullscreen presentation while it keeps its "
                              "place in the column, so it does not cover the screen. Press again to leave "
                              "it."));
        add(kIdScrollCycleColumnWidth, kScrollingCategory.source, 10, "scrolling", kModeNameContext,
            QT_TRANSLATE_NOOP("plasmazones", "Cycle Column Width"),
            QT_TRANSLATE_NOOP("plasmazones",
                              "Steps the focused column through the screen's size presets along the strip."));
        add(kIdScrollCycleColumnWidthBack, kScrollingCategory.source, 10, "scrolling", kModeNameContext,
            QT_TRANSLATE_NOOP("plasmazones", "Cycle Column Width Back"),
            QT_TRANSLATE_NOOP("plasmazones",
                              "Steps the focused column through the screen's size presets along the strip, "
                              "in reverse."));
        add(kIdScrollIncreaseColumnWidth, kScrollingCategory.source, 10, "scrolling", kModeNameContext, nullptr,
            QT_TRANSLATE_NOOP("plasmazones", "Grows the focused column along the strip by the configured step."));
        add(kIdScrollDecreaseColumnWidth, kScrollingCategory.source, 10, "scrolling", kModeNameContext, nullptr,
            QT_TRANSLATE_NOOP("plasmazones", "Shrinks the focused column along the strip by the configured step."));
        // "A smaller size", not "its previous width": the engine keeps ONE
        // pre-maximize slot for the whole strip, so a second column's
        // maximize discards the first's stored width and un-maximizing then
        // falls back to the default width. And the target is the work area
        // (panels excluded), not the screen.
        add(kIdScrollMaximizeColumn, kScrollingCategory.source, 10, "scrolling", kModeNameContext, nullptr,
            QT_TRANSLATE_NOOP("plasmazones",
                              "Toggles the focused column between filling the work area and a smaller size."));
        add(kIdScrollExpandColumn, kScrollingCategory.source, 10, "scrolling", kModeNameContext,
            QT_TRANSLATE_NOOP("plasmazones", "Grow into Empty Space"),
            QT_TRANSLATE_NOOP("plasmazones",
                              "Grows the focused column to fill the empty space visible on screen. "
                              "Other columns keep their size."));
        add(kIdScrollCycleWindowHeight, kScrollingCategory.source, 10, "scrolling", kModeNameContext,
            QT_TRANSLATE_NOOP("plasmazones", "Cycle Window Height"),
            QT_TRANSLATE_NOOP("plasmazones",
                              "Steps the focused window through the screen's size presets within its column."));
        add(kIdScrollCycleWindowHeightBack, kScrollingCategory.source, 10, "scrolling", kModeNameContext,
            QT_TRANSLATE_NOOP("plasmazones", "Cycle Window Height Back"),
            QT_TRANSLATE_NOOP("plasmazones",
                              "Steps the focused window through the screen's size presets within its column, "
                              "in reverse."));
        add(kIdScrollIncreaseWindowHeight, kScrollingCategory.source, 10, "scrolling", kModeNameContext, nullptr,
            QT_TRANSLATE_NOOP("plasmazones", "Grows the focused window within its column by the configured step."));
        add(kIdScrollDecreaseWindowHeight, kScrollingCategory.source, 10, "scrolling", kModeNameContext, nullptr,
            QT_TRANSLATE_NOOP("plasmazones", "Shrinks the focused window within its column by the configured step."));
        add(kIdScrollResetWindowHeights, kScrollingCategory.source, 10, "scrolling", kModeNameContext, nullptr,
            QT_TRANSLATE_NOOP("plasmazones",
                              "Clears manual window sizes in the focused column so its windows share the "
                              "column's space evenly."));
        add(kIdScrollCenterVisibleColumns, kScrollingCategory.source, 10, "scrolling", kModeNameContext, nullptr,
            QT_TRANSLATE_NOOP("plasmazones", "Scrolls the view so the fully visible columns sit centered as a group."));
        // No shortLabel on this pair: the registration names ("Focus First /
        // Last Window in Column") are already short enough for the column, so
        // an override would have to repeat them verbatim and ship a second
        // translatable string saying the same thing.
        add(kIdScrollFocusWindowTop, kScrollingCategory.source, 10, "scrolling", kModeNameContext, nullptr,
            QT_TRANSLATE_NOOP("plasmazones", "Moves focus to the first window of the focused column."));
        add(kIdScrollFocusWindowBottom, kScrollingCategory.source, 10, "scrolling", kModeNameContext, nullptr,
            QT_TRANSLATE_NOOP("plasmazones", "Moves focus to the last window of the focused column."));
        add(kIdScrollFocusColumnLeft, kScrollingCategory.source, 10, "scrolling", kModeNameContext,
            QT_TRANSLATE_NOOP("plasmazones", "Focus Previous Column (Edge Stop)"),
            QT_TRANSLATE_NOOP("plasmazones",
                              "Moves focus one column toward the start of the strip and stops at the edge. The "
                              "regular focus shortcut continues onto the next monitor instead."));
        add(kIdScrollFocusColumnRight, kScrollingCategory.source, 10, "scrolling", kModeNameContext,
            QT_TRANSLATE_NOOP("plasmazones", "Focus Next Column (Edge Stop)"),
            QT_TRANSLATE_NOOP("plasmazones",
                              "Moves focus one column toward the end of the strip and stops at the edge. The "
                              "regular focus shortcut continues onto the next monitor instead."));
        add(kIdScrollFocusColumnLeftOrLast, kScrollingCategory.source, 10, "scrolling", kModeNameContext,
            QT_TRANSLATE_NOOP("plasmazones", "Focus Previous Column (Wrap)"),
            QT_TRANSLATE_NOOP("plasmazones",
                              "Moves focus one column toward the start of the strip, wrapping to the last column "
                              "at the edge."));
        add(kIdScrollFocusColumnRightOrFirst, kScrollingCategory.source, 10, "scrolling", kModeNameContext,
            QT_TRANSLATE_NOOP("plasmazones", "Focus Next Column (Wrap)"),
            QT_TRANSLATE_NOOP("plasmazones",
                              "Moves focus one column toward the end of the strip, wrapping to the first column "
                              "at the edge."));
        add(kIdScrollMoveToFloating, kScrollingCategory.source, 10, "scrolling", kModeNameContext,
            QT_TRANSLATE_NOOP("plasmazones", "Move to Floating"),
            QT_TRANSLATE_NOOP("plasmazones",
                              "Makes the focused window float. Unlike the float toggle, it never re-tiles."));
        add(kIdScrollMoveToTiling, kScrollingCategory.source, 10, "scrolling", kModeNameContext,
            QT_TRANSLATE_NOOP("plasmazones", "Move to Tiled"),
            QT_TRANSLATE_NOOP("plasmazones",
                              "Returns the focused floating window to its column. Unlike the float toggle, "
                              "it never floats."));
        return m;
    }();

    const auto it = kMeta.constFind(id);
    if (it != kMeta.constEnd()) {
        return *it;
    }
    // Indexed slot families are prefix-keyed, not enumerated above. The
    // prefixes come from the id header, so a rename moves the ids and these
    // tests together instead of compiling clean and quietly dumping the whole
    // family into the "Other" bucket below.
    if (id.startsWith(QLatin1String(kQuickLayoutPrefix))) {
        // Same capability tag as the enumerated Layouts rows above — and the
        // same per-capability tooltip split those rows carry, since the
        // digit family's meaning shifts with the capability too.
        return {QT_TRANSLATE_NOOP("plasmazones", "Layouts"),
                1,
                "layouts",
                nullptr,
                nullptr,
                QT_TRANSLATE_NOOP("plasmazones", "Applies the numbered layout to this screen."),
                QT_TRANSLATE_NOOP("plasmazones", "Applies the numbered column template to this screen.")};
    }
    if (id.startsWith(QLatin1String(kSnapToZonePrefix))) {
        // Every engine implements moveFocusedToPosition: zone N in
        // snapping, layout slot N in tiling, visible tile slot N in
        // scrolling. Only 1 to 9 have shortcuts, so a scrolling strip with
        // more visible tiles than that numbers them all but leaves the ones
        // past 9 with no digit that reaches them. The tooltip stays
        // mode-neutral for exactly that per-mode divergence.
        return {QT_TRANSLATE_NOOP("plasmazones", "Zones"),
                3,
                "all",
                nullptr,
                nullptr,
                QT_TRANSLATE_NOOP("plasmazones", "Sends the focused window to the numbered slot on this screen.")};
    }
    // A shortcut added to the table without catalog metadata still shows up
    // (miscategorised beats invisible), and the log points at the fix.
    qCWarning(lcShortcuts) << "cheatsheet: no catalog metadata for shortcut id" << id;
    return {QT_TRANSLATE_NOOP("plasmazones", "Other"), 99, "all"};
}

/// The key token of a shortcut sequence: everything after the last '+'.
///
/// File-scope rather than a lambda because the cheatsheet family compression
/// has a PRODUCER (which builds the merged chip's token list) and a CONSUMER
/// (which matches a member's token against the family's expected one), and
/// they must agree exactly. They did not: a sequence ENDING in '+' binds the
/// plus key itself ("Meta+Alt++"), where a bare mid(split + 1) yields an EMPTY
/// token. The producer special-cased it and the consumer did not, so the two
/// could never match on such a binding and the family silently stopped
/// compressing. No shipped default ends in '+', so it took a user rebind to
/// reach.
///
/// One definition removes THIS divergence, not every one: the consumer also
/// rejects split == 0 (a sequence starting with '+') and split == size-1 (a
/// '+'-terminated binding, whose retained-'+' prefix would render a garbage
/// merged chip) before it ever asks for the token, where the producer
/// happily tokenizes both. Those cases cannot wrongly compress — they only
/// decline to — so the asymmetry is left alone.
QString lastKeyToken(const QString& sequence)
{
    const int split = sequence.lastIndexOf(QLatin1Char('+'));
    if (split < 0) {
        return sequence;
    }
    if (split == sequence.size() - 1) {
        return QStringLiteral("+");
    }
    return sequence.mid(split + 1);
}

} // namespace

QVariantList ShortcutManager::cheatsheetModel() const
{
    QVector<QVariantMap> rows;
    rows.reserve(m_entries.size());
    for (const auto& e : m_entries) {
        const CatalogMeta meta = catalogMetaForId(e.id);
        QStringList triggers;
        if (m_registry) {
            triggers = m_registry->effectiveTriggers(e.id);
        }
        // Normalize to PortableText where the string parses as a key
        // sequence. KGlobalAccel read-back is PortableText already, but the
        // Portal backend relays the compositor's trigger_description
        // verbatim, which may be native/localized spelling — without
        // normalization the family compression's token compares silently
        // fail and the sheet shows every family-member row uncompressed. A string that
        // doesn't parse stays verbatim (better an odd chip than a lost
        // binding).
        for (QString& t : triggers) {
            const QKeySequence parsed(t);
            if (!parsed.isEmpty()) {
                t = parsed.toString(QKeySequence::PortableText);
            }
        }
        QVariantMap row;
        row.insert(QStringLiteral("id"), e.id);
        row.insert(QStringLiteral("label"), meta.shortLabel ? PhosphorI18n::tr(meta.shortLabel) : e.description);
        // Always present (possibly empty) so the QML tooltip binding never
        // reads an undefined role.
        row.insert(QStringLiteral("description"), meta.explanation ? PhosphorI18n::tr(meta.explanation) : QString());
        // Templates-capability tooltip variant; falls back to the plain
        // explanation so QML can bind one expression per row.
        row.insert(QStringLiteral("templatesDescription"),
                   meta.templatesExplanation ? PhosphorI18n::tr(meta.templatesExplanation)
                                             : (meta.explanation ? PhosphorI18n::tr(meta.explanation) : QString()));
        row.insert(QStringLiteral("category"), PhosphorI18n::tr(meta.category, meta.categoryDisambiguation));
        row.insert(QStringLiteral("categoryOrder"), meta.categoryOrder);
        row.insert(QStringLiteral("triggers"), triggers);
        row.insert(QStringLiteral("assigned"), !triggers.isEmpty());
        row.insert(QStringLiteral("mode"), QString::fromLatin1(meta.mode));
        rows.push_back(row);
    }

    // ─── Family compression ────────────────────────────────────────────────
    // The numbered slot families (kIndexedSlotCount rows each) and the
    // directional quads (4 rows each) dominate the sheet as walls of
    // near-identical lines.
    // When every member of a family is assigned, all members share the same
    // modifier prefix, and each member's final key token is the expected one
    // (its digit / direction), the family collapses into ONE row whose last
    // chip is a range token ("1-9") or "Arrows". Any deviation — a member
    // unassigned, a rebind off-pattern — falls back to the individual rows,
    // because a compressed row would then lie about what the keys do.
    using FamilySpec = CheatsheetFamily;
    // The scrolling pairs have no structural token pattern to expect (unlike
    // the digits and the arrows), so their expected tokens come from the
    // SAME defaults the shortcuts are registered with. Spelling them here
    // would silently stop the family collapsing the moment a default is
    // retuned, and the sheet would fall back to two near-identical rows with
    // nothing to explain why.
    //
    // Structural asymmetry worth knowing: because the expectation IS the
    // default, any user rebind of either member uncompresses this pair for
    // good, where a digit or arrow family recompresses as soon as the rebind
    // still lands on its structural token. The one exception is a member
    // whose default is empty — several verbs now SHIP unbound, and a user
    // can clear a bound one — whose expectation is taken from the live
    // binding instead; see addScrollPair below.
    const auto lastKeyOf = [](const QString& sequence) {
        // Normalize to PortableText first, exactly as the row builder above
        // does to the LIVE triggers before the compare. The defaults are
        // authored in config spelling, and a spelling difference alone would
        // silently stop the pair collapsing.
        const QKeySequence parsed(sequence);
        return lastKeyToken(parsed.isEmpty() ? sequence : parsed.toString(QKeySequence::PortableText));
    };
    const QStringList arrowTokens{QStringLiteral("Left"), QStringLiteral("Right"), QStringLiteral("Up"),
                                  QStringLiteral("Down")};
    const QString arrowsTail = PhosphorI18n::tr("Arrows");
    QStringList digitTokens;
    QStringList quickLayoutIds;
    QStringList snapToZoneIds;
    for (int i = 0; i < kIndexedSlotCount; ++i) {
        digitTokens.append(QString::number(i + 1));
        quickLayoutIds.append(quickLayoutId(i));
        snapToZoneIds.append(snapToZoneId(i));
    }
    // One spelling of the digit range everywhere: the chip token and the row
    // labels used to disagree ("1…9" against "1-9") for no reason a reader
    // could act on.
    const QString digitRange = QStringLiteral("1-") + QString::number(kIndexedSlotCount);
    // INVARIANT: a spec without a combinedDescription must only cover members
    // that carry no per-action explanation — the merged row keeps the FIRST
    // member's description, and a directional/digit member's single-direction
    // wording would misdescribe the whole family. This covers the
    // templatesDescription variant too: a combinedDescription overwrites
    // BOTH tooltip roles, so a member's templatesExplanation is dropped by
    // the merge and a family needing distinct templates wording must be left
    // uncompressed rather than given one combined string.  Every member below
    // has an empty explanation today; give the spec a combinedDescription
    // before adding one.
    QVector<FamilySpec> families = {
        // Labels derive the digit range from kIndexedSlotCount, like the chip
        // token, so raising the constant cannot desynchronize the two.
        // "range of layout slots" disambiguation: the registration table uses
        // the SAME source string with an ordinal ("Apply Layout 3"), and a
        // locale may phrase a range differently from an ordinal — without the
        // context lupdate would merge the two into one translatable unit.
        {quickLayoutIds, digitTokens,
         PhosphorI18n::tr("Apply Layout %1", "range of layout slots, e.g. 1-9").arg(digitRange), digitRange},
        // "Zone", not "Snap to Zone": the group heading already carries that,
        // and the digits address a zone in snapping, a layout slot in tiling
        // and a visible tile in scrolling, so the row itself stays out of any
        // one mode's vocabulary.
        {snapToZoneIds, digitTokens, PhosphorI18n::tr("Zone %1").arg(digitRange), digitRange},
        {{QString::fromLatin1(kIdMoveWindowLeft), QString::fromLatin1(kIdMoveWindowRight),
          QString::fromLatin1(kIdMoveWindowUp), QString::fromLatin1(kIdMoveWindowDown)},
         arrowTokens,
         PhosphorI18n::tr("Move Window"),
         arrowsTail},
        {{QString::fromLatin1(kIdFocusZoneLeft), QString::fromLatin1(kIdFocusZoneRight),
          QString::fromLatin1(kIdFocusZoneUp), QString::fromLatin1(kIdFocusZoneDown)},
         arrowTokens,
         // Mode-neutral, like the per-row labels: what it focuses is a zone in
         // snapping, a layout slot in tiling and a strip tile in scrolling.
         PhosphorI18n::tr("Move Focus"),
         arrowsTail},
        {{QString::fromLatin1(kIdSwapWindowLeft), QString::fromLatin1(kIdSwapWindowRight),
          QString::fromLatin1(kIdSwapWindowUp), QString::fromLatin1(kIdSwapWindowDown)},
         arrowTokens,
         PhosphorI18n::tr("Swap Window"),
         arrowsTail},
        {{QString::fromLatin1(kIdSpanWindowLeft), QString::fromLatin1(kIdSpanWindowRight),
          QString::fromLatin1(kIdSpanWindowUp), QString::fromLatin1(kIdSpanWindowDown)},
         arrowTokens,
         PhosphorI18n::tr("Span Window"),
         arrowsTail},
        {{QString::fromLatin1(kIdSwapVirtualScreenLeft), QString::fromLatin1(kIdSwapVirtualScreenRight),
          QString::fromLatin1(kIdSwapVirtualScreenUp), QString::fromLatin1(kIdSwapVirtualScreenDown)},
         arrowTokens,
         // Group heading ("Virtual Screens") carries the context.
         PhosphorI18n::tr("Swap Screens"),
         arrowsTail},
        // Bracket-opposed and comma-opposed pairs outside the scrolling
        // group: their shipped defaults share a modifier prefix and differ
        // only in the final token, so they compress exactly like the arrow
        // quads.
        {{QString::fromLatin1(kIdRotateWindowsCW), QString::fromLatin1(kIdRotateWindowsCCW)},
         {QStringLiteral("]"), QStringLiteral("[")},
         PhosphorI18n::tr("Rotate"),
         QStringLiteral("] / [")},
        {{QString::fromLatin1(kIdCycleWindowForward), QString::fromLatin1(kIdCycleWindowBackward)},
         {QStringLiteral("."), QStringLiteral(",")},
         PhosphorI18n::tr("Cycle Focus"),
         QStringLiteral(". / ,")},
        {{QString::fromLatin1(kIdRotateVirtualScreensCW), QString::fromLatin1(kIdRotateVirtualScreensCCW)},
         {QStringLiteral("]"), QStringLiteral("[")},
         PhosphorI18n::tr("Rotate"),
         QStringLiteral("] / [")},
    };

    // The scrolling family's natural pairs collapse the same way the
    // directional quads do. Only the pairs whose OPPOSED members end in
    // DIFFERENT keys are listed: the letter+Shift pairs (consume/expel on I,
    // adjust width on W, both preset cycles on D and H) share their final
    // key, so compression would require both members to carry the identical
    // full sequence — a duplicate chord KGlobalAccel refuses — and a spec
    // for them would sit in the table permanently dead, extracting merged
    // labels no user could ever see.
    //
    // A pair is SKIPPED when either member has no sequence to expect: an
    // expectation of "" can never match a live trigger, so a spelled-out
    // pair for a default-less member would sit in the table looking active
    // while being structurally dead. Several defaults now SHIP unbound (the
    // edge-stop/wrap focus variants and the one-way float verbs) and no
    // specs are listed for those pairs — until a user binds both members
    // such a spec is dead weight, and once they do, the individual rows
    // still render each binding on its own. (A member a user cleared keeps
    // the shipped default as its expectation; a cleared member has no
    // trigger, so its row fails the single-trigger compression test either
    // way and renders individually.)
    //
    // Both members' key tokens joined for the merged chip. The digit family
    // reads as a range and the directional one as a class name; a bare
    // space-joined pair instead read as one chord, so the alternatives are
    // separated the way the row labels separate them.
    const auto addScrollPair = [&](const char* firstId, const QString& firstSeq, const char* secondId,
                                   const QString& secondSeq, const QString& label, const QString& description) {
        if (firstSeq.isEmpty() || secondSeq.isEmpty()) {
            return;
        }
        const QString firstKey = lastKeyOf(firstSeq);
        const QString secondKey = lastKeyOf(secondSeq);
        families.append(FamilySpec{{QString::fromLatin1(firstId), QString::fromLatin1(secondId)},
                                   {firstKey, secondKey},
                                   label,
                                   firstKey + QStringLiteral(" / ") + secondKey,
                                   description});
    };
    addScrollPair(kIdScrollFocusColumnFirst, ConfigDefaults::scrollingFocusColumnFirstShortcut(),
                  kIdScrollFocusColumnLast, ConfigDefaults::scrollingFocusColumnLastShortcut(),
                  PhosphorI18n::tr("Focus First / Last Column"),
                  PhosphorI18n::tr("Moves focus to the first or last column."));
    addScrollPair(kIdScrollMoveColumnToFirst, ConfigDefaults::scrollingMoveColumnToFirstShortcut(),
                  kIdScrollMoveColumnToLast, ConfigDefaults::scrollingMoveColumnToLastShortcut(),
                  PhosphorI18n::tr("Move Column to Start / End"),
                  PhosphorI18n::tr("Moves the focused column to the first or last position."));
    addScrollPair(kIdScrollConsumeOrExpelLeft, ConfigDefaults::scrollingConsumeOrExpelLeftShortcut(),
                  kIdScrollConsumeOrExpelRight, ConfigDefaults::scrollingConsumeOrExpelRightShortcut(),
                  PhosphorI18n::tr("Consume or Expel Toward Start / End"),
                  PhosphorI18n::tr("Splits the focused window out of a shared column toward that end of the strip. A "
                                   "window alone in its column merges into the neighboring column instead."));
    addScrollPair(kIdScrollIncreaseWindowHeight, ConfigDefaults::scrollingIncreaseWindowHeightShortcut(),
                  kIdScrollDecreaseWindowHeight, ConfigDefaults::scrollingDecreaseWindowHeightShortcut(),
                  PhosphorI18n::tr("Adjust Window Height"),
                  PhosphorI18n::tr("Adjusts the height of the focused window by the configured step."));

    QVector<QVariantMap> out = compressCheatsheetFamilies(rows, families);
    // Category blocks in display order; stable sort keeps the table's
    // hand-authored order within each category. Sorting the map vector
    // (before the QVariantList conversion) compares by reference instead of
    // detaching two QVariantMaps per comparison.
    std::stable_sort(out.begin(), out.end(), [](const QVariantMap& a, const QVariantMap& b) {
        return a.value(QLatin1String("categoryOrder")).toInt() < b.value(QLatin1String("categoryOrder")).toInt();
    });
    QVariantList model;
    model.reserve(out.size());
    for (const QVariantMap& row : std::as_const(out)) {
        model.push_back(row);
    }
    return model;
}

QVector<QVariantMap> ShortcutManager::compressCheatsheetFamilies(QVector<QVariantMap> rows,
                                                                 const QVector<CheatsheetFamily>& families)
{
    QHash<QString, int> rowIndexById;
    for (int i = 0; i < rows.size(); ++i) {
        rowIndexById.insert(rows[i].value(QLatin1String("id")).toString(), i);
    }
    QSet<int> removedIndices;
    // Every index an earlier family already consumed — its removed members
    // AND its rewritten first row. The overlap guard tests this set, not
    // removedIndices alone: a later family naming an earlier family's FIRST
    // member would otherwise slip past (that row was rewritten in place, not
    // removed) and only fail quietly on the token compare.
    QSet<int> consumedIndices;
    for (const auto& family : families) {
        // The two lists are parallel arrays; a mismatched spec would index
        // expectedLastTokens out of bounds below, and an EMPTY one would walk
        // the member loop zero times, stay "compressible", and then deref
        // ids.first() on an empty list. All current families are 9/9 or 4/4 —
        // this guards the table against a future bad entry.
        Q_ASSERT(!family.ids.isEmpty());
        Q_ASSERT(family.ids.size() == family.expectedLastTokens.size());
        if (family.ids.isEmpty() || family.ids.size() != family.expectedLastTokens.size()) {
            qCWarning(lcShortcuts) << "cheatsheet: malformed family spec for" << family.combinedLabel
                                   << "ids=" << family.ids.size() << "tokens=" << family.expectedLastTokens.size()
                                   << "— skipping compression";
            continue;
        }
        QString sharedPrefix;
        bool compressible = true;
        for (int m = 0; m < family.ids.size() && compressible; ++m) {
            const int idx = rowIndexById.value(family.ids[m], -1);
            if (idx < 0) {
                compressible = false;
                break;
            }
            // A member already consumed by an EARLIER family must not merge
            // again: the earlier merge removed its row (or rewrote it as that
            // family's merged row), so this family would fold itself into a
            // removed row and silently vanish from the sheet. Overlapping
            // specs are a table bug; refuse loudly like the malformed case.
            if (consumedIndices.contains(idx)) {
                qCWarning(lcShortcuts) << "cheatsheet: family" << family.combinedLabel << "member" << family.ids[m]
                                       << "already merged by an earlier family — skipping compression";
                compressible = false;
                break;
            }
            // No separate "assigned" check: the producer derives it as
            // triggers-non-empty, so the single-trigger requirement below
            // already rejects unassigned members.
            const QStringList memberTriggers = rows[idx].value(QLatin1String("triggers")).toStringList();
            // A member carrying an alternate binding must not compress: the
            // compressed row shows a single combined chip, so the extra
            // binding would silently vanish from the sheet.
            if (memberTriggers.size() != 1) {
                compressible = false;
                break;
            }
            const QString seq = memberTriggers.first();
            const int split = seq.lastIndexOf(QLatin1Char('+'));
            // Through the shared tokenizer, so a '+'-terminated binding is
            // read the same way the merged chip's producer reads it. A
            // '+'-terminated sequence (the plus KEY) is additionally rejected
            // outright: its prefix would keep the trailing '+' and the merged
            // chip would render as "Meta+Alt++ / …" garbage.
            if (split <= 0 || split == seq.size() - 1 || lastKeyToken(seq) != family.expectedLastTokens[m]) {
                compressible = false;
                break;
            }
            const QString prefix = seq.left(split);
            if (m == 0) {
                sharedPrefix = prefix;
            } else if (prefix != sharedPrefix) {
                compressible = false;
                break;
            }
        }
        if (!compressible) {
            continue;
        }
        // Sentinel default like the member lookups above: a compressibility
        // check weakened in future must not let a missing id resolve to 0 and
        // rewrite row 0 as the merged row.
        const int firstIdx = rowIndexById.value(family.ids.first(), -1);
        if (firstIdx < 0) {
            continue;
        }
        // The merged row keeps the FIRST member's mode and category and drops
        // the rest, so a family whose members disagree on either would hide
        // working rows behind the first member's mode filter or file them
        // under the wrong heading. Every current family is uniform on both;
        // this pins that as a requirement of the spec rather than a
        // coincidence of the table.
        // In a DEBUG build the Q_ASSERT below aborts, surfacing the table
        // typo at the developer's desk. The release behaviour is warn-only:
        // a mixed family still merges rather than bailing, because the
        // consequence is cosmetic (a row filed under the first member's mode
        // or heading) and dropping the compression would cost the user a
        // readable sheet over a table typo.
        for (int m = 1; m < family.ids.size(); ++m) {
            const int memberIdx = rowIndexById.value(family.ids[m], -1);
            const bool uniform = memberIdx < 0
                || (rows[memberIdx].value(QLatin1String("mode")) == rows[firstIdx].value(QLatin1String("mode"))
                    && rows[memberIdx].value(QLatin1String("categoryOrder"))
                        == rows[firstIdx].value(QLatin1String("categoryOrder")));
            Q_ASSERT(uniform);
            if (!uniform) {
                qCWarning(lcShortcuts) << "cheatsheet family" << family.ids.first() << "member" << family.ids[m]
                                       << "disagrees on mode/category; merged row keeps the first member's";
            }
        }
        QVariantMap& row = rows[firstIdx];
        row.insert(QStringLiteral("label"), family.combinedLabel);
        // The merged row otherwise keeps the first member's description,
        // which for an opposed pair names only one direction. Both tooltip
        // roles are rewritten: templatesDescription is what the sheet shows
        // on a Templates (scrolling) screen, and leaving it behind would put
        // the single-direction wording back on exactly the screens the
        // scrolling pairs are for.
        if (!family.combinedDescription.isEmpty()) {
            row.insert(QStringLiteral("description"), family.combinedDescription);
            row.insert(QStringLiteral("templatesDescription"), family.combinedDescription);
        }
        row.insert(QStringLiteral("triggers"), QStringList{sharedPrefix + QLatin1Char('+') + family.tailToken});
        consumedIndices.insert(firstIdx);
        for (int m = 1; m < family.ids.size(); ++m) {
            const int memberIdx = rowIndexById.value(family.ids[m], -1);
            if (memberIdx >= 0) {
                removedIndices.insert(memberIdx);
                consumedIndices.insert(memberIdx);
            }
        }
    }

    QVector<QVariantMap> out;
    out.reserve(rows.size());
    for (int i = 0; i < rows.size(); ++i) {
        if (!removedIndices.contains(i)) {
            out.push_back(rows[i]);
        }
    }
    return out;
}

} // namespace PlasmaZones
