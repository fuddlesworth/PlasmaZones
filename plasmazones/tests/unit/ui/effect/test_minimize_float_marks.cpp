// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Pure-logic tests for the minimize-float qualifiers
// (tilinghandler/minimizefloatmarks.h): whether a restoring window's frame can
// still be the tile it is about to get. Same header-only reach as
// test_pretile_decisions.

#include <tilinghandler/minimizefloatmarks.h>

#include <QTest>

using PlasmaZones::MinimizeFloatMarks;

class TestMinimizeFloatMarks : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // No record means the window was not minimize-floated through the runtime
    // path, and the peers test must not force the immediate commit for it.
    void peers_noRecord_readsUnchanged()
    {
        const MinimizeFloatMarks marks;
        QVERIFY(!marks.peersChanged(QStringLiteral("a"), {QStringLiteral("b")}));
    }

    void peers_sameSet_unchanged()
    {
        MinimizeFloatMarks marks;
        marks.recordPeers(QStringLiteral("a"), {QStringLiteral("a"), QStringLiteral("b")});
        QVERIFY(!marks.peersChanged(QStringLiteral("a"), {QStringLiteral("b")}));
    }

    // The window itself is excluded on both sides: it is in the tiled set when
    // it minimizes and may or may not be when it restores.
    void peers_selfIgnoredOnBothSides()
    {
        MinimizeFloatMarks marks;
        marks.recordPeers(QStringLiteral("a"), {QStringLiteral("b")});
        QVERIFY(!marks.peersChanged(QStringLiteral("a"), {QStringLiteral("a"), QStringLiteral("b")}));
    }

    // The reported bug: a sole window minimized, others opened since.
    void peers_soleWindowJoined_changed()
    {
        MinimizeFloatMarks marks;
        marks.recordPeers(QStringLiteral("a"), {QStringLiteral("a")});
        QVERIFY(marks.peersChanged(QStringLiteral("a"), {QStringLiteral("b")}));
    }

    void peers_peerClosed_changed()
    {
        MinimizeFloatMarks marks;
        marks.recordPeers(QStringLiteral("a"), {QStringLiteral("a"), QStringLiteral("b")});
        QVERIFY(marks.peersChanged(QStringLiteral("a"), {}));
    }

    void peers_rerecordReplaces()
    {
        MinimizeFloatMarks marks;
        marks.recordPeers(QStringLiteral("a"), {QStringLiteral("b")});
        marks.recordPeers(QStringLiteral("a"), {QStringLiteral("c")});
        QVERIFY(!marks.peersChanged(QStringLiteral("a"), {QStringLiteral("c")}));
    }

    // Untiled and displaced are independent: only untiled opens the pre-tile
    // capture carve-out, so a displaced window must not read as untiled.
    void marks_areIndependent()
    {
        MinimizeFloatMarks marks;
        marks.markDisplaced(QStringLiteral("a"));
        QVERIFY(marks.isDisplaced(QStringLiteral("a")));
        QVERIFY(!marks.isUntiled(QStringLiteral("a")));
        marks.markUntiled(QStringLiteral("b"));
        QVERIFY(marks.isUntiled(QStringLiteral("b")));
        QVERIFY(!marks.isDisplaced(QStringLiteral("b")));
    }

    void remove_dropsEveryMarkForThatWindowOnly()
    {
        MinimizeFloatMarks marks;
        for (const QString& id : {QStringLiteral("a"), QStringLiteral("b")}) {
            marks.markUntiled(id);
            marks.markDisplaced(id);
            marks.recordPeers(id, {QStringLiteral("x")});
        }
        marks.remove(QStringLiteral("a"));
        QVERIFY(!marks.isUntiled(QStringLiteral("a")));
        QVERIFY(!marks.isDisplaced(QStringLiteral("a")));
        QVERIFY(!marks.peersChanged(QStringLiteral("a"), {}));
        QVERIFY(marks.isUntiled(QStringLiteral("b")));
        QVERIFY(marks.isDisplaced(QStringLiteral("b")));
        QVERIFY(marks.peersChanged(QStringLiteral("b"), {}));
    }

    void clear_dropsEverything()
    {
        MinimizeFloatMarks marks;
        marks.markUntiled(QStringLiteral("a"));
        marks.markDisplaced(QStringLiteral("a"));
        marks.recordPeers(QStringLiteral("a"), {QStringLiteral("x")});
        marks.clear();
        QVERIFY(!marks.isUntiled(QStringLiteral("a")));
        QVERIFY(!marks.isDisplaced(QStringLiteral("a")));
        QVERIFY(!marks.peersChanged(QStringLiteral("a"), {}));
    }
};

QTEST_MAIN(TestMinimizeFloatMarks)
#include "test_minimize_float_marks.moc"
