// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "clipboardhistorymodel.h"

#include <QDateTime>

namespace PhosphorServiceClipboard {

namespace {
// The KDE de-facto password-manager hint: when a selection offers this MIME
// type the content is a secret and must not enter the history.
constexpr QLatin1String kPasswordHint{"x-kde-passwordManagerHint"};
constexpr int kPreviewMaxChars = 120;

bool isTextType(const QString& mimeType)
{
    return mimeType.startsWith(QLatin1String("text/")) || mimeType == QLatin1String("UTF8_STRING")
        || mimeType == QLatin1String("STRING") || mimeType == QLatin1String("TEXT");
}
} // namespace

ClipboardHistoryModel::ClipboardHistoryModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

ClipboardHistoryModel::~ClipboardHistoryModel() = default;

void ClipboardHistoryModel::setSource(IClipboardSource* source)
{
    if (m_source == source)
        return;
    if (m_source)
        m_source->disconnect(this);
    m_source = source;

    // A read outstanding against the old source can never complete against the
    // new one; reset the read state machine so the new source starts clean and
    // cannot be wedged by the abandoned read.
    m_reading = false;
    m_selectionChangedWhileReading = false;
    m_latestTypes.clear();
    m_readTypes.clear();
    m_readMime.clear();

    if (m_source) {
        connect(m_source, &IClipboardSource::selectionChanged, this, &ClipboardHistoryModel::onSelectionChanged);
        connect(m_source, &IClipboardSource::dataReceived, this, &ClipboardHistoryModel::onDataReceived);
    }
}

void ClipboardHistoryModel::setMaxEntries(int max)
{
    m_maxEntries = max < 0 ? 0 : max;
    const qsizetype before = m_entries.size();
    enforceCap();
    if (m_entries.size() != before) {
        Q_EMIT countChanged();
        Q_EMIT historyChanged();
    }
}

int ClipboardHistoryModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return m_entries.size();
}

QVariant ClipboardHistoryModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size())
        return {};
    const ClipboardEntry& entry = m_entries.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case PreviewRole:
        return entry.preview;
    case MimeTypeRole:
        return entry.mimeType;
    case OfferedTypesRole:
        return entry.offeredTypes;
    case TimestampRole:
        return entry.timestamp;
    default:
        return {};
    }
}

QHash<int, QByteArray> ClipboardHistoryModel::roleNames() const
{
    return {
        {PreviewRole, QByteArrayLiteral("preview")},
        {MimeTypeRole, QByteArrayLiteral("mimeType")},
        {OfferedTypesRole, QByteArrayLiteral("offeredTypes")},
        {TimestampRole, QByteArrayLiteral("timestamp")},
    };
}

ClipboardEntry ClipboardHistoryModel::entryAt(int row) const
{
    if (row < 0 || row >= m_entries.size())
        return {};
    return m_entries.at(row);
}

QList<ClipboardEntry> ClipboardHistoryModel::entries() const
{
    return m_entries;
}

void ClipboardHistoryModel::setEntries(const QList<ClipboardEntry>& entries)
{
    const qsizetype before = m_entries.size();
    beginResetModel();
    m_entries = entries;
    // m_maxEntries is clamped to >= 0 in setMaxEntries, so the cap always applies.
    if (m_entries.size() > static_cast<qsizetype>(m_maxEntries))
        m_entries.erase(m_entries.begin() + m_maxEntries, m_entries.end());
    endResetModel();
    if (m_entries.size() != before)
        Q_EMIT countChanged();
}

void ClipboardHistoryModel::removeAt(int row)
{
    if (row < 0 || row >= m_entries.size())
        return;
    beginRemoveRows(QModelIndex(), row, row);
    m_entries.removeAt(row);
    endRemoveRows();
    Q_EMIT countChanged();
    Q_EMIT historyChanged();
}

void ClipboardHistoryModel::clear()
{
    if (m_entries.isEmpty())
        return;
    beginResetModel();
    m_entries.clear();
    endResetModel();
    Q_EMIT countChanged();
    Q_EMIT historyChanged();
}

void ClipboardHistoryModel::onSelectionChanged(const QStringList& mimeTypes)
{
    // A cleared selection keeps the history but has nothing to read; a sensitive
    // selection (password manager) is never read or kept. Either way the "latest
    // to capture" becomes empty.
    m_latestTypes = (mimeTypes.isEmpty() || isSensitive(mimeTypes)) ? QStringList() : mimeTypes;

    // Reads are serialized. If one is already outstanding, just remember that the
    // selection moved on; the in-flight read records its own bytes (with its own
    // types) and then picks up the latest. Otherwise start reading now.
    if (m_reading)
        m_selectionChangedWhileReading = true;
    else
        startNextRead();
}

void ClipboardHistoryModel::startNextRead()
{
    m_readTypes = m_latestTypes;
    m_selectionChangedWhileReading = false;
    if (m_readTypes.isEmpty()) {
        m_reading = false;
        return; // nothing to capture (cleared or sensitive selection).
    }
    m_readMime = preferredMimeType(m_readTypes);
    if (m_readMime.isEmpty() || !m_source) {
        // Nothing readable, or no source to read from: stay idle rather than
        // marking a read outstanding that no dataReceived will ever complete.
        m_reading = false;
        return;
    }
    m_reading = true;
    m_source->receive(m_readMime);
}

void ClipboardHistoryModel::onDataReceived(const QString& mimeType, const QByteArray& data)
{
    // Accept only the bytes for the read currently outstanding; a stray delivery
    // is ignored.
    if (!m_reading || mimeType != m_readMime)
        return;
    m_reading = false;

    // Record with the types captured when THIS read was issued, so a selection
    // change mid-read cannot mis-attribute these bytes to a newer selection. An
    // empty/failed read records nothing.
    if (!data.isEmpty())
        recordEntry(data, mimeType, m_readTypes);

    // If the selection moved on while we were reading, capture the latest now.
    if (m_selectionChangedWhileReading)
        startNextRead();
}

void ClipboardHistoryModel::recordEntry(const QByteArray& content, const QString& mimeType,
                                        const QStringList& offeredTypes)
{
    ClipboardEntry entry;
    entry.content = content;
    entry.mimeType = mimeType;
    entry.offeredTypes = offeredTypes;
    entry.preview = makePreview(content, mimeType);
    entry.timestamp = QDateTime::currentDateTime();
    entry.sensitive = false; // sensitive selections never reach here.

    const qsizetype beforeCount = m_entries.size();

    // De-duplicate: an identical capture moves to the front (most-recent) rather
    // than adding a second copy.
    const int existing = indexOfContent(entry.content, entry.mimeType);
    if (existing >= 0) {
        beginRemoveRows(QModelIndex(), existing, existing);
        m_entries.removeAt(existing);
        endRemoveRows();
    }

    beginInsertRows(QModelIndex(), 0, 0);
    m_entries.prepend(entry);
    endInsertRows();

    enforceCap();

    if (m_entries.size() != beforeCount)
        Q_EMIT countChanged();
    // A capture (new or a dedup re-order) always changes the persisted history.
    Q_EMIT historyChanged();
}

int ClipboardHistoryModel::indexOfContent(const QByteArray& content, const QString& mimeType) const
{
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).mimeType == mimeType && m_entries.at(i).content == content)
            return i;
    }
    return -1;
}

void ClipboardHistoryModel::enforceCap()
{
    // Evicts the oldest rows past the cap with the proper row signals, but does
    // NOT emit countChanged/historyChanged itself: callers detect the size delta
    // and emit (so a net-zero dedup+evict still reports the right count).
    while (m_entries.size() > m_maxEntries) {
        const int last = m_entries.size() - 1;
        beginRemoveRows(QModelIndex(), last, last);
        m_entries.removeLast();
        endRemoveRows();
    }
}

QString ClipboardHistoryModel::preferredMimeType(const QStringList& mimeTypes)
{
    // Prefer UTF-8 text, then any plain/other text, then a common image type,
    // then any image, then whatever is offered first.
    static const QStringList textPreference = {
        QStringLiteral("text/plain;charset=utf-8"),
        QStringLiteral("text/plain"),
        QStringLiteral("UTF8_STRING"),
        QStringLiteral("STRING"),
    };
    for (const QString& candidate : textPreference) {
        if (mimeTypes.contains(candidate))
            return candidate;
    }
    for (const QString& mime : mimeTypes) {
        if (isTextType(mime))
            return mime;
    }
    if (mimeTypes.contains(QLatin1String("image/png")))
        return QStringLiteral("image/png");
    for (const QString& mime : mimeTypes) {
        if (mime.startsWith(QLatin1String("image/")))
            return mime;
    }
    return mimeTypes.isEmpty() ? QString() : mimeTypes.first();
}

bool ClipboardHistoryModel::isSensitive(const QStringList& mimeTypes)
{
    return mimeTypes.contains(kPasswordHint);
}

QString ClipboardHistoryModel::makePreview(const QByteArray& content, const QString& mimeType)
{
    if (!isTextType(mimeType)) {
        return QStringLiteral("[%1, %2 bytes]").arg(mimeType).arg(content.size());
    }
    // Collapse all runs of whitespace (newlines, tabs, spaces) to single spaces
    // and trim, so the preview is a single line.
    QString text = QString::fromUtf8(content).simplified();
    if (text.size() > kPreviewMaxChars)
        text = text.left(kPreviewMaxChars) + QStringLiteral("...");
    return text;
}

} // namespace PhosphorServiceClipboard
