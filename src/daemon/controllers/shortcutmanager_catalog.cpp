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
//    size, and the snap-to-zone digits (moveFocusedToPosition addresses
//    zones in snapping, layout slots in autotile, visible tile slots in
//    scrolling)
//  - "snapping" for ops that are hard no-ops off snapping: the multi-zone
//    span quad and the empty-zone push
//  - "autotile" for the master-stack ops, hard no-ops off autotile
//  - "scrolling" for column/strip ops (consume/expel, column widths, tab
//    display), hard no-ops off scrolling
//  - toggle_autotile is the doorway INTO autotile → all modes, always shown
// A row tagged "all" also has to READ mode-neutrally. A label that names
// zones on a key which addresses columns in scrolling misinforms the reader
// exactly as much as the wrong tag would.
struct CatalogMeta
{
    const char* category;
    int categoryOrder;
    // "all" | "snapping" | "autotile" | "scrolling" — string form matches what the QML
    // filter consumes; no enum round-trip needed.
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
};

CatalogMeta catalogMetaForId(const QString& id)
{
    static const QHash<QString, CatalogMeta> kMeta = [] {
        QHash<QString, CatalogMeta> m;
        const auto add = [&m](const char* id, const char* category, int order, const char* mode,
                              const char* categoryDisambiguation = nullptr, const char* shortLabel = nullptr) {
            m.insert(QLatin1String(id), {category, order, mode, categoryDisambiguation, shortLabel});
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
        add(kIdToggleAutotile, QT_TRANSLATE_NOOP("plasmazones", "General"), 0, "all");
        add(kIdPreviousLayout, QT_TRANSLATE_NOOP("plasmazones", "Layouts"), 1, "all");
        add(kIdNextLayout, QT_TRANSLATE_NOOP("plasmazones", "Layouts"), 1, "all");
        add(kIdLayoutPicker, QT_TRANSLATE_NOOP("plasmazones", "Layouts"), 1, "all");
        add(kIdToggleLayoutLock, QT_TRANSLATE_NOOP("plasmazones", "Layouts"), 1, "all");
        add(kIdResnapToNewLayout, QT_TRANSLATE_NOOP("plasmazones", "Layouts"), 1, "all");
        add(kIdSnapAllWindows, QT_TRANSLATE_NOOP("plasmazones", "Layouts"), 1, "all");
        add(kIdPushToEmptyZone, QT_TRANSLATE_NOOP("plasmazones", "Snap to Zone"), 3, "snapping");
        // Restore size works in every mode (off snapping it is the float-back
        // half of toggle_window_float), so hiding it on the sheet outside
        // snapping hid a key that does something.
        add(kIdRestoreWindowSize, QT_TRANSLATE_NOOP("plasmazones", "Snap to Zone"), 3, "all");
        // Directional families compress to one row each in cheatsheetModel(),
        // so Move/Focus/Swap/rotate/cycle all fit one "Windows" group instead
        // of four near-empty ones.
        add(kIdMoveWindowLeft, QT_TRANSLATE_NOOP("plasmazones", "Windows"), 4, "all");
        add(kIdMoveWindowRight, QT_TRANSLATE_NOOP("plasmazones", "Windows"), 4, "all");
        add(kIdMoveWindowUp, QT_TRANSLATE_NOOP("plasmazones", "Windows"), 4, "all");
        add(kIdMoveWindowDown, QT_TRANSLATE_NOOP("plasmazones", "Windows"), 4, "all");
        add(kIdFocusZoneLeft, QT_TRANSLATE_NOOP("plasmazones", "Windows"), 4, "all");
        add(kIdFocusZoneRight, QT_TRANSLATE_NOOP("plasmazones", "Windows"), 4, "all");
        add(kIdFocusZoneUp, QT_TRANSLATE_NOOP("plasmazones", "Windows"), 4, "all");
        add(kIdFocusZoneDown, QT_TRANSLATE_NOOP("plasmazones", "Windows"), 4, "all");
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
        add(kIdScrollFocusColumnFirst, kScrollingCategory.source, 10, "scrolling", kModeNameContext);
        add(kIdScrollFocusColumnLast, kScrollingCategory.source, 10, "scrolling", kModeNameContext);
        add(kIdScrollMoveColumnToFirst, kScrollingCategory.source, 10, "scrolling", kModeNameContext);
        add(kIdScrollMoveColumnToLast, kScrollingCategory.source, 10, "scrolling", kModeNameContext);
        add(kIdScrollConsumeWindow, kScrollingCategory.source, 10, "scrolling", kModeNameContext,
            QT_TRANSLATE_NOOP("plasmazones", "Consume Window"));
        add(kIdScrollExpelWindow, kScrollingCategory.source, 10, "scrolling", kModeNameContext,
            QT_TRANSLATE_NOOP("plasmazones", "Expel Window"));
        add(kIdScrollConsumeOrExpelLeft, kScrollingCategory.source, 10, "scrolling", kModeNameContext);
        add(kIdScrollConsumeOrExpelRight, kScrollingCategory.source, 10, "scrolling", kModeNameContext);
        add(kIdScrollCenterColumn, kScrollingCategory.source, 10, "scrolling", kModeNameContext);
        add(kIdScrollToggleColumnTabbed, kScrollingCategory.source, 10, "scrolling", kModeNameContext);
        add(kIdScrollCycleColumnWidth, kScrollingCategory.source, 10, "scrolling", kModeNameContext,
            QT_TRANSLATE_NOOP("plasmazones", "Cycle Column Width"));
        add(kIdScrollCycleColumnWidthBack, kScrollingCategory.source, 10, "scrolling", kModeNameContext,
            QT_TRANSLATE_NOOP("plasmazones", "Cycle Column Width Back"));
        add(kIdScrollIncreaseColumnWidth, kScrollingCategory.source, 10, "scrolling", kModeNameContext);
        add(kIdScrollDecreaseColumnWidth, kScrollingCategory.source, 10, "scrolling", kModeNameContext);
        add(kIdScrollMaximizeColumn, kScrollingCategory.source, 10, "scrolling", kModeNameContext);
        add(kIdScrollExpandColumn, kScrollingCategory.source, 10, "scrolling", kModeNameContext,
            QT_TRANSLATE_NOOP("plasmazones", "Expand Column"));
        add(kIdScrollCycleWindowHeight, kScrollingCategory.source, 10, "scrolling", kModeNameContext);
        add(kIdScrollIncreaseWindowHeight, kScrollingCategory.source, 10, "scrolling", kModeNameContext);
        add(kIdScrollDecreaseWindowHeight, kScrollingCategory.source, 10, "scrolling", kModeNameContext);
        add(kIdScrollResetWindowHeights, kScrollingCategory.source, 10, "scrolling", kModeNameContext);
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
        return {QT_TRANSLATE_NOOP("plasmazones", "Layouts"), 1, "all"};
    }
    if (id.startsWith(QLatin1String(kSnapToZonePrefix))) {
        // Every engine implements moveFocusedToPosition: zone N in
        // snapping, layout slot N in tiling, visible tile slot N in
        // scrolling. Only 1 to 9 have shortcuts, so a scrolling strip with
        // more visible tiles than that numbers them all but leaves the ones
        // past 9 with no digit that reaches them.
        return {QT_TRANSLATE_NOOP("plasmazones", "Snap to Zone"), 3, "all"};
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
/// rejects split == 0 (a sequence starting with '+') before it ever asks for
/// the token, where the producer happily tokenizes one. That case cannot
/// wrongly compress — it only declines to — so the asymmetry is left alone.
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
        row.insert(QStringLiteral("category"), PhosphorI18n::tr(meta.category, meta.categoryDisambiguation));
        row.insert(QStringLiteral("categoryOrder"), meta.categoryOrder);
        row.insert(QStringLiteral("triggers"), triggers);
        row.insert(QStringLiteral("assigned"), !triggers.isEmpty());
        row.insert(QStringLiteral("mode"), QString::fromLatin1(meta.mode));
        rows.push_back(row);
    }

    // ─── Family compression ────────────────────────────────────────────────
    // The numbered slot families (9 rows each) and the directional quads
    // (4 rows each) dominate the sheet as walls of near-identical lines.
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
    // still lands on its structural token.
    const auto lastKeyOf = [](const QString& sequence) {
        // Normalize to PortableText first, exactly as the row builder above
        // does to the LIVE triggers before the compare. The defaults are
        // authored in config spelling, and a spelling difference alone would
        // silently stop the pair collapsing.
        const QKeySequence parsed(sequence);
        return lastKeyToken(parsed.isEmpty() ? sequence : parsed.toString(QKeySequence::PortableText));
    };
    // Both members' key tokens joined for the merged chip. The digit family
    // reads as a range and the directional one as a class name; a bare
    // space-joined pair instead read as one chord, so the alternatives are
    // separated the way the row labels separate them.
    const auto scrollPair = [&lastKeyOf](const char* firstId, const QString& firstDefault, const char* secondId,
                                         const QString& secondDefault, const QString& label) {
        const QString firstKey = lastKeyOf(firstDefault);
        const QString secondKey = lastKeyOf(secondDefault);
        return FamilySpec{{QString::fromLatin1(firstId), QString::fromLatin1(secondId)},
                          {firstKey, secondKey},
                          label,
                          firstKey + QStringLiteral(" / ") + secondKey};
    };
    const QStringList arrowTokens{QStringLiteral("Left"), QStringLiteral("Right"), QStringLiteral("Up"),
                                  QStringLiteral("Down")};
    const QString arrowsTail = PhosphorI18n::tr("Arrows");
    QStringList digitTokens;
    QStringList quickLayoutIds;
    QStringList snapToZoneIds;
    for (int i = 0; i < 9; ++i) {
        digitTokens.append(QString::number(i + 1));
        quickLayoutIds.append(quickLayoutId(i));
        snapToZoneIds.append(snapToZoneId(i));
    }
    // One spelling of the digit range everywhere: the chip token and the row
    // labels used to disagree ("1…9" against "1-9") for no reason a reader
    // could act on.
    const QString digitRange = QStringLiteral("1-9");
    const QVector<FamilySpec> families = {
        {quickLayoutIds, digitTokens, PhosphorI18n::tr("Apply Layout 1-9"), digitRange},
        // "Zone", not "Snap to Zone": the group heading already carries that,
        // and the digits address a zone in snapping, a layout slot in tiling
        // and a visible tile in scrolling, so the row itself stays out of any
        // one mode's vocabulary.
        {snapToZoneIds, digitTokens, PhosphorI18n::tr("Zone 1-9"), digitRange},
        {{QString::fromLatin1(kIdMoveWindowLeft), QString::fromLatin1(kIdMoveWindowRight),
          QString::fromLatin1(kIdMoveWindowUp), QString::fromLatin1(kIdMoveWindowDown)},
         arrowTokens,
         PhosphorI18n::tr("Move Window"),
         arrowsTail},
        {{QString::fromLatin1(kIdFocusZoneLeft), QString::fromLatin1(kIdFocusZoneRight),
          QString::fromLatin1(kIdFocusZoneUp), QString::fromLatin1(kIdFocusZoneDown)},
         arrowTokens,
         PhosphorI18n::tr("Focus Zone"),
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
        // The scrolling family's natural pairs collapse the same way the
        // directional quads do, trimming the 20-row Scrolling group.
        scrollPair(kIdScrollConsumeWindow, ConfigDefaults::scrollingConsumeWindowShortcut(), kIdScrollExpelWindow,
                   ConfigDefaults::scrollingExpelWindowShortcut(), PhosphorI18n::tr("Consume / Expel Window")),
        scrollPair(kIdScrollConsumeOrExpelLeft, ConfigDefaults::scrollingConsumeOrExpelLeftShortcut(),
                   kIdScrollConsumeOrExpelRight, ConfigDefaults::scrollingConsumeOrExpelRightShortcut(),
                   PhosphorI18n::tr("Consume or Expel Left / Right")),
        scrollPair(kIdScrollIncreaseColumnWidth, ConfigDefaults::scrollingIncreaseColumnWidthShortcut(),
                   kIdScrollDecreaseColumnWidth, ConfigDefaults::scrollingDecreaseColumnWidthShortcut(),
                   PhosphorI18n::tr("Adjust Column Width")),
        scrollPair(kIdScrollIncreaseWindowHeight, ConfigDefaults::scrollingIncreaseWindowHeightShortcut(),
                   kIdScrollDecreaseWindowHeight, ConfigDefaults::scrollingDecreaseWindowHeightShortcut(),
                   PhosphorI18n::tr("Adjust Window Height")),
    };

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
            // read the same way the merged chip's producer reads it.
            if (split <= 0 || lastKeyToken(seq) != family.expectedLastTokens[m]) {
                compressible = false;
                break;
            }
            const QString prefix = seq.left(split);
            if (m == 0) {
                sharedPrefix = prefix;
            } else if (prefix != sharedPrefix) {
                compressible = false;
            }
        }
        if (!compressible) {
            continue;
        }
        const int firstIdx = rowIndexById.value(family.ids.first());
        // The merged row keeps the FIRST member's mode and category and drops
        // the rest, so a family whose members disagree on either would hide
        // working rows behind the first member's mode filter or file them
        // under the wrong heading. Every current family is uniform on both;
        // this pins that as a requirement of the spec rather than a
        // coincidence of the table.
        // Release pair for the Q_ASSERT: a mixed family still merges (the
        // consequence is a row filed under the first member's mode, cosmetic)
        // but must not do so silently.
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
        row.insert(QStringLiteral("triggers"), QStringList{sharedPrefix + QLatin1Char('+') + family.tailToken});
        for (int m = 1; m < family.ids.size(); ++m) {
            removedIndices.insert(rowIndexById.value(family.ids[m]));
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
