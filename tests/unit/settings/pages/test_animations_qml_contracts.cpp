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
 *   - the Override toggle's ON branch writes nothing, and its OFF branch closes
 *     the timing editor only when the clear was accepted. Both live entirely in
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
        QSet<QString> aliasNames;
        for (const QString& rootDir : kQmlRoots) {
            QDirIterator dirIt(rootDir, QStringList{QStringLiteral("*.qml")}, QDir::Files,
                               QDirIterator::Subdirectories);
            while (dirIt.hasNext()) {
                // Comments stripped first — LINE and BLOCK alike: this tree's
                // comments (including /** doc blocks */) name controller
                // methods freely, and a comment mentioning a method that was
                // since removed would fail the slot for prose.
                static const QRegularExpression lineCommentRe(QStringLiteral("//[^\\n]*"));
                static const QRegularExpression blockCommentRe(QStringLiteral("/\\*.*?\\*/"),
                                                               QRegularExpression::DotMatchesEverythingOption);
                const QString src = readFile(dirIt.next()).remove(blockCommentRe).remove(lineCommentRe);
                used.unite(namesUsedOn(src, QStringLiteral("animationsPage")));
                // The alias receiver, scraped TREE-WIDE rather than only in the
                // file that declares it. The sole alias today is RulesPage.qml's
                // `animationsController`, and RulesPage.qml never calls anything
                // on it — the real call sites are in ActionParamEditors.qml, via
                // `row.appSettings.animationsController`. Scraping same-file only
                // therefore contributed ZERO names and left three live calls
                // (eventSections, eventLabel, availableShaderEffectsForPath)
                // entirely unchecked, while the floor below still passed.
                used.unite(namesUsedOn(src, QStringLiteral("animationsController")));
                auto aliasIt = aliasRe.globalMatch(src);
                while (aliasIt.hasNext()) {
                    aliasNames.insert(aliasIt.next().captured(1));
                }
            }
        }

        // Non-vacuity floors. The direct surface alone is well over a dozen
        // names, and at least one alias exists (RulesPage.qml). A collapse
        // means the scrape broke, not that the call sites went away.
        QVERIFY2(used.size() >= 15,
                 qPrintable(QStringLiteral("scraped only %1 animationsPage names from the QML tree").arg(used.size())));
        // The names reached through an alias are covered by the tree-wide
        // `animationsController` receiver scraped above — but ONLY because every
        // alias to animationsPage is in fact named `animationsController`. Use
        // the captured alias names to hold that assumption honest: a new alias
        // under a different name would slip past the hardcoded receiver above and
        // leave its call sites unchecked, so assert every captured name is the
        // one the scrape covers. This also floors non-vacuity — an empty set
        // means the alias regex matched nothing.
        QVERIFY2(!aliasNames.isEmpty(), "the alias regex matched nothing — the alias receiver name may have changed");
        for (const QString& alias : aliasNames) {
            QVERIFY2(alias == QLatin1String("animationsController"),
                     qPrintable(QStringLiteral("a new alias '%1' targets animationsPage, but the tree-wide scrape "
                                               "above only covers 'animationsController' — add it there")
                                    .arg(alias)));
        }

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
    void overrideToggleArmsMatchTheirQmlOnlyContracts()
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

        // Brace-count to the arm's closing `}`, then to the `else {` that
        // follows it, then to the OFF arm's closing `}`. Both arms are scanned:
        // the ON arm is the no-write latch, and the OFF arm carries the
        // refusal-gated `_editingTiming` reset, which is pure QML that no
        // controller call can distinguish — reverting it to an unconditional
        // reset is invisible to every other test in the suite.
        const auto scanBlock = [&src](int from, int* endOut) {
            int depth = 1;
            int i = from;
            for (; i < src.size() && depth > 0; ++i) {
                const QChar c = src.at(i);
                if (c == QLatin1Char('{'))
                    ++depth;
                else if (c == QLatin1Char('}'))
                    --depth;
            }
            *endOut = i;
            return depth == 0 ? src.mid(from, i - 1 - from) : QString();
        };

        int onArmEnd = 0;
        const QString arm = scanBlock(head.capturedEnd(), &onArmEnd);
        QVERIFY2(!arm.isEmpty(), "unbalanced braces while scanning the onToggleClicked ON arm");

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
        static const QRegularExpression commentRe(QStringLiteral("//[^\\n]*"));
        QString code = QString(arm).remove(commentRe);
        // Checked AFTER stripping, like the OFF arm's twin below. Checking BEFORE
        // meant an ordinary apostrophe in an ON-arm comment ("the user's intent")
        // failed the build claiming "the ON arm now contains a string literal" —
        // a message that was simply false about the code. Post-strip, a surviving
        // quote of any kind really can only come from a literal.
        QVERIFY2(!code.contains(QLatin1Char('"')) && !code.contains(QLatin1Char('\''))
                     && !code.contains(QLatin1Char('`')),
                 "the ON arm now contains a string literal — the comment stripper above is only sound without one");

        // Assert what the arm IS, not which receivers it avoids. An
        // exclusion list has to enumerate every route to the controller, and
        // this card reaches it through its own helpers (`root._setOverrideMerged`,
        // `_clearFieldOnAll`, …) as well as directly — the old form missed
        // exactly those, which is the shape of the regression being pinned.
        static const QRegularExpression wsRe(QStringLiteral("\\s+"));
        code = code.replace(wsRe, QStringLiteral(" ")).trimmed();

        QCOMPARE(code, QStringLiteral("root._editingTiming = true;"));

        // ── OFF arm ────────────────────────────────────────────────────────
        // Not an equality check: this arm legitimately carries several
        // statements and will grow. What is pinned is the one property a
        // controller-driven test cannot see — that closing the timing editor is
        // GATED on the clear being accepted. Unconditional, a refusal during an
        // async discard leaves the toggle visibly off beside a toast saying it
        // could not be changed.
        // Comments are allowed in the gap: a `// Turning the toggle off clears
        // both axes.` between the arms is an ordinary edit and must not redden
        // the build. `\G` anchors the match at the offset passed to match(),
        // so a later `else {` elsewhere in the file cannot be picked up instead.
        static const QRegularExpression elseRe(QStringLiteral("\\G(?:\\s|//[^\\n]*|/\\*.*?\\*/)*else\\s*\\{"),
                                               QRegularExpression::DotMatchesEverythingOption);
        const QRegularExpressionMatch elseHead = elseRe.match(src, onArmEnd);
        QVERIFY2(elseHead.hasMatch(), "onToggleClicked's ON arm is no longer followed by an `else {`");

        int offArmEnd = 0;
        const QString offArm = scanBlock(elseHead.capturedEnd(), &offArmEnd);
        QVERIFY2(!offArm.isEmpty(), "unbalanced braces while scanning the onToggleClicked OFF arm");
        const QString offCode = QString(offArm).remove(commentRe);

        // Same string-literal guard as the ON arm, and for the same reason:
        // `console.log("root._editingTiming = false")` would be counted as a
        // statement by the reset count below and fail correct code.
        //
        // Checked AFTER stripping, the same way the ON arm's guard is. Both arms'
        // prose legitimately contains apostrophes, so a pre-strip check would
        // reject ordinary comments; post-strip, a surviving quote can only come
        // from a literal. The residual risk is a `//` inside a string literal,
        // which would take the stripper past the closing quote — narrower than
        // the case being guarded, and it fails loudly rather than passing.
        QVERIFY2(!offCode.contains(QLatin1Char('"')) && !offCode.contains(QLatin1Char('\''))
                     && !offCode.contains(QLatin1Char('`')),
                 "the OFF arm now contains a string literal — the reset count below is only sound without one");

        // Whitespace-tolerant, like the two regexes below. A hardcoded-space
        // substring test would reject `_editingTiming=false;` and report that
        // the arm no longer closes the editor, which would be a lie.
        static const QRegularExpression anyResetRe(QStringLiteral("_editingTiming\\s*=\\s*false"));
        const QString offFlat = QString(offCode).replace(wsRe, QStringLiteral(" "));
        QVERIFY2(offFlat.contains(anyResetRe), "the OFF arm no longer closes the timing editor at all");
        // Brace optional: `if (…) { root._editingTiming = false; }` is the same
        // thing, and failing it would accuse the code of not gating when it does.
        // Only a DIRECT reset is recognised. A helper that clears the flag
        // indirectly is out of scope for a textual scrape and would read as
        // "no reset at all".
        static const QRegularExpression gatedResetRe(
            QStringLiteral("if\\s*\\(\\s*root\\._clearOverrideOnAll\\(\\s*\\)\\s*\\)\\s*\\{?\\s*"
                           "root\\._editingTiming\\s*=\\s*false\\s*;"));
        QVERIFY2(gatedResetRe.match(offFlat).hasMatch(),
                 "the OFF arm closes the timing editor WITHOUT gating on _clearOverrideOnAll's result — a refused "
                 "clear would turn the toggle off while the controller toasts that it could not");

        // Exactly one, not merely at-least-one. A gated reset plus a second
        // UNGATED one elsewhere in the arm satisfies the match above while
        // reintroducing the whole defect, and the brace-optional form widened
        // that hole. Counting is what closes it.
        QCOMPARE(offFlat.count(anyResetRe), 1);
    }

    // ─── Revert-latch and refresh-gate contracts (the PR's titled fix) ────

    /// The three QML-only pieces of the "reverted value shows immediately
    /// without collapsing the editor" fix, scraped because no controller-side
    /// test can see them: (1) `_timingEditorOpen` is the disjunction of the
    /// stored state and the session latch; (2) both per-field revert handlers
    /// set the latch GATED on the clear being accepted; (3) refreshFromTree
    /// drops the latch only on an EXTERNAL true→false transition
    /// (`!selfDriven`). Reverting any of the three leaves every other test in
    /// the suite green.
    void revertLatchContractsHoldInTheEventCard()
    {
        const QString qmlPath =
            QStringLiteral(P_SOURCE_DIR "/src/settings/qml/pages/animations/AnimationEventCard.qml");
        QString src = readFile(qmlPath);
        QVERIFY2(!src.isEmpty(), qPrintable(QStringLiteral("could not read ") + qmlPath));
        static const QRegularExpression lineCommentRe(QStringLiteral("//[^\\n]*"));
        static const QRegularExpression blockCommentRe(QStringLiteral("/\\*.*?\\*/"),
                                                       QRegularExpression::DotMatchesEverythingOption);
        src.remove(blockCommentRe);
        src.remove(lineCommentRe);
        static const QRegularExpression wsRe(QStringLiteral("\\s+"));
        const QString flat = QString(src).replace(wsRe, QStringLiteral(" "));

        // (1) The derived open-state is exactly stored-or-latched.
        QVERIFY2(flat.contains(QStringLiteral(
                     "readonly property bool _timingEditorOpen: root.overrideEnabled || root._editingTiming")),
                 "_timingEditorOpen is no longer the overrideEnabled || _editingTiming disjunction");

        // (2) Both revert handlers latch the editor open, gated on the clear
        // being ACCEPTED (a refused clear must not latch anything).
        QVERIFY2(flat.contains(QStringLiteral(
                     "onCurveRevertRequested: { if (root._clearFieldOnAll(\"curve\")) root._editingTiming = true; }")),
                 "the curve revert no longer latches _editingTiming gated on the clear's return");
        QVERIFY2(
            flat.contains(QStringLiteral(
                "onDurationRevertRequested: { if (root._clearFieldOnAll(\"duration\")) root._editingTiming = true; }")),
            "the duration revert no longer latches _editingTiming gated on the clear's return");

        // (3) The refresh path clears the latch only for an EXTERNAL clear.
        QVERIFY2(flat.contains(QStringLiteral(
                     "if (wasEnabled && !root.overrideEnabled && !selfDriven) root._editingTiming = false;")),
                 "refreshFromTree's latch reset is no longer transition- and selfDriven-gated");
    }

    /// ShaderBrowserPage's `_typeCatalog` labels the shader browser's type
    /// axis, one entry per event class. There is no way to derive it from the
    /// C++ SSOT (ProfilePaths::allEventClassTokens) inside QML, so it is a
    /// hand-maintained list — and a class added without an entry here ships
    /// an untranslated raw token sorted last, which is exactly how the strip
    /// class first shipped. Pin the key set against the same literal
    /// vocabulary test_animationshadereffect pins on the C++ side.
    void shaderBrowserTypeCatalogCoversEveryEventClass()
    {
        const QString qmlPath = QStringLiteral(P_SOURCE_DIR "/src/settings/qml/pages/shaders/ShaderBrowserPage.qml");
        const QString src = readFile(qmlPath);
        QVERIFY2(!src.isEmpty(), qPrintable(QStringLiteral("could not read ") + qmlPath));

        const int start = src.indexOf(QStringLiteral("_typeCatalog"));
        QVERIFY2(start >= 0, "_typeCatalog is gone from ShaderBrowserPage.qml");
        const int end = src.indexOf(QStringLiteral("_universalKey"), start);
        QVERIFY2(end > start, "could not find the end of the _typeCatalog block (_universalKey moved?)");
        const QString block = src.mid(start, end - start);

        // Mirrors PhosphorAnimation::ProfilePaths::allEventClassTokens().
        // "universal" is deliberately absent: it is the synthetic order-0
        // bucket the helpers resolve, not a declared class.
        const QStringList classTokens{QStringLiteral("geometry"), QStringLiteral("appearance"),
                                      QStringLiteral("desktop"),  QStringLiteral("move"),
                                      QStringLiteral("strip"),    QStringLiteral("tab")};
        QStringList missing;
        for (const QString& token : classTokens) {
            if (!block.contains(QStringLiteral("\"key\": \"") + token + QLatin1Char('"'))) {
                missing << token;
            }
        }
        QVERIFY2(missing.isEmpty(),
                 qPrintable(QStringLiteral("_typeCatalog has no entry for event class(es): ")
                            + missing.join(QLatin1String(", "))
                            + QStringLiteral(" — such packs get an untranslated badge sorted last")));
    }

    /// _effectTypeKey must bucket a pack by CATALOG order, not by the order
    /// its metadata happens to list its classes. Reading `appliesTo[0]` made
    /// two behaviourally identical hybrids badge and sort into different
    /// buckets, and it was the last consumer anywhere that observed the
    /// declaration order — AnimationShaderEffect::operator== now compares
    /// appliesTo as a set on the strength of that. A regression here would
    /// silently reintroduce the asymmetry, so guard the shape.
    void shaderBrowserTypeKeyIsIndependentOfDeclarationOrder()
    {
        const QString qmlPath = QStringLiteral(P_SOURCE_DIR "/src/settings/qml/pages/shaders/ShaderBrowserPage.qml");
        const QString src = readFile(qmlPath);
        QVERIFY2(!src.isEmpty(), qPrintable(QStringLiteral("could not read ") + qmlPath));

        const int start = src.indexOf(QStringLiteral("function _effectTypeKey"));
        QVERIFY2(start >= 0, "_effectTypeKey is gone from ShaderBrowserPage.qml");
        const int end = src.indexOf(QStringLiteral("function _typeLabel"), start);
        QVERIFY2(end > start, "could not find the end of _effectTypeKey");
        const QString body = src.mid(start, end - start);

        QVERIFY2(body.contains(QStringLiteral("_typeCatalog")),
                 "_effectTypeKey must resolve the bucket through _typeCatalog so the result is independent of "
                 "the pack's declaration order");
        // The bare first-token read is the regression. The catalog-order
        // walk keeps `appliesTo[0]` only as the unknown-token fallback, so
        // require the catalog lookup to come FIRST.
        const int catalogAt = body.indexOf(QStringLiteral("_typeCatalog"));
        const int firstTokenAt = body.indexOf(QStringLiteral("appliesTo[0]"));
        if (firstTokenAt >= 0) {
            QVERIFY2(catalogAt >= 0 && catalogAt < firstTokenAt,
                     "appliesTo[0] may only be the unknown-token fallback, after the catalog-order lookup");
        }
    }

    // ─── Shader-browser bridge route ──────────────────────────────────────

    /// The Animations → Shaders page routes AnimationsPageController through
    /// ShaderBrowserPage as `bridge`, so names the browser tree calls on
    /// `bridge.` must resolve on the controller — except the documented
    /// optional surface (`previewController`), which the detail dialog
    /// null-checks and treats as "no preview pane" (its docstring says so).
    void everyBridgeCallFromTheShaderBrowserIsReachable()
    {
        const QSet<QString> documentedOptional{QStringLiteral("previewController")};

        // Only the files the animations route instantiates: the browser page
        // and its detail dialog. ShaderSetsPage lives in the same directory
        // but is a DIFFERENT route with a set-capable bridge the animations
        // controller never provides, so sweeping the whole directory would
        // demand its API of the wrong controller.
        const QString shadersDir = QStringLiteral(P_SOURCE_DIR "/src/settings/qml/pages/shaders");
        const QStringList routeFiles{shadersDir + QStringLiteral("/ShaderBrowserPage.qml"),
                                     shadersDir + QStringLiteral("/ShaderBrowserCard.qml"),
                                     shadersDir + QStringLiteral("/ShaderBrowserDetailDialog.qml")};
        // Completeness guard against the hardcoded list drifting: any OTHER
        // *.qml in this directory that talks to `bridge.` is a browser-route
        // file the list forgot, and its calls would go unchecked. The
        // ShaderSetsPage route is the documented exception — a different route
        // whose set-capable bridge (applySet/removeSet/updateSet/…) the
        // animations controller never provides. ShaderSetsPage.qml and its
        // ShaderSetCard delegate both belong to it.
        const QSet<QString> setsRouteExclusions{QStringLiteral("ShaderSetsPage.qml"),
                                                QStringLiteral("ShaderSetCard.qml")};
        {
            static const QRegularExpression bridgeUseRe(QStringLiteral("\\bbridge\\."));
            QDirIterator sweep(shadersDir, QStringList{QStringLiteral("*.qml")}, QDir::Files);
            while (sweep.hasNext()) {
                const QString path = sweep.next();
                if (routeFiles.contains(path) || setsRouteExclusions.contains(QFileInfo(path).fileName())) {
                    continue;
                }
                QVERIFY2(!readFile(path).contains(bridgeUseRe),
                         qPrintable(QStringLiteral("%1 uses bridge.* but is not in routeFiles — add it or exclude it "
                                                   "like the ShaderSetsPage route")
                                        .arg(QFileInfo(path).fileName())));
            }
        }
        QSet<QString> used;
        static const QRegularExpression lineCommentRe(QStringLiteral("//[^\\n]*"));
        static const QRegularExpression blockCommentRe(QStringLiteral("/\\*.*?\\*/"),
                                                       QRegularExpression::DotMatchesEverythingOption);
        static const QRegularExpression bridgeRe(QStringLiteral("\\bbridge\\.([A-Za-z_][A-Za-z0-9_]*)"));
        for (const QString& file : routeFiles) {
            const QString src = readFile(file).remove(blockCommentRe).remove(lineCommentRe);
            QVERIFY2(!src.isEmpty(), qPrintable(QStringLiteral("could not read ") + file));
            auto it = bridgeRe.globalMatch(src);
            while (it.hasNext())
                used.insert(it.next().captured(1));
        }
        QVERIFY2(!used.isEmpty(), "scraped no bridge.* names — the browser tree or receiver name moved");

        AnimationsPageController c;
        const QMetaObject* meta = c.metaObject();
        QStringList unreachable;
        for (const QString& name : used) {
            if (documentedOptional.contains(name))
                continue;
            const QByteArray raw = name.toUtf8();
            if (meta->indexOfProperty(raw.constData()) >= 0)
                continue;
            bool found = false;
            for (int i = 0; i < meta->methodCount() && !found; ++i)
                found = meta->method(i).name() == raw;
            if (!found)
                unreachable.append(name);
        }
        QVERIFY2(unreachable.isEmpty(),
                 qPrintable(QStringLiteral("the shader browser calls these on its bridge, but the animations "
                                           "route's controller lacks them: %1")
                                .arg(unreachable.join(QStringLiteral(", ")))));
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
