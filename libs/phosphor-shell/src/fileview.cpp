// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShell/FileView.h>

#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QTimer>

namespace PhosphorShell {

namespace {
// Cap on file content read size — defensive against being pointed at a
// huge file (or /dev/zero) by a malicious or buggy QML config. 16 MiB is
// well above the typical /proc, /sys, and config file size, but small
// enough to keep memory bounded if something goes wrong.
constexpr qint64 kMaxFileBytes = 16 * 1024 * 1024;
} // namespace

FileView::FileView(QObject* parent)
    : QObject(parent)
    , m_timer(new QTimer(this))
{
    m_timer->setSingleShot(false);
    connect(m_timer, &QTimer::timeout, this, &FileView::readFile);
}

FileView::~FileView() = default;

QString FileView::path() const
{
    return m_path;
}

void FileView::setPath(const QString& path)
{
    if (m_path == path) {
        return;
    }
    // Drop any previously-watched path so the watcher doesn't accumulate
    // notifications for files we no longer care about.
    if (m_watcher && !m_watcher->files().isEmpty()) {
        m_watcher->removePaths(m_watcher->files());
    }
    m_path = path;
    Q_EMIT pathChanged();

    if (m_interval > 0) {
        setupTimer();
    } else {
        setupWatcher();
    }
    readFile();
}

QString FileView::content() const
{
    return m_content;
}

int FileView::interval() const
{
    return m_interval;
}

void FileView::setInterval(int interval)
{
    if (m_interval == interval) {
        return;
    }
    const int previous = m_interval;
    m_interval = interval;
    Q_EMIT intervalChanged();

    if (m_interval > 0) {
        if (m_watcher) {
            m_watcher->deleteLater();
            m_watcher = nullptr;
        }
        setupTimer();
        // Switching from 0 (watcher mode) to N>0 (poll mode) without
        // an immediate read would leave the consumer with stale
        // content for the first interval window. Read once now so the
        // transition is visible.
        if (previous == 0 && !m_path.isEmpty()) {
            readFile();
        }
    } else {
        m_timer->stop();
        setupWatcher();
    }
}

bool FileView::exists() const
{
    return m_exists;
}

void FileView::readFile()
{
    if (m_path.isEmpty()) {
        return;
    }

    QFile file(m_path);
    const bool fileExists = file.exists();

    if (fileExists != m_exists) {
        m_exists = fileExists;
        Q_EMIT existsChanged();
    }

    if (!fileExists || !file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (!m_content.isEmpty()) {
            m_content.clear();
            Q_EMIT contentChanged();
        }
        return;
    }

    // Bounded read — protects against /dev/zero, multi-GB logs, etc.
    const QByteArray bytes = file.read(kMaxFileBytes);
    const QString newContent = QString::fromUtf8(bytes);
    if (newContent != m_content) {
        m_content = newContent;
        Q_EMIT contentChanged();
    }
}

void FileView::onFileChanged()
{
    readFile();

    // Editors that save via atomic-rename (vim, most IDEs) invalidate the
    // watch on the original inode. Re-add the path — but only if it now
    // exists on disk. Otherwise QFileSystemWatcher::addPath silently fails
    // and we'd burn a watcher slot on every fire for a file that's gone.
    if (m_watcher && !m_watcher->files().contains(m_path) && QFileInfo::exists(m_path)) {
        m_watcher->addPath(m_path);
    }
}

void FileView::setupWatcher()
{
    if (m_path.isEmpty()) {
        return;
    }

    if (!m_watcher) {
        m_watcher = new QFileSystemWatcher(this);
        connect(m_watcher, &QFileSystemWatcher::fileChanged, this, &FileView::onFileChanged);
    }

    if (!m_watcher->files().contains(m_path) && QFileInfo::exists(m_path)) {
        m_watcher->addPath(m_path);
    }
}

void FileView::setupTimer()
{
    if (m_interval > 0 && !m_path.isEmpty()) {
        m_timer->start(m_interval);
    } else {
        m_timer->stop();
    }
}

} // namespace PhosphorShell
