// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <QtGlobal>

#ifndef PHOSPHOR_REGISTRY_FIXTURE_DIR
#error "PHOSPHOR_REGISTRY_FIXTURE_DIR must be defined by tests/CMakeLists.txt"
#endif

namespace PhosphorRegistryTestHelpers {

void messageCapturingHandler(QtMsgType type, const QMessageLogContext& context, const QString& message);

/// Captures every warning (and, on request, info line) logged while in scope.
/// qInstallMessageHandler takes a plain function, so the active capture is
/// reached through a static pointer, saved and restored so captures nest.
class MessageCapture
{
public:
    explicit MessageCapture(QStringList& warnings, QStringList* infos = nullptr)
        : m_warnings(&warnings)
        , m_infos(infos)
    {
        m_priorActive = s_active;
        s_active = this;
        m_priorHandler = qInstallMessageHandler(messageCapturingHandler);
    }
    ~MessageCapture()
    {
        qInstallMessageHandler(m_priorHandler);
        s_active = m_priorActive;
    }
    Q_DISABLE_COPY_MOVE(MessageCapture)

private:
    inline static MessageCapture* s_active = nullptr;
    QtMessageHandler m_priorHandler = nullptr;
    QStringList* m_warnings = nullptr;
    QStringList* m_infos = nullptr;
    MessageCapture* m_priorActive = nullptr;

    friend void messageCapturingHandler(QtMsgType type, const QMessageLogContext& context, const QString& message);
};

inline void messageCapturingHandler(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    MessageCapture* const active = MessageCapture::s_active;
    if (!active) {
        return;
    }
    if (type == QtWarningMsg && active->m_warnings) {
        active->m_warnings->append(message);
    } else if (type == QtInfoMsg && active->m_infos) {
        active->m_infos->append(message);
    }
    if (active->m_priorHandler) {
        active->m_priorHandler(type, context, message);
    }
}

/// How many entries of @p lines contain @p needle.
[[nodiscard]] inline int countContaining(const QStringList& lines, const QString& needle)
{
    int count = 0;
    for (const QString& line : lines) {
        if (line.contains(needle)) {
            ++count;
        }
    }
    return count;
}

/// The built fixture library for @p variant (see tests/CMakeLists.txt).
[[nodiscard]] inline QString fixturePath(const QString& variant)
{
    return QStringLiteral(PHOSPHOR_REGISTRY_FIXTURE_DIR) + QLatin1Char('/') + variant + QStringLiteral(".so");
}

/// Copy the fixture @p variant into @p directory as @p fileName (default
/// `<variant>.so`), creating the directory, with owner-only write bits on the
/// file and the directory so the catalog's permission check passes. Returns
/// the destination path, or an empty string on failure.
[[nodiscard]] inline QString stagePlugin(const QString& directory, const QString& variant,
                                         const QString& fileName = QString())
{
    if (!QDir().mkpath(directory)) {
        qWarning().noquote() << "stagePlugin: cannot create" << directory;
        return QString();
    }
    const QFileDevice::Permissions readable = QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner
        | QFileDevice::ReadGroup | QFileDevice::ExeGroup | QFileDevice::ReadOther | QFileDevice::ExeOther;
    QFile::setPermissions(directory, readable);
    const QString source = fixturePath(variant);
    if (!QFileInfo::exists(source)) {
        qWarning().noquote() << "stagePlugin: missing fixture" << source;
        return QString();
    }
    const QString destination =
        QDir(directory).absoluteFilePath(fileName.isEmpty() ? variant + QStringLiteral(".so") : fileName);
    QFile::remove(destination);
    if (!QFile::copy(source, destination)) {
        qWarning().noquote() << "stagePlugin: cannot copy" << source << "to" << destination;
        return QString();
    }
    QFile::setPermissions(destination, readable);
    return destination;
}

} // namespace PhosphorRegistryTestHelpers
