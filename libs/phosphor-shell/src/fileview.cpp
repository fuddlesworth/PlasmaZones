// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShell/FileView.h>

#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QTimer>

namespace PhosphorShell {

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
    m_interval = interval;
    Q_EMIT intervalChanged();

    if (m_interval > 0) {
        delete m_watcher;
        m_watcher = nullptr;
        setupTimer();
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

    const QString newContent = QString::fromUtf8(file.readAll());
    if (newContent != m_content) {
        m_content = newContent;
        Q_EMIT contentChanged();
    }
}

void FileView::onFileChanged()
{
    readFile();

    if (m_watcher && !m_watcher->files().contains(m_path)) {
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
