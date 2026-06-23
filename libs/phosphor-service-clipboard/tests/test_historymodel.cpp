// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
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
        m_data.clear();
        Q_EMIT selectionChanged(QStringList());
    }

private:
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
    void receive(const QString& mimeType) override
    {
        m_queue.append({mimeType, m_data.value(mimeType)});
    }

    void pushSelection(const QStringList& mimeTypes, const QMap<QString, QByteArray>& data)
    {
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
    void historyChangedFiresOnMutations();
    void setEntriesTrimsWithoutResave();
    void roleNamesAndRolesMap();
    void previewTruncatesLongText();
    void dedupDistinguishesMimeType();
    void prefersImageFallback();
    void removeAtOutOfRangeIsNoOp();
    void setMaxEntriesBoundaries();
    void dataWithInvalidIndexIsEmpty();
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

void ClipboardHistoryModelTest::historyChangedFiresOnMutations()
{
    FakeClipboardSource source;
    ClipboardHistoryModel model;
    model.setSource(&source);
    QSignalSpy historySpy(&model, &ClipboardHistoryModel::historyChanged);

    source.pushText(utf8("a")); // new capture
    QCOMPARE(historySpy.count(), 1);
    source.pushText(utf8("b")); // new capture
    QCOMPARE(historySpy.count(), 2);
    source.pushText(utf8("a")); // dedup move-to-front still changes persisted order
    QCOMPARE(historySpy.count(), 3);

    model.removeAt(0); // removal
    QCOMPARE(historySpy.count(), 4);
    model.clear(); // clear
    QCOMPARE(historySpy.count(), 5);
}

void ClipboardHistoryModelTest::setEntriesTrimsWithoutResave()
{
    ClipboardHistoryModel model;
    model.setMaxEntries(2);
    QSignalSpy historySpy(&model, &ClipboardHistoryModel::historyChanged);
    QSignalSpy countSpy(&model, &ClipboardHistoryModel::countChanged);

    QList<ClipboardEntry> seed;
    for (int i = 0; i < 5; ++i) {
        ClipboardEntry entry;
        entry.content = QByteArray::number(i);
        entry.mimeType = QStringLiteral("text/plain");
        entry.preview = QString::number(i);
        seed.append(entry);
    }
    model.setEntries(seed);

    QCOMPARE(model.rowCount(), 2); // trimmed to the cap
    QCOMPARE(countSpy.count(), 1); // count went 0 -> 2
    // A load seeds the model but must NOT look like a user mutation, or the host
    // would immediately re-save what it just read.
    QCOMPARE(historySpy.count(), 0);
}

void ClipboardHistoryModelTest::roleNamesAndRolesMap()
{
    FakeClipboardSource source;
    ClipboardHistoryModel model;
    model.setSource(&source);
    source.pushSelection({QStringLiteral("text/plain;charset=utf-8"), QStringLiteral("text/plain")},
                         {{QStringLiteral("text/plain;charset=utf-8"), utf8("hi")}});
    QCOMPARE(model.rowCount(), 1);

    const QHash<int, QByteArray> roles = model.roleNames();
    QCOMPARE(roles.value(ClipboardHistoryModel::PreviewRole), QByteArrayLiteral("preview"));
    QCOMPARE(roles.value(ClipboardHistoryModel::MimeTypeRole), QByteArrayLiteral("mimeType"));
    QCOMPARE(roles.value(ClipboardHistoryModel::OfferedTypesRole), QByteArrayLiteral("offeredTypes"));
    QCOMPARE(roles.value(ClipboardHistoryModel::TimestampRole), QByteArrayLiteral("timestamp"));

    const QModelIndex idx = model.index(0);
    // DisplayRole aliases the preview so a plain view shows something readable.
    QCOMPARE(model.data(idx, Qt::DisplayRole), model.data(idx, ClipboardHistoryModel::PreviewRole));
    QCOMPARE(model.data(idx, ClipboardHistoryModel::OfferedTypesRole).toStringList(),
             (QStringList{QStringLiteral("text/plain;charset=utf-8"), QStringLiteral("text/plain")}));
    QVERIFY(model.data(idx, ClipboardHistoryModel::TimestampRole).toDateTime().isValid());
}

void ClipboardHistoryModelTest::previewTruncatesLongText()
{
    FakeClipboardSource source;
    ClipboardHistoryModel model;
    model.setSource(&source);

    // Multi-line, whitespace-heavy, and longer than the 120-char preview cap.
    const QByteArray longText = QByteArray("line one\nline two\t") + QByteArray(200, 'x');
    source.pushText(longText);
    QCOMPARE(model.rowCount(), 1);

    const QString preview = model.data(model.index(0), ClipboardHistoryModel::PreviewRole).toString();
    QVERIFY(!preview.contains(QLatin1Char('\n'))); // collapsed to a single line
    QVERIFY(!preview.contains(QLatin1Char('\t')));
    QVERIFY(preview.endsWith(QStringLiteral("..."))); // truncated
    QCOMPARE(preview.size(), 123); // 120 chars + "..."
}

void ClipboardHistoryModelTest::dedupDistinguishesMimeType()
{
    FakeClipboardSource source;
    ClipboardHistoryModel model;
    model.setSource(&source);

    // Identical bytes under two different MIME types are distinct entries, not a
    // dedup: dedup keys on (content, mimeType).
    const QByteArray bytes = utf8("same bytes");
    source.pushSelection({QStringLiteral("text/plain;charset=utf-8")},
                         {{QStringLiteral("text/plain;charset=utf-8"), bytes}});
    source.pushSelection({QStringLiteral("image/png")}, {{QStringLiteral("image/png"), bytes}});
    QCOMPARE(model.rowCount(), 2);
}

void ClipboardHistoryModelTest::prefersImageFallback()
{
    FakeClipboardSource source;
    ClipboardHistoryModel model;
    model.setSource(&source);

    // No text and no image/png: a non-png image type is chosen over an unknown
    // type, exercising the generic-image fallback tier of preferredMimeType.
    source.pushSelection(
        {QStringLiteral("application/x-foo"), QStringLiteral("image/jpeg")},
        {{QStringLiteral("image/jpeg"), utf8("JPEGDATA")}, {QStringLiteral("application/x-foo"), utf8("FOO")}});
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0), ClipboardHistoryModel::MimeTypeRole).toString(), QStringLiteral("image/jpeg"));
}

void ClipboardHistoryModelTest::removeAtOutOfRangeIsNoOp()
{
    FakeClipboardSource source;
    ClipboardHistoryModel model;
    model.setSource(&source);
    source.pushText(utf8("only"));

    QSignalSpy countSpy(&model, &ClipboardHistoryModel::countChanged);
    QSignalSpy historySpy(&model, &ClipboardHistoryModel::historyChanged);
    model.removeAt(-1);
    model.removeAt(99);
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(countSpy.count(), 0); // the guard must not emit
    QCOMPARE(historySpy.count(), 0); // nor trigger a re-save
}

void ClipboardHistoryModelTest::setMaxEntriesBoundaries()
{
    FakeClipboardSource source;
    ClipboardHistoryModel model;
    model.setSource(&source);
    source.pushText(utf8("1"));
    source.pushText(utf8("2"));
    QCOMPARE(model.rowCount(), 2);

    QSignalSpy historySpy(&model, &ClipboardHistoryModel::historyChanged);
    QSignalSpy countSpy(&model, &ClipboardHistoryModel::countChanged);

    model.setMaxEntries(0); // a zero cap empties the model
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(historySpy.count(), 1); // eviction changes the persisted history
    QCOMPARE(countSpy.count(), 1);

    model.setMaxEntries(-5); // negative is clamped to zero: a no-op on an empty model
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(historySpy.count(), 1); // nothing evicted, no emit
}

void ClipboardHistoryModelTest::dataWithInvalidIndexIsEmpty()
{
    FakeClipboardSource source;
    ClipboardHistoryModel model;
    model.setSource(&source);
    source.pushText(utf8("x"));

    QVERIFY(!model.data(QModelIndex(), ClipboardHistoryModel::PreviewRole).isValid());
    QVERIFY(!model.data(model.index(5), ClipboardHistoryModel::PreviewRole).isValid()); // row out of range
    QVERIFY(!model.data(model.index(0), Qt::UserRole + 999).isValid()); // unknown role
}

QTEST_GUILESS_MAIN(ClipboardHistoryModelTest)
#include "test_historymodel.moc"
