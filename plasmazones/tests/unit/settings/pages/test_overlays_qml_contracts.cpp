// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_overlays_qml_contracts.cpp
 * @brief Contracts between the Appearance → Overlays QML and its controller,
 *        pinned by parsing the QML source itself.
 *
 * The same class of contract test as test_animations_qml_contracts, and for
 * the same reason: QML resolves names at runtime and the settings app has no
 * QML harness, so a renamed invokable is a silent TypeError and a dead card
 * rather than a build failure. These pages were renamed wholesale when the
 * assignment surface moved out of Snapping, which is exactly the change that
 * leaves one stale call site behind.
 *
 * Two contracts live entirely in QML and so cannot be observed by driving the
 * controller from C++:
 *
 *   - a pending debounced parameter write is dropped whenever the node it was
 *     computed against changed, compared on CONTENT. Comparing only the
 *     override's existence or the shader id misses a params-only revert, which
 *     is what a page Reset or Discard performs, and lets the flush re-stage the
 *     reverted value ~200ms after the page reported clean;
 *   - a card refreshes on a single-node change only when it is that node, or
 *     when the baseline moved and the card has no override of its own.
 */

#include <QTest>

#include <QFile>
#include <QMetaMethod>
#include <QMetaObject>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

#include "settings/pages/overlayspagecontroller.h"

using namespace PlasmaZones;

namespace {

QString readSource(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QString src = QString::fromUtf8(f.readAll());
    // A call that survives only in prose is not a caller.
    static const QRegularExpression blockCommentRe(QStringLiteral("/\\*.*?\\*/"),
                                                   QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression lineCommentRe(QStringLiteral("(?<![:\"'])//[^\n]*"));
    src.remove(blockCommentRe);
    src.remove(lineCommentRe);
    return src;
}

/// The QML file's body with whitespace collapsed, so the assertions below
/// survive a qmlformat re-wrap of a long condition.
QString flattened(const QString& src)
{
    static const QRegularExpression wsRe(QStringLiteral("\\s+"));
    return QString(src).replace(wsRe, QStringLiteral(" "));
}

} // namespace

class TestOverlaysQmlContracts : public QObject
{
    Q_OBJECT

private:
    static QString cardPath()
    {
        return QStringLiteral(P_SOURCE_DIR "/src/settings/qml/pages/overlays/OverlayShaderAssignmentCard.qml");
    }

private Q_SLOTS:
    /// Every `bridge.<name>` the overlays QML calls must exist on
    /// OverlaysPageController. This is what catches a half-finished rename.
    /// Two files, not the whole directory: the appearance and library pages
    /// bind their bridge straight through to a shared component and make no
    /// `bridge.<name>` calls of their own, and the sets page reaches its
    /// store through setsBridge, which the C++ suite covers.
    void everyBridgeCallFromTheOverlaysQmlIsReachable()
    {
        const QStringList files{
            cardPath(),
            QStringLiteral(P_SOURCE_DIR "/src/settings/qml/pages/overlays/OverlaysAssignmentsPage.qml"),
        };
        QSet<QString> used;
        for (const QString& path : files) {
            const QString src = readSource(path);
            QVERIFY2(!src.isEmpty(), qPrintable(QStringLiteral("cannot read ") + path));
            // Both spellings: the card holds the controller as `bridge`, the
            // page reaches it as `page.bridge`.
            static const QRegularExpression callRe(QStringLiteral("\\bbridge\\.([A-Za-z_][A-Za-z0-9_]*)"));
            auto it = callRe.globalMatch(src);
            while (it.hasNext())
                used.insert(it.next().captured(1));
        }
        QVERIFY2(!used.isEmpty(), "scraped no bridge.* names — the card or the property name moved");

        OverlaysPageController c(nullptr, nullptr, nullptr, nullptr);
        const QMetaObject* meta = c.metaObject();
        QStringList unreachable;
        for (const QString& name : used) {
            const QByteArray raw = name.toUtf8();
            if (meta->indexOfProperty(raw.constData()) >= 0)
                continue;
            bool found = false;
            for (int i = 0; i < meta->methodCount() && !found; ++i)
                found = meta->method(i).name() == raw;
            if (!found)
                unreachable.append(name);
        }
        std::sort(unreachable.begin(), unreachable.end());
        QVERIFY2(unreachable.isEmpty(),
                 qPrintable(QStringLiteral("The overlays QML calls these on the bridge, but "
                                           "OverlaysPageController lacks them: %1")
                                .arg(unreachable.join(QStringLiteral(", ")))));
    }

    /// The pending-write drop must be gated on the node's CONTENT.
    ///
    /// Pinning the comparison rather than the outcome, because the outcome
    /// needs a running QML engine. The failure this guards against is precise:
    /// a Reset or Discard that reverts only parameters leaves both the
    /// override's existence and the shader id unchanged, so a guard written in
    /// terms of either one alone never fires, and the debounce flush re-stages
    /// the reverted value after the page has already reported clean.
    void aPendingParameterWriteIsDroppedOnAnyNodeChangeNotJustAShaderSwitch()
    {
        const QString src = flattened(readSource(cardPath()));
        QVERIFY2(!src.isEmpty(), "cannot read OverlayShaderAssignmentCard.qml");

        // refresh() captures the pre-refresh node so it has something to
        // compare against at all.
        QVERIFY2(src.contains(QStringLiteral("var prevEditParams = root._editParams")),
                 "refresh() no longer snapshots the params the pending write was computed against");

        // ...and the drop is gated on that comparison, not only on the id.
        QVERIFY2(src.contains(QStringLiteral("_paramsEqual(prevEditParams, root._editParams)")),
                 "the pending-write drop no longer compares the node's parameters — a params-only revert "
                 "(what Reset and Discard perform) will not drop the pending write");

        // The existence check is NOT redundant with the content one: an
        // external clear whose baseline carries the same shader and parameters
        // is a content no-op, and flushing into it recreates the override the
        // clear just removed.
        QVERIFY2(src.contains(QStringLiteral("wasOverride !== root._hasOverride")),
                 "the pending-write drop no longer notices the override appearing or disappearing — a clear "
                 "into an identical baseline would be re-overridden by the flush");
    }

    /// The zone preview draws the user's wallpaper behind the zones.
    ///
    /// Parsed rather than rendered, like the assertions above: this lives in
    /// the SHARED browser dialog, whose zone pane is the overlay preview.
    ///
    /// It matters because every overlay pack is translucent somewhere — that is
    /// what an overlay is — so over the flat black ground this pane used to
    /// paint, all of them read as far more opaque than they will be in use, and
    /// a pack whose whole point is what shows through cannot be judged at all.
    /// The decoration and animation panes have shown the wallpaper for the same
    /// reason; this one was the odd surface out.
    void theZonePreviewDrawsTheWallpaperBehindTheZones()
    {
        const QString path =
            QStringLiteral(P_SOURCE_DIR "/src/settings/qml/pages/shaders/ShaderBrowserDetailDialog.qml");
        const QString src = flattened(readSource(path));
        QVERIFY2(!src.isEmpty(), "cannot read ShaderBrowserDetailDialog.qml");

        QVERIFY2(src.contains(QStringLiteral("previewController.wallpaperPath()")),
                 "the zone preview no longer resolves a wallpaper path, so it has nothing to draw");
        QVERIFY2(src.contains(QStringLiteral("source: root._zoneWallpaperUrl")),
                 "nothing binds the zone pane's backdrop image to the resolved wallpaper");
        // The backdrop is for EVERY pack. Gating it on the pack's useWallpaper
        // flag would restore the old behaviour for all but a handful of packs,
        // since that flag means "this shader SAMPLES the wallpaper", which is a
        // different feed entirely (livePreviewPane._wallpaperTex).
        //
        // Asserted over the backdrop Image's whole BLOCK rather than against
        // one exact spelling: a re-gate would not be written the way the old
        // code happened to spell it, and it could sit on `visible` or on an
        // enclosing condition just as easily as on `source`. The block is the
        // window from the Image that carries the binding to that Image's close.
        const int bindingAt = src.indexOf(QStringLiteral("source: root._zoneWallpaperUrl"));
        QVERIFY(bindingAt > 0);
        const int blockStart = src.lastIndexOf(QStringLiteral("Image {"), bindingAt);
        const int blockEnd = src.indexOf(QLatin1Char('}'), bindingAt);
        QVERIFY(blockStart > 0 && blockEnd > blockStart);
        const QString block = src.mid(blockStart, blockEnd - blockStart);
        QVERIFY2(!block.contains(QStringLiteral("_shaderInfo")) && !block.contains(QStringLiteral("seWallpaper")),
                 "the backdrop is gated on the pack sampling the wallpaper, but it must be drawn for every pack");
    }

    /// A card must not re-run its full refresh for a single-node change to a
    /// different node. Every parameter commit used to refresh every card on
    /// the page, and each of those re-parsed the tree and re-read the pack
    /// registry.
    void aCardFiltersSingleNodeChangesButStillFollowsTheBaseline()
    {
        const QString src = flattened(readSource(cardPath()));
        QVERIFY2(!src.isEmpty(), "cannot read OverlayShaderAssignmentCard.qml");

        QVERIFY2(src.contains(QStringLiteral("function onShaderProfileChanged(path, wholeTree)")),
                 "the card no longer takes the signal's wholeTree argument, so it cannot tell a "
                 "single-node change from a whole-tree one");
        QVERIFY2(src.contains(QStringLiteral("path === root.assignmentPath")),
                 "the card no longer filters a single-node change against its own path");
        // The baseline arm is the easy half to drop, and dropping it is a
        // correctness bug rather than a performance one: a card with no
        // override resolves THROUGH the baseline, so a baseline change moves
        // what it displays.
        // Anchored to the whole expression, not just "!root._hasOverride":
        // that substring also appears in the pending-write drop higher up, so
        // matching it alone was satisfied by an unrelated line and the arm
        // this slot exists for could be deleted with the suite still green.
        QVERIFY2(src.contains(QStringLiteral("path.length === 0 && !root._hasOverride")),
                 "the card no longer refreshes on a baseline change while inheriting — an inheriting "
                 "card would keep showing the old resolved shader");
    }
};

QTEST_MAIN(TestOverlaysQmlContracts)
#include "test_overlays_qml_contracts.moc"
