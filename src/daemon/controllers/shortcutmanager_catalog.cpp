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

#include "core/platform/logging.h"
#include "phosphor_i18n.h"

#include <PhosphorShortcuts/Registry.h>

#include <QHash>
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
// Mode classification contract (maintainer-decided):
//  - directional move/focus/swap are generic navigation → all modes
//  - zone-centric ops (snap-to-zone slots, empty-zone push, restore size,
//    cycle/rotate within zones) are hard no-ops off snapping → snapping
//  - master-stack ops are hard no-ops off autotile → autotile
//  - column/strip ops (consume/expel, column widths, tab display) are hard
//    no-ops off scrolling → scrolling
//  - toggle_autotile is the doorway INTO autotile → all modes, always shown
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
        // The (source, comment) pair is extracted through osd.cpp's live
        // PhosphorI18n::tr("Scrolling", "tiling mode name") call, so no
        // separate extraction marker is needed here.
        constexpr const char* kModeNameContext = "tiling mode name";
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
        add(kIdRestoreWindowSize, QT_TRANSLATE_NOOP("plasmazones", "Snap to Zone"), 3, "snapping");
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
        add(kIdRotateWindowsCW, QT_TRANSLATE_NOOP("plasmazones", "Windows"), 4, "snapping", nullptr,
            QT_TRANSLATE_NOOP("plasmazones", "Rotate Clockwise"));
        add(kIdRotateWindowsCCW, QT_TRANSLATE_NOOP("plasmazones", "Windows"), 4, "snapping", nullptr,
            QT_TRANSLATE_NOOP("plasmazones", "Rotate Counterclockwise"));
        add(kIdCycleWindowForward, QT_TRANSLATE_NOOP("plasmazones", "Windows"), 4, "snapping", nullptr,
            QT_TRANSLATE_NOOP("plasmazones", "Cycle Forward in Zone"));
        add(kIdCycleWindowBackward, QT_TRANSLATE_NOOP("plasmazones", "Windows"), 4, "snapping", nullptr,
            QT_TRANSLATE_NOOP("plasmazones", "Cycle Backward in Zone"));
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
        add(kIdScrollFocusColumnFirst, "Scrolling", 10, "scrolling", kModeNameContext);
        add(kIdScrollFocusColumnLast, "Scrolling", 10, "scrolling", kModeNameContext);
        add(kIdScrollMoveColumnToFirst, "Scrolling", 10, "scrolling", kModeNameContext);
        add(kIdScrollMoveColumnToLast, "Scrolling", 10, "scrolling", kModeNameContext);
        add(kIdScrollConsumeWindow, "Scrolling", 10, "scrolling", kModeNameContext,
            QT_TRANSLATE_NOOP("plasmazones", "Consume Window"));
        add(kIdScrollExpelWindow, "Scrolling", 10, "scrolling", kModeNameContext,
            QT_TRANSLATE_NOOP("plasmazones", "Expel Window"));
        add(kIdScrollConsumeOrExpelLeft, "Scrolling", 10, "scrolling", kModeNameContext);
        add(kIdScrollConsumeOrExpelRight, "Scrolling", 10, "scrolling", kModeNameContext);
        add(kIdScrollCenterColumn, "Scrolling", 10, "scrolling", kModeNameContext);
        add(kIdScrollToggleColumnTabbed, "Scrolling", 10, "scrolling", kModeNameContext);
        add(kIdScrollCycleColumnWidth, "Scrolling", 10, "scrolling", kModeNameContext,
            QT_TRANSLATE_NOOP("plasmazones", "Cycle Column Width"));
        add(kIdScrollCycleColumnWidthBack, "Scrolling", 10, "scrolling", kModeNameContext,
            QT_TRANSLATE_NOOP("plasmazones", "Cycle Column Width Back"));
        add(kIdScrollIncreaseColumnWidth, "Scrolling", 10, "scrolling", kModeNameContext);
        add(kIdScrollDecreaseColumnWidth, "Scrolling", 10, "scrolling", kModeNameContext);
        add(kIdScrollMaximizeColumn, "Scrolling", 10, "scrolling", kModeNameContext);
        add(kIdScrollExpandColumn, "Scrolling", 10, "scrolling", kModeNameContext,
            QT_TRANSLATE_NOOP("plasmazones", "Expand Column"));
        add(kIdScrollCycleWindowHeight, "Scrolling", 10, "scrolling", kModeNameContext);
        add(kIdScrollIncreaseWindowHeight, "Scrolling", 10, "scrolling", kModeNameContext);
        add(kIdScrollDecreaseWindowHeight, "Scrolling", 10, "scrolling", kModeNameContext);
        add(kIdScrollResetWindowHeights, "Scrolling", 10, "scrolling", kModeNameContext);
        return m;
    }();

    const auto it = kMeta.constFind(id);
    if (it != kMeta.constEnd()) {
        return *it;
    }
    // Indexed slot families are prefix-keyed, not enumerated above.
    if (id.startsWith(QLatin1String("quick_layout_"))) {
        return {QT_TRANSLATE_NOOP("plasmazones", "Layouts"), 1, "all"};
    }
    if (id.startsWith(QLatin1String("snap_to_zone_"))) {
        return {QT_TRANSLATE_NOOP("plasmazones", "Snap to Zone"), 3, "snapping"};
    }
    // A shortcut added to the table without catalog metadata still shows up
    // (miscategorised beats invisible), and the log points at the fix.
    qCWarning(lcShortcuts) << "cheatsheet: no catalog metadata for shortcut id" << id;
    return {QT_TRANSLATE_NOOP("plasmazones", "Other"), 99, "all"};
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
    // chip is a range token ("1…9") or "Arrows". Any deviation — a member
    // unassigned, a rebind off-pattern — falls back to the individual rows,
    // because a compressed row would then lie about what the keys do.
    using FamilySpec = CheatsheetFamily;
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
    const QVector<FamilySpec> families = {
        {quickLayoutIds, digitTokens, PhosphorI18n::tr("Apply Layout 1-9"), QStringLiteral("1…9")},
        {snapToZoneIds, digitTokens, PhosphorI18n::tr("Snap to Zone 1-9"), QStringLiteral("1…9")},
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
        {{QString::fromLatin1(kIdScrollConsumeWindow), QString::fromLatin1(kIdScrollExpelWindow)},
         {QStringLiteral(";"), QStringLiteral("'")},
         PhosphorI18n::tr("Consume / Expel Window"),
         QStringLiteral("; '")},
        {{QString::fromLatin1(kIdScrollConsumeOrExpelLeft), QString::fromLatin1(kIdScrollConsumeOrExpelRight)},
         {QStringLiteral("U"), QStringLiteral("O")},
         PhosphorI18n::tr("Consume or Expel Left / Right"),
         QStringLiteral("U O")},
        {{QString::fromLatin1(kIdScrollIncreaseColumnWidth), QString::fromLatin1(kIdScrollDecreaseColumnWidth)},
         {QStringLiteral("="), QStringLiteral("-")},
         PhosphorI18n::tr("Adjust Column Width"),
         QStringLiteral("= -")},
        {{QString::fromLatin1(kIdScrollIncreaseWindowHeight), QString::fromLatin1(kIdScrollDecreaseWindowHeight)},
         {QStringLiteral("PgUp"), QStringLiteral("PgDown")},
         PhosphorI18n::tr("Adjust Window Height"),
         QStringLiteral("PgUp PgDown")},
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
        // expectedLastTokens out of bounds below. All current families are
        // 9/9 or 4/4 — this guards the table against a future bad entry.
        Q_ASSERT(family.ids.size() == family.expectedLastTokens.size());
        if (family.ids.size() != family.expectedLastTokens.size()) {
            qCWarning(lcShortcuts) << "cheatsheet: family spec size mismatch for" << family.combinedLabel
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
            if (split <= 0 || seq.mid(split + 1) != family.expectedLastTokens[m]) {
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
