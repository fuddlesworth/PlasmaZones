// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Unit test for the clipboard history model. It drives the model with a fake
// clipboard source that delivers selection / data edges directly, so the whole
// policy (preferred-MIME selection, dedup move-to-front, cap eviction,
// sensitive-selection exclusion, preview generation) is exercised
// deterministically with no live compositor. The fake links nothing from
// phosphor-wayland; the production ClipboardDevice adapter is exercised through
// the CLI demo against a real session.

#include "clipboardhistorymodel.h"
#include "iclipboardsource.h"

#include <QSignalSpy>
#include <QTest>

using namespace PhosphorServiceClipboard;

namespace {

// A test double for IClipboardSource. pushSelection() simulates a clipboard
// change and pre-loads the bytes the model will read back via receive().
class FakeClipboardSource : public IClipboardSource
{
public:
    [[nodiscard]] QStringList mimeTypes() const override
    {
        return m_mimeTypes;
    }
    void receive(const QString& mimeType) override
    {
        // The real client reads asynchronously; this fake delivers synchronously,
        // which exercises every same-thread code path but cannot interleave two
        // reads. The async interleaving (a stale same-MIME delivery) is covered by
        // DeferringFakeClipboardSource below.
        Q_EMIT dataReceived(mimeType, m_data.value(mimeType));
    }

    void pushSelection(const QStringList& mimeTypes, const QMap<QString, QByteArray>& data)
    {
        m_mimeTypes = mimeTypes;
        m_data = data;
        Q_EMIT selectionChanged(mimeTypes);
    }
    void pushText(const QByteArray& text)
    {
        pushSelection({QStringLiteral("text/plain;charset=utf-8")},
                      {{QStringLiteral("text/plain;charset=utf-8"), text}});
    }
    void clearSelection()
    {
        m_mimeTypes.clear();
        m_data.clear();
        Q_EMIT selectionChanged(QStringList());
    }

private:
    QStringList m_mimeTypes;
    QMap<QString, QByteArray> m_data;
};

// A fake whose reads are deferred: receive() captures the bytes to deliver (as
// the real async client would, snapshotting the offer at request time) and
// queues them; flushNext() delivers one, simulating an async completion. This
// lets a test interleave two selections so a read is still in flight when the
// next selection arrives.
class DeferringFakeClipboardSource : public IClipboardSource
{
public:
    [[nodiscard]] QStringList mimeTypes() const override
    {
        return m_mimeTypes;
    }
    void receive(const QString& mimeType) override
    {
        m_queue.append({mimeType, m_data.value(mimeType)});
    }

    void pushSelection(const QStringList& mimeTypes, const QMap<QString, QByteArray>& data)
    {
        m_mimeTypes = mimeTypes;
        m_data = data;
        Q_EMIT selectionChanged(mimeTypes);
    }
    bool flushNext()
    {
        if (m_queue.isEmpty())
            return false;
        const auto [mime, data] = m_queue.takeFirst();
        Q_EMIT dataReceived(mime, data);
        return true;
    }
    [[nodiscard]] int pending() const
    {
        return m_queue.size();
    }

private:
    QStringList m_mimeTypes;
    QMap<QString, QByteArray> m_data;
    QList<QPair<QString, QByteArray>> m_queue;
};

QByteArray utf8(const char* s)
{
    return QByteArray(s);
}

} // namespace

class ClipboardHistoryModelTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void startsEmpty();
    void recordsTextSelection();
    void clearedSelectionKeepsHistory();
    void duplicateMovesToFront();
    void capEvictsOldest();
    void sensitiveSelectionIsDropped();
    void emptyDataIsIgnored();
    void prefersUtf8Text();
    void binaryPreviewIsPlaceholder();
    void removeAndClear();
    void asyncSameMimeReadsAreNotMisattributed();
};

void ClipboardHistoryModelTest::startsEmpty()
{
    ClipboardHistoryModel model;
    QCOMPARE(model.rowCount(), 0);
}

void ClipboardHistoryModelTest::recordsTextSelection()
{
    FakeClipboardSource source;
    ClipboardHistoryModel model;
    model.setSource(&source);
    QSignalSpy countSpy(&model, &ClipboardHistoryModel::countChanged);

    source.pushText(utf8("hello world"));
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0), ClipboardHistoryModel::PreviewRole).toString(), QStringLiteral("hello world"));
    QCOMPARE(model.data(model.index(0), ClipboardHistoryModel::MimeTypeRole).toString(),
             QStringLiteral("text/plain;charset=utf-8"));
    QCOMPARE(countSpy.count(), 1);

    source.pushText(utf8("second"));
    QCOMPARE(model.rowCount(), 2);
    // Most-recent first.
    QCOMPARE(model.data(model.index(0), ClipboardHistoryModel::PreviewRole).toString(), QStringLiteral("second"));
    QCOMPARE(model.data(model.index(1), ClipboardHistoryModel::PreviewRole).toString(), QStringLiteral("hello world"));
}

void ClipboardHistoryModelTest::clearedSelectionKeepsHistory()
{
    FakeClipboardSource source;
    ClipboardHistoryModel model;
    model.setSource(&source);

    source.pushText(utf8("kept"));
    QCOMPARE(model.rowCount(), 1);
    // A cleared clipboard does not erase the history.
    source.clearSelection();
    QCOMPARE(model.rowCount(), 1);
}

void ClipboardHistoryModelTest::duplicateMovesToFront()
{
    FakeClipboardSource source;
    ClipboardHistoryModel model;
    model.setSource(&source);

    source.pushText(utf8("a"));
    source.pushText(utf8("b"));
    source.pushText(utf8("c"));
    QCOMPARE(model.rowCount(), 3); // c, b, a

    QSignalSpy countSpy(&model, &ClipboardHistoryModel::countChanged);
    source.pushText(utf8("a")); // re-copy an existing entry
    QCOMPARE(model.rowCount(), 3); // no new row: deduped
    QCOMPARE(countSpy.count(), 0); // count unchanged
    QCOMPARE(model.data(model.index(0), ClipboardHistoryModel::PreviewRole).toString(), QStringLiteral("a"));
    QCOMPARE(model.data(model.index(1), ClipboardHistoryModel::PreviewRole).toString(), QStringLiteral("c"));
    QCOMPARE(model.data(model.index(2), ClipboardHistoryModel::PreviewRole).toString(), QStringLiteral("b"));
}

void ClipboardHistoryModelTest::capEvictsOldest()
{
    FakeClipboardSource source;
    ClipboardHistoryModel model;
    model.setMaxEntries(3);
    model.setSource(&source);

    source.pushText(utf8("1"));
    source.pushText(utf8("2"));
    source.pushText(utf8("3"));
    source.pushText(utf8("4")); // evicts "1"
    QCOMPARE(model.rowCount(), 3);
    QCOMPARE(model.data(model.index(0), ClipboardHistoryModel::PreviewRole).toString(), QStringLiteral("4"));
    QCOMPARE(model.data(model.index(2), ClipboardHistoryModel::PreviewRole).toString(), QStringLiteral("2"));

    // Lowering the cap evicts immediately.
    model.setMaxEntries(1);
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0), ClipboardHistoryModel::PreviewRole).toString(), QStringLiteral("4"));
}

void ClipboardHistoryModelTest::sensitiveSelectionIsDropped()
{
    FakeClipboardSource source;
    ClipboardHistoryModel model;
    model.setSource(&source);

    // A password-manager hint marks the selection secret: never read, never kept.
    source.pushSelection({QStringLiteral("text/plain;charset=utf-8"), QStringLiteral("x-kde-passwordManagerHint")},
                         {{QStringLiteral("text/plain;charset=utf-8"), utf8("hunter2")}});
    QCOMPARE(model.rowCount(), 0);
}

void ClipboardHistoryModelTest::emptyDataIsIgnored()
{
    FakeClipboardSource source;
    ClipboardHistoryModel model;
    model.setSource(&source);

    // A selection whose read yields no bytes adds nothing.
    source.pushSelection({QStringLiteral("text/plain;charset=utf-8")},
                         {{QStringLiteral("text/plain;charset=utf-8"), QByteArray()}});
    QCOMPARE(model.rowCount(), 0);
}

void ClipboardHistoryModelTest::prefersUtf8Text()
{
    FakeClipboardSource source;
    ClipboardHistoryModel model;
    model.setSource(&source);

    // Offered both an image and utf-8 text: text wins, and that is what is read.
    source.pushSelection({QStringLiteral("image/png"), QStringLiteral("text/plain;charset=utf-8")},
                         {{QStringLiteral("text/plain;charset=utf-8"), utf8("pick me")},
                          {QStringLiteral("image/png"), utf8("PNGDATA")}});
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0), ClipboardHistoryModel::MimeTypeRole).toString(),
             QStringLiteral("text/plain;charset=utf-8"));
    QCOMPARE(model.data(model.index(0), ClipboardHistoryModel::PreviewRole).toString(), QStringLiteral("pick me"));
}

void ClipboardHistoryModelTest::binaryPreviewIsPlaceholder()
{
    FakeClipboardSource source;
    ClipboardHistoryModel model;
    model.setSource(&source);

    source.pushSelection({QStringLiteral("image/png")}, {{QStringLiteral("image/png"), utf8("12345")}});
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0), ClipboardHistoryModel::PreviewRole).toString(),
             QStringLiteral("[image/png, 5 bytes]"));
}

void ClipboardHistoryModelTest::removeAndClear()
{
    FakeClipboardSource source;
    ClipboardHistoryModel model;
    model.setSource(&source);

    source.pushText(utf8("x"));
    source.pushText(utf8("y"));
    QCOMPARE(model.rowCount(), 2);

    model.removeAt(0); // remove "y"
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0), ClipboardHistoryModel::PreviewRole).toString(), QStringLiteral("x"));

    model.clear();
    QCOMPARE(model.rowCount(), 0);
}

void ClipboardHistoryModelTest::asyncSameMimeReadsAreNotMisattributed()
{
    DeferringFakeClipboardSource source;
    ClipboardHistoryModel model;
    model.setSource(&source);

    const QString text = QStringLiteral("text/plain;charset=utf-8");

    // Selection A is read (request issued + queued); before its bytes arrive,
    // selection B replaces it with the SAME MIME type. A naive single-slot
    // pending state would record A's bytes under B's selection and drop B.
    source.pushSelection({text}, {{text, utf8("AAA")}});
    source.pushSelection({text}, {{text, utf8("BBB")}});
    QCOMPARE(model.rowCount(), 0); // both reads deferred; nothing recorded yet
    QCOMPARE(source.pending(), 1); // reads are serialized: only A is outstanding

    QVERIFY(source.flushNext()); // deliver A's bytes
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0), ClipboardHistoryModel::PreviewRole).toString(), QStringLiteral("AAA"));
    QCOMPARE(source.pending(), 1); // model then issued B's read

    QVERIFY(source.flushNext()); // deliver B's bytes
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.data(model.index(0), ClipboardHistoryModel::PreviewRole).toString(), QStringLiteral("BBB"));
    QCOMPARE(model.data(model.index(1), ClipboardHistoryModel::PreviewRole).toString(), QStringLiteral("AAA"));
    QVERIFY(!source.flushNext()); // no stale third read
}

QTEST_GUILESS_MAIN(ClipboardHistoryModelTest)
#include "test_historymodel.moc"
