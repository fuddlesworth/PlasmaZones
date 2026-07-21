// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_search_catalog_tiers.cpp
 * @brief Keeps the search catalogue's advanced-only flags in step with the QML.
 *
 * A row's advanced-mode tier is declared in QML (`advancedOnly: true`, or a
 * `visible:` binding gated on `advancedMode`). The global search index is built
 * in C++ from searchcatalog.cpp before any page is instantiated, so it cannot
 * read that declaration at runtime — the QML anchor registry is per-page and
 * only populates once a page is built. The flag therefore has to be mirrored
 * into the catalogue by hand, and a hand-mirrored fact drifts.
 *
 * This test removes the drift: it parses the QML for anchors that sit inside an
 * advanced-gated block, parses the catalogue for the entries carrying
 * `advancedOnly=`, and fails when the two disagree in either direction. Getting
 * it wrong is user-visible both ways: a missing flag offers a simple-mode search
 * result that reveals a collapsed row, and a spurious one hides a setting the
 * user can actually reach.
 *
 * Scope limit, stated honestly: attribution is per FILE. An anchor declared
 * inside a shared card component (WindowFilterCard's `windowFiltering`) whose
 * tier is set by the HOST page is invisible to this parse, because the
 * declaration and the anchor live in different files. Those pairs are listed in
 * kCrossFileAdvanced below and asserted from that list instead. Adding a shared
 * card with a host-set tier means adding it there too.
 */

#include <QTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QTextStream>

namespace {

/// (pageId, anchor) pairs whose anchor is declared in a shared card component
/// but whose advanced-only tier is set by the hosting page. See the file
/// comment — the per-file parse cannot see these.
const QSet<QString>& crossFileAdvanced()
{
    // window-appearance hosts WindowFilterCard with `advancedOnly: true`;
    // general hosts the same card untiered, so only the former is listed.
    static const QSet<QString> kSet{
        QStringLiteral("window-appearance/windowFiltering"),
        QStringLiteral("window-appearance/excludeTransient"),
        QStringLiteral("window-appearance/minimumWindowWidth"),
        QStringLiteral("window-appearance/minimumWindowHeight"),
    };
    return kSet;
}

QString readAll(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    return QString::fromUtf8(f.readAll());
}

/// Strip line comments only. Block comments are deliberately KEPT: the
/// catalogue marks the flag with an argument annotation (`/*advancedOnly=*/`),
/// so removing block comments would erase the very marker this test reads.
QString stripLineComments(const QString& src)
{
    QString out;
    const QStringList lines = src.split(QLatin1Char('\n'));
    for (const QString& line : lines) {
        // String-aware: a naive indexOf("//") truncates at the "//" inside a
        // URL literal. AboutPage.qml has `u.startsWith("https://") ... ) {`,
        // where that would delete the line's opening brace and leave the
        // caller's brace stack off by one for the entire rest of the file.
        // Escapes are honoured so a literal \" cannot end the span early.
        int cut = -1;
        QChar quote;
        for (int i = 0; i < line.size(); ++i) {
            const QChar c = line.at(i);
            if (!quote.isNull()) {
                if (c == QLatin1Char('\\')) {
                    ++i;
                } else if (c == quote) {
                    quote = QChar();
                }
                continue;
            }
            if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
                quote = c;
            } else if (c == QLatin1Char('/') && i + 1 < line.size() && line.at(i + 1) == QLatin1Char('/')) {
                cut = i;
                break;
            }
        }
        out += (cut >= 0 ? line.left(cut) : line);
        out += QLatin1Char('\n');
    }
    return out;
}

/// Line AND block comments — for the QML side, where a commented-out
/// `advancedOnly:` must not count as a live declaration.
QString stripComments(const QString& src)
{
    QString out = stripLineComments(src);
    out.remove(QRegularExpression(QStringLiteral("/\\*.*?\\*/"), QRegularExpression::DotMatchesEverythingOption));
    return out;
}

struct Block
{
    int start = 0;
    int end = 0;
    QList<int> childStarts;
    QList<int> childEnds;
};

/// Every brace-delimited block, each carrying its direct children's spans so
/// the caller can look at a block's OWN body. Without excluding nested blocks a
/// parent inherits any `advancedOnly` its children declare, which would mark
/// every anchor on the page.
///
/// LIMITATION: braces are counted lexically, including any inside string
/// literals and JS expression bodies. One unbalanced brace in a QML string
/// would shift every block boundary after it and silently mis-attribute
/// anchors in either direction. No parsed file contains one today (verified across all src/settings/qml/*Page.qml), and
/// theParserFindsAdvancedAnchors' per-file assertions would fail loudly if a
/// shift ever collapsed a file's parse. Teach this to skip quoted spans before
/// pointing it at a third file.
QList<Block> parseBlocks(const QString& src)
{
    QList<Block> all;
    QList<Block> stack;
    for (int i = 0; i < src.size(); ++i) {
        const QChar ch = src.at(i);
        if (ch == QLatin1Char('{')) {
            Block b;
            b.start = i;
            stack.append(b);
        } else if (ch == QLatin1Char('}') && !stack.isEmpty()) {
            Block b = stack.takeLast();
            b.end = i;
            if (!stack.isEmpty()) {
                stack.last().childStarts.append(b.start);
                stack.last().childEnds.append(b.end);
            }
            all.append(b);
        }
    }
    return all;
}

QString ownBody(const QString& src, const Block& b)
{
    QString out;
    int prev = b.start;
    for (int i = 0; i < b.childStarts.size(); ++i) {
        out += src.mid(prev, b.childStarts.at(i) - prev);
        prev = b.childEnds.at(i) + 1;
    }
    out += src.mid(prev, b.end - prev);
    return out;
}

bool declaresAdvancedGate(const QString& body)
{
    static const QRegularExpression kProp(QStringLiteral("\\badvancedOnly\\s*:\\s*true"));
    static const QRegularExpression kBinding(QStringLiteral("\\bvisible\\s*:[^\\n]*\\badvancedMode\\b"));
    return body.contains(kProp) || body.contains(kBinding);
}

/// Anchors in @p qmlPath that sit inside an advanced-gated block.
QSet<QString> advancedAnchorsIn(const QString& qmlPath)
{
    const QString src = stripComments(readAll(qmlPath));
    const QList<Block> blocks = parseBlocks(src);

    QList<QPair<int, int>> gated;
    for (const Block& b : blocks) {
        if (declaresAdvancedGate(ownBody(src, b))) {
            gated.append({b.start, b.end});
        }
    }

    QSet<QString> out;
    static const QRegularExpression kAnchor(QStringLiteral("\\bsearchAnchor\\s*:\\s*\"([^\"]+)\""));
    auto it = kAnchor.globalMatch(src);
    while (it.hasNext()) {
        const auto m = it.next();
        const int pos = m.capturedStart();
        for (const auto& g : gated) {
            if (pos > g.first && pos < g.second) {
                out.insert(m.captured(1));
                break;
            }
        }
    }
    return out;
}

/// Map a QML file to the page id whose entries the catalogue registers for it.
/// Only the pages that host advanced-gated anchors need an entry, and
/// everyAdvancedGatedPageIsInTheMap asserts the converse so a third page
/// growing a gate cannot escape catalogueTiersMatchTheQml silently.
const QHash<QString, QString>& qmlFileToPageId()
{
    static const QHash<QString, QString> kMap{
        {QStringLiteral("GeneralPage.qml"), QStringLiteral("general")},
        {QStringLiteral("WindowAppearancePage.qml"), QStringLiteral("window-appearance")},
    };
    return kMap;
}

} // namespace

class TestSearchCatalogTiers : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        m_qmlDir = QStringLiteral(P_SOURCE_DIR "/src/settings/qml");
        m_catalog = QStringLiteral(P_SOURCE_DIR "/src/settings/searchcatalog.cpp");
        QVERIFY2(QFileInfo::exists(m_catalog), qPrintable(m_catalog));
        QVERIFY2(QDir(m_qmlDir).exists(), qPrintable(m_qmlDir));
    }

    /// The parser must actually find something. Without this a regex that
    /// silently stops matching turns the whole test into a vacuous pass.
    void theParserFindsAdvancedAnchors()
    {
        // Per-file rather than a global floor. `total >= 20` stayed green when
        // one file's parse collapsed and another's grew — exactly the
        // half-broken-regex case this exists to catch. Every file in the map is
        // in the map BECAUSE it carries advanced anchors, so each must
        // contribute at least one; that is a derived invariant, not a magic
        // number, and it localises the failure to the file that broke.
        //
        // Deliberately NOT cross-checked against the catalogue's
        // advancedOnly=true row count: those two sets are not in a containment
        // relation. A catalogue row is also advanced-only when it sits on an
        // AdvancedOnly PAGE or comes from crossFileAdvanced(), neither of which
        // shows up as a per-row `advancedOnly` in these two QML files (26 vs 22
        // at the time of writing).
        for (auto it = qmlFileToPageId().cbegin(); it != qmlFileToPageId().cend(); ++it) {
            const int n = advancedAnchorsIn(m_qmlDir + QLatin1Char('/') + it.key()).size();
            QVERIFY2(n > 0,
                     qPrintable(QStringLiteral("no advanced anchors parsed from %1 — the regex or the file's "
                                               "advancedOnly usage changed")
                                    .arg(it.key())));
        }
    }

    void everyAdvancedGatedPageIsInTheMap()
    {
        // qmlFileToPageId is hand-maintained, and catalogueTiersMatchTheQml
        // only compares the files IN it. A third page growing an advanced gate
        // would therefore be skipped entirely, in the "missing" direction,
        // silently. Glob the page files and assert the converse of the map's
        // own comment, so the invariant is checked rather than described.
        const QStringList pages = QDir(m_qmlDir).entryList({QStringLiteral("*Page.qml")}, QDir::Files);
        QVERIFY(!pages.isEmpty());
        QStringList missing;
        for (const QString& file : pages) {
            if (qmlFileToPageId().contains(file)) {
                continue;
            }
            if (!advancedAnchorsIn(m_qmlDir + QLatin1Char('/') + file).isEmpty()) {
                missing << file;
            }
        }
        QVERIFY2(missing.isEmpty(),
                 qPrintable(QStringLiteral("pages with advanced gates but no qmlFileToPageId entry: %1")
                                .arg(missing.join(QStringLiteral(", ")))));
    }

    void catalogueTiersMatchTheQml()
    {
        const QString catalogSrc = stripLineComments(readAll(m_catalog));

        // Every (page, anchor) the catalogue marks advanced-only.
        QSet<QString> flagged;
        static const QRegularExpression kCall(
            QStringLiteral("\\badd(?:Setting|Section)\\(\\s*search\\s*,\\s*QStringLiteral\\(\"([^\"]+)\"\\)\\s*,"
                           "\\s*QStringLiteral\\(\"([^\"]+)\"\\)"));
        auto it = kCall.globalMatch(catalogSrc);
        while (it.hasNext()) {
            const auto m = it.next();
            // Scan to the end of this call and look for the flag.
            int depth = 1;
            int i = m.capturedEnd();
            while (i < catalogSrc.size() && depth > 0) {
                if (catalogSrc.at(i) == QLatin1Char('(')) {
                    ++depth;
                } else if (catalogSrc.at(i) == QLatin1Char(')')) {
                    --depth;
                }
                ++i;
            }
            const QString body = catalogSrc.mid(m.capturedEnd(), i - m.capturedEnd());
            if (body.contains(QLatin1String("advancedOnly=*/true"))) {
                flagged.insert(m.captured(1) + QLatin1Char('/') + m.captured(2));
            }
        }

        // Every (page, anchor) the QML gates behind advanced mode.
        QSet<QString> expected = crossFileAdvanced();
        for (auto pit = qmlFileToPageId().cbegin(); pit != qmlFileToPageId().cend(); ++pit) {
            const QSet<QString> anchors = advancedAnchorsIn(m_qmlDir + QLatin1Char('/') + pit.key());
            for (const QString& a : anchors) {
                expected.insert(pit.value() + QLatin1Char('/') + a);
            }
        }

        // A catalogue entry may legitimately not exist for a given anchor, so
        // only compare against anchors the catalogue actually registers.
        QSet<QString> registered;
        auto rit = kCall.globalMatch(catalogSrc);
        while (rit.hasNext()) {
            const auto m = rit.next();
            registered.insert(m.captured(1) + QLatin1Char('/') + m.captured(2));
        }
        expected.intersect(registered);

        const QSet<QString> missing = expected - flagged;
        const QSet<QString> spurious = flagged - expected;

        for (const QString& k : missing) {
            qWarning() << "advanced in QML but NOT flagged in the catalogue:" << k;
        }
        for (const QString& k : spurious) {
            qWarning() << "flagged in the catalogue but NOT advanced in QML:" << k;
        }

        QVERIFY2(missing.isEmpty(),
                 qPrintable(QStringLiteral("%1 catalogue entries missing advancedOnly").arg(missing.size())));
        QVERIFY2(spurious.isEmpty(),
                 qPrintable(QStringLiteral("%1 catalogue entries flagged in error").arg(spurious.size())));
    }

    /// Every pageId the catalogue registers must be a page the app registers.
    /// SearchController::tierAllows now DROPS entries whose pageId is unknown
    /// to the registry (they produced results that navigated nowhere), so a
    /// stale id here is no longer a dead link — it is a setting that silently
    /// cannot be found at all, with only a runtime warning as evidence. The
    /// static allowlist that used to bound this set was deleted with the
    /// simple/advanced rework and nothing replaced it.
    void everyCataloguePageIdIsRegistered()
    {
        const QString catalogSrc = stripLineComments(readAll(m_catalog));
        const QString registration =
            readAll(QStringLiteral(P_SOURCE_DIR "/src/settings/settingscontroller_pageregistration.cpp"));
        const QString topology =
            readAll(QStringLiteral(P_SOURCE_DIR "/src/settings/settingscontroller_pagetopology.cpp"));
        QVERIFY2(!registration.isEmpty(), "settingscontroller_pageregistration.cpp unreadable");
        QVERIFY2(!topology.isEmpty(), "settingscontroller_pagetopology.cpp unreadable");

        // Slice to validPageNames()'s BODY before matching. Scanning the whole
        // file also picks up the category-id tables and reveal-target maps, so
        // non-navigable ids (placement, snapping, animations, the *-cat
        // headers) would count as known — and a catalogue entry addressed at a
        // category also passes hasPage() at runtime, then navigates into an
        // empty page body. That is the exact dead-result class this catches.
        const int bodyStart = topology.indexOf(QStringLiteral("SettingsController::validPageNames()"));
        QVERIFY2(bodyStart >= 0, "validPageNames() definition not found in pagetopology");
        const int braceStart = topology.indexOf(QLatin1Char('{'), bodyStart);
        const int bodyEnd = topology.indexOf(QStringLiteral("};"), braceStart);
        QVERIFY2(braceStart >= 0 && bodyEnd > braceStart, "could not slice validPageNames() body");
        const QString validBody = topology.mid(braceStart, bodyEnd - braceStart);

        QSet<QString> known;
        static const QRegularExpression kValid(QStringLiteral("QStringLiteral\\(\"([a-z0-9-]+)\"\\)"));
        auto vit = kValid.globalMatch(validBody);
        while (vit.hasNext()) {
            known.insert(vit.next().captured(1));
        }
        // Guard against the slice silently collapsing: validPageNames has ~41
        // entries, so a near-zero count means the parse broke, not that the
        // catalogue is clean.
        QVERIFY2(known.size() > 20,
                 qPrintable(QStringLiteral("validPageNames slice yielded only %1 ids").arg(known.size())));

        static const QRegularExpression kCall(
            QStringLiteral("\\badd(?:Setting|Section)\\(\\s*search\\s*,\\s*QStringLiteral\\(\"([^\"]+)\"\\)"));
        QSet<QString> unregistered;
        // setPageKeywords takes the page id directly. A stale id there costs
        // the page its entire synonym list SILENTLY: the call never reaches
        // tierAllows, so nothing warns and the page just stops answering to
        // its synonyms.
        static const QRegularExpression kKeywords(
            QStringLiteral("\\bsetPageKeywords\\(\\s*QStringLiteral\\(\"([^\"]+)\"\\)"));
        int keywordIds = 0;
        auto kit = kKeywords.globalMatch(catalogSrc);
        while (kit.hasNext()) {
            const QString pageId = kit.next().captured(1);
            ++keywordIds;
            if (!known.contains(pageId)) {
                unregistered.insert(pageId);
            }
        }
        // Same vacuity guard as the id slice: a regex that silently stops
        // matching must fail loudly rather than validate nothing.
        QVERIFY2(keywordIds > 20, // vacuity guard
                 qPrintable(QStringLiteral("setPageKeywords parse yielded only %1 ids").arg(keywordIds)));

        auto it = kCall.globalMatch(catalogSrc);
        while (it.hasNext()) {
            const QString pageId = it.next().captured(1);
            if (!pageId.isEmpty() && !known.contains(pageId)) {
                unregistered.insert(pageId);
            }
        }

        QVERIFY2(
            unregistered.isEmpty(),
            qPrintable(QStringLiteral("catalogue registers entries for unregistered page id(s): %1")
                           .arg(QStringList(unregistered.cbegin(), unregistered.cend()).join(QLatin1String(", ")))));
    }

private:
    QString m_qmlDir;
    QString m_catalog;
};

QTEST_MAIN(TestSearchCatalogTiers)
#include "test_search_catalog_tiers.moc"
