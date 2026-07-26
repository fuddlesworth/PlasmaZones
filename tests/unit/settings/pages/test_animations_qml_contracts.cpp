// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_animations_qml_contracts.cpp
 * @brief Contracts between the animations settings QML and its C++ controller,
 *        pinned by parsing the QML source itself.
 *
 * Three things the compiler cannot check, because QML resolves names at
 * runtime and the settings app has no QML test harness:
 *
 *   - every `animationsPage.<name>` the QML calls exists on the controller's
 *     meta-object, so a rename on either side fails here rather than as a
 *     silent no-op in the running app;
 *   - every event path the simple Animations page hosts falls inside the C++
 *     page-scope roots, so a per-page Reset covers what the page shows;
 *   - the Override toggle's ON branch writes nothing, which lives entirely in
 *     QML and so cannot be observed by driving the controller from C++.
 *
 * Split out of `test_animations_page_controller.cpp`, which owns the
 * controller's own behaviour. These slots need `P_SOURCE_DIR` to read the
 * source tree; that is what makes them a separate concern rather than a
 * separate file for size alone.
 */

#include <QTest>

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QMetaMethod>
#include <QRegularExpression>
#include <QSet>

#include "settings/pages/animationpagescope.h"
#include "settings/pages/animationspagecontroller.h"

using namespace PlasmaZones;

namespace {

QString readFile(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(f.readAll());
}

} // namespace

class TestAnimationsQmlContracts : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // ─── QML → controller name reachability ───────────────────────────────

    void everyAnimationsPageCallFromQmlIsReachable()
    {
        static const QStringList kQmlRoots{QStringLiteral(P_SOURCE_DIR "/src/settings/qml"),
                                           QStringLiteral(P_SOURCE_DIR "/kcm")};

        // `<receiver>.<name>`, with or without a call paren. Word-boundary
        // anchored so a longer identifier ending in the receiver name cannot
        // match.
        const auto namesUsedOn = [](const QString& src, const QString& receiver) {
            QSet<QString> names;
            const QRegularExpression re(QStringLiteral("\\b%1\\.([A-Za-z_][A-Za-z0-9_]*)").arg(receiver));
            auto it = re.globalMatch(src);
            while (it.hasNext())
                names.insert(it.next().captured(1));
            return names;
        };

        // The binding must END at `animationsPage`. Without the lookahead this
        // also matched `... : settingsController.animationsPage.userPresets`,
        // aliasing a LIST rather than the controller, and then reported that
        // list's `length` as a missing controller member.
        static const QRegularExpression aliasRe(
            QStringLiteral("property\\s+var\\s+([A-Za-z_][A-Za-z0-9_]*)\\s*:"
                           "\\s*settingsController\\.animationsPage\\s*(?![.A-Za-z0-9_])"));

        QSet<QString> used;
        int aliasesResolved = 0;
        for (const QString& rootDir : kQmlRoots) {
            QDirIterator dirIt(rootDir, QStringList{QStringLiteral("*.qml")}, QDir::Files,
                               QDirIterator::Subdirectories);
            while (dirIt.hasNext()) {
                const QString src = readFile(dirIt.next());
                used.unite(namesUsedOn(src, QStringLiteral("animationsPage")));
                auto aliasIt = aliasRe.globalMatch(src);
                while (aliasIt.hasNext()) {
                    used.unite(namesUsedOn(src, aliasIt.next().captured(1)));
                    ++aliasesResolved;
                }
            }
        }

        // Non-vacuity floors. The direct surface alone is well over a dozen
        // names, and at least one alias exists (RulesPage.qml). A collapse
        // means the scrape broke, not that the call sites went away.
        QVERIFY2(used.size() >= 15,
                 qPrintable(QStringLiteral("scraped only %1 animationsPage names from the QML tree").arg(used.size())));
        QVERIFY2(aliasesResolved >= 1, "no same-file alias was resolved — the alias leg is checking nothing");

        AnimationsPageController c;
        const QMetaObject* meta = c.metaObject();
        QStringList unreachable;
        for (const QString& name : used) {
            const QByteArray raw = name.toUtf8();
            if (meta->indexOfProperty(raw.constData()) >= 0)
                continue;
            // By NAME, not signature: indexOfMethod needs an exact parameter
            // list, and QML reaches these through every overload.
            bool found = false;
            for (int i = 0; i < meta->methodCount() && !found; ++i)
                found = meta->method(i).name() == raw;
            if (!found)
                unreachable.append(name);
        }
        QVERIFY2(unreachable.isEmpty(),
                 qPrintable(QStringLiteral("QML calls these, but they are absent from the meta-object "
                                           "(missing Q_INVOKABLE / Q_PROPERTY): %1")
                                .arg(unreachable.join(QStringLiteral(", ")))));
    }

    // ─── Override toggle no-write latch ───────────────────────────────────

    /// The Override toggle's ON branch is a no-write latch: it opens the timing
    /// editor and writes nothing until a control is actually edited. The old
    /// behaviour committed a snapshot of the inherited values so the derived
    /// toggle state would stick, which silently pinned a copy of the Global
    /// curve and duration the moment the toggle was flipped.
    ///
    /// Scraped rather than exercised, because the branch lives entirely in QML
    /// and calls nothing on the controller — a C++ slot driving the controller
    /// directly would stay green no matter what the branch did. What CAN be
    /// pinned is the property this test actually cares about: that the branch
    /// body reaches no controller method at all.
    void overrideToggleOnBranchCallsNothingOnTheController()
    {
        const QString qmlPath =
            QStringLiteral(P_SOURCE_DIR "/src/settings/qml/pages/animations/AnimationEventCard.qml");
        const QString src = readFile(qmlPath);
        QVERIFY2(!src.isEmpty(), qPrintable(QStringLiteral("could not read ") + qmlPath));

        // Brace-COUNTED, not regex-captured. A lazy `\\{(.*?)\\}\\s*else\\s*\\{`
        // stops at the first nested `} else {`, so a plain `if/else` inside the
        // arm hides everything after it — a direct controller write past that
        // point scans clean while `hasMatch()` still reports success.
        // The handler's parameter name is CAPTURED and back-referenced rather
        // than hardcoded, so renaming it (QML permits any name) does not redden
        // the build for a refactor that changed nothing this slot cares about.
        static const QRegularExpression headRe(
            QStringLiteral("onToggleClicked\\s*:\\s*function\\s*\\(\\s*([A-Za-z_][A-Za-z0-9_]*)\\s*\\)\\s*\\{\\s*"
                           "if\\s*\\(\\s*\\1\\s*\\)\\s*\\{"));
        const QRegularExpressionMatch head = headRe.match(src);
        QVERIFY2(head.hasMatch(),
                 "could not locate the onToggleClicked `if (checked) {` arm in AnimationEventCard.qml");

        int depth = 1;
        int i = head.capturedEnd();
        const int armStart = i;
        for (; i < src.size() && depth > 0; ++i) {
            const QChar c = src.at(i);
            if (c == QLatin1Char('{'))
                ++depth;
            else if (c == QLatin1Char('}'))
                --depth;
        }
        QVERIFY2(depth == 0, "unbalanced braces while scanning the onToggleClicked ON arm");
        const QString arm = src.mid(armStart, i - 1 - armStart);

        // Strip comments: the arm's own comment explains the behaviour by naming
        // the calls it does NOT make, and matching those would fail the test for
        // describing itself.
        //
        // Trailing comments are stripped too, not just whole lines — a
        // `// latch only` after the statement is a comment, and reddening the
        // build for one would make this slot a nuisance rather than a guard.
        // That is only safe while the arm holds no string or template literal,
        // where a `//` would be content rather than a comment, so that is
        // asserted rather than assumed. Block comments are deliberately NOT
        // stripped: one could hide a statement, and the equality below would
        // then fail loudly, which is the right direction to be wrong in.
        QVERIFY2(!arm.contains(QLatin1Char('"')) && !arm.contains(QLatin1Char('\'')) && !arm.contains(QLatin1Char('`')),
                 "the ON arm now contains a string literal — the comment stripper below is only sound without one");
        static const QRegularExpression commentRe(QStringLiteral("//[^\\n]*"));
        QString code = QString(arm).remove(commentRe);

        // Assert what the arm IS, not which receivers it avoids. An
        // exclusion list has to enumerate every route to the controller, and
        // this card reaches it through its own helpers (`root._setOverrideMerged`,
        // `_clearFieldOnAll`, …) as well as directly — the old form missed
        // exactly those, which is the shape of the regression being pinned.
        static const QRegularExpression wsRe(QStringLiteral("\\s+"));
        code = code.replace(wsRe, QStringLiteral(" ")).trimmed();

        QCOMPARE(code, QStringLiteral("root._editingTiming = true;"));
    }

    // ─── Simple-page scope contract ───────────────────────────────────────

    /// Every event card on AnimationsSimplePage.qml must fall inside the
    /// `animations-simple` scope in animationpagescope.cpp. That scope drives
    /// the page's Reset, Discard and dirty walk, so a card whose path is not
    /// under one of the scope's roots is silently exempt from all three: the
    /// user edits it, the page reports itself clean, and Reset leaves it set.
    ///
    /// Mirror paths are held to the same contract. A mirror receives every
    /// write the primary does (AnimationEventCard's group writers loop
    /// `_writePaths`), so it is exactly as dependent on the scope's roots: a
    /// mirror declared outside them would be edited by the page, reported clean
    /// by it, and survive its Reset.
    ///
    /// The two lists were previously held together by a comment alone. This
    /// reads the QML as TEXT (no Qt Quick engine, no page construction) and
    /// pulls the eventPath and mirrorPaths literals straight out of the
    /// eventModel, so adding a card or a mirror there without widening the
    /// scope fails here.
    ///
    /// Scope limit, stated plainly: the parse sees `"eventPath": "…"` and
    /// `"mirrorPaths": [ … ]` string literals only. A path that came from a JS
    /// expression or a property reference would not be seen, and the page has
    /// never used one. The count guards below are what stop a rewrite in that
    /// style from turning this into a test that asserts nothing.
    void simpleScopeCoversEverySimplePageCard()
    {
        const QString qmlPath =
            QStringLiteral(P_SOURCE_DIR "/src/settings/qml/pages/animations/AnimationsSimplePage.qml");
        const QString src = readFile(qmlPath);
        QVERIFY2(!src.isEmpty(), qPrintable(QStringLiteral("could not read ") + qmlPath));

        static const QRegularExpression re(QStringLiteral("\"eventPath\"\\s*:\\s*\"([^\"]+)\""));
        QStringList cardPaths;
        auto it = re.globalMatch(src);
        while (it.hasNext())
            cardPaths.append(it.next().captured(1));

        // The page hosts five grouped cards today. A drop below that means the
        // regex or the file stopped matching rather than that a card was
        // removed, and the loop below would then pass vacuously.
        QVERIFY2(cardPaths.size() >= 5,
                 qPrintable(QStringLiteral("parsed only %1 eventPath literals from AnimationsSimplePage.qml")
                                .arg(cardPaths.size())));

        // Mirrors: pull each `"mirrorPaths": [...]` array body, then the string
        // literals inside it. Checked against the same scope and reported
        // through the same list as the primaries, since the consequence of an
        // out-of-scope mirror is identical.
        static const QRegularExpression mirrorArrayRe(QStringLiteral("\"mirrorPaths\"\\s*:\\s*\\[([^\\]]*)\\]"));
        static const QRegularExpression mirrorEntryRe(QStringLiteral("\"([^\"]+)\""));
        QStringList mirrorPaths;
        auto arrayIt = mirrorArrayRe.globalMatch(src);
        while (arrayIt.hasNext()) {
            const QString body = arrayIt.next().captured(1);
            auto entryIt = mirrorEntryRe.globalMatch(body);
            while (entryIt.hasNext())
                mirrorPaths.append(entryIt.next().captured(1));
        }

        // One mirror on the page today (the combined opened & closed card). Its
        // own non-vacuity floor, so a rewrite that stopped matching mirrors
        // fails here rather than silently checking the primaries alone.
        QVERIFY2(mirrorPaths.size() >= 1,
                 qPrintable(QStringLiteral("parsed only %1 mirrorPaths literals from AnimationsSimplePage.qml")
                                .arg(mirrorPaths.size())));
        cardPaths.append(mirrorPaths);

        const AnimationPageScope scope = animationPageScope(QStringLiteral("animations-simple"));
        QCOMPARE(scope.kind, AnimationPageScope::EventSubtree);
        // Collected rather than QVERIFY'd per row: a QVERIFY failure aborts the
        // slot, so the first out-of-scope card would hide every other one and
        // widening the scope would turn into a one-at-a-time hunt.
        QStringList outOfScope;
        for (const QString& path : cardPaths) {
            if (!animationPathInScope(path, scope))
                outOfScope.append(path);
        }
        QVERIFY2(outOfScope.isEmpty(),
                 qPrintable(QStringLiteral("simple-page cards outside the animations-simple scope: ")
                            + outOfScope.join(QLatin1String(", "))));
    }
};

QTEST_MAIN(TestAnimationsQmlContracts)
#include "test_animations_qml_contracts.moc"
