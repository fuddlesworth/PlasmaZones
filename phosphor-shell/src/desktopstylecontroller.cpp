// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#include "DesktopStyleController.h"
#include <PhosphorProtocol/ServiceConstants.h>
#include <KPluginMetaData>
#include <QDBusArgument>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusServiceWatcher>
#include <QDBusVariant>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <utility>

namespace PhosphorShellApp {
namespace {
namespace Service = PhosphorProtocol::Service;
namespace Key = PhosphorProtocol::Service::SettingProperty;
const QString LibraryKey = QStringLiteral("org.kde.kdecoration2/library");
const QString Library = QStringLiteral("org.phosphor.decoration");
// Bounds every blocking call to the daemon, so a wedged daemon costs the
// shell a short stall rather than D-Bus's 25 second default.
constexpr int DaemonTimeoutMs = 2000;
QString configPath(const QString& name)
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + u'/' + name;
}
// Named apart from shellmotion.cpp's unwrap(QVariant): a unity blob holding
// both would make every call ambiguous.
QVariant unwrapDaemonValue(const QVariant& value)
{
    return value.canConvert<QDBusVariant>() ? value.value<QDBusVariant>().variant() : value;
}
bool sameValue(const QVariant& a, const QVariant& b)
{
    return QJsonValue::fromVariant(a) == QJsonValue::fromVariant(b);
}
}
DesktopStyleController::DesktopStyleController(QObject* parent)
    : DesktopStyleController(configPath(QStringLiteral("kwinrc")),
                             configPath(QStringLiteral("phosphor-shell/desktop-style-session.json")),
                             QString(Service::Name),
                             KPluginMetaData::findPluginById(QStringLiteral("org.kde.kdecoration3"), Library).isValid(),
                             QDBusConnection::sessionBus(), parent)
{
}
DesktopStyleController::DesktopStyleController(QString kwinPath, QString journalPath, QString service, bool available,
                                               QDBusConnection bus, QObject* parent)
    : QObject(parent)
    , m_kwinPath(std::move(kwinPath))
    , m_journalPath(std::move(journalPath))
    , m_service(std::move(service))
    , m_bus(std::move(bus))
    , m_available(available)
{
    QFile journal(m_journalPath);
    if (journal.open(QIODevice::ReadOnly)) {
        const auto document = QJsonDocument::fromJson(journal.read(16385)).object();
        if (document.value(QStringLiteral("version")).toInt() == 1)
            m_journal = document.value(QStringLiteral("state")).toObject().toVariantMap();
    }
    if (!m_bus.isConnected() || m_service.isEmpty())
        return;
    // The daemon owns the gaps. One that starts after the shell, or restarts
    // under it, gets the current request again; a same-value write is a no-op.
    auto* watcher = new QDBusServiceWatcher(m_service, m_bus, QDBusServiceWatcher::WatchForRegistration, this);
    connect(watcher, &QDBusServiceWatcher::serviceRegistered, this, [this] {
        if (m_requested.isEmpty())
            return;
        m_settings.clear();
        apply(m_requested);
    });
}
DesktopStyleController::~DesktopStyleController()
{
    restore();
}
QVariantMap DesktopStyleController::gaps(const QVariantMap& settings)
{
    const int gap = qBound(6, settings.value(QStringLiteral("gap"), 16).toInt(), 30);
    const bool bottom = settings.value(QStringLiteral("edge")).toString() == QStringLiteral("bottom");
    return {{QString(Key::InnerGap), gap},
            {QString(Key::UsePerSideOuterGap), true},
            {QString(Key::OuterGapLeft), 36 + gap / 2},
            {QString(Key::OuterGapRight), 36 + gap / 2},
            {QString(Key::OuterGapTop), (bottom ? 36 : 14) + gap / 2},
            {QString(Key::OuterGapBottom), (bottom ? 14 : 54) + gap / 2}};
}
std::optional<QVariantMap> DesktopStyleController::readGaps(const QStringList& keys) const
{
    auto message = QDBusMessage::createMethodCall(m_service, QString(Service::ObjectPath),
                                                  QString(Service::Interface::Settings), QStringLiteral("getSettings"));
    // Never activate the daemon from here: the shell does not start PlasmaZones.
    message.setAutoStartService(false);
    message << keys;
    const auto reply = m_bus.call(message, QDBus::Block, DaemonTimeoutMs);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
        return std::nullopt;
    const auto argument = reply.arguments().constFirst();
    QVariantMap values;
    // Off the wire an a{sv} arrives as a QDBusArgument; a call the bus
    // delivers in-process hands the map over as it is.
    if (argument.userType() == qMetaTypeId<QDBusArgument>())
        argument.value<QDBusArgument>() >> values;
    else
        values = argument.toMap();
    QVariantMap out;
    for (const auto& key : keys) {
        // An unknown key is omitted from the reply. A daemon that cannot name
        // every gap cannot be restored faithfully, so treat it as unavailable.
        if (!values.contains(key))
            return std::nullopt;
        out.insert(key, unwrapDaemonValue(values.value(key)));
    }
    return out;
}
bool DesktopStyleController::writeGaps(const QVariantMap& values) const
{
    auto message = QDBusMessage::createMethodCall(m_service, QString(Service::ObjectPath),
                                                  QString(Service::Interface::Settings), QStringLiteral("setSettings"));
    message.setAutoStartService(false);
    message << values;
    const auto reply = m_bus.call(message, QDBus::Block, DaemonTimeoutMs);
    return reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()
        && reply.arguments().constFirst().toBool();
}
bool DesktopStyleController::saveJournal()
{
    if (!QDir().mkpath(QFileInfo(m_journalPath).absolutePath()))
        return false;
    QSaveFile file(m_journalPath);
    const auto bytes = QJsonDocument(QJsonObject{{QStringLiteral("version"), 1},
                                                 {QStringLiteral("state"), QJsonObject::fromVariantMap(m_journal)}})
                           .toJson();
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
void DesktopStyleController::reconfigure()
{
    m_bus.asyncCall(QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"), QStringLiteral("/KWin"),
                                                   QStringLiteral("org.kde.KWin"), QStringLiteral("reconfigure")));
}
void DesktopStyleController::apply(const QVariantMap& settings)
{
    m_requested = settings;
    if (!settings.value(QStringLiteral("desktopStyle"), true).toBool()) {
        restore();
        m_settings = settings;
        return;
    }
    if (!m_available || (settings == m_settings && !m_journal.isEmpty()))
        return;
    const auto desired = gaps(settings);
    QSettings kwin(m_kwinPath, QSettings::IniFormat);
    if (m_journal.isEmpty()) {
        const auto original = readGaps(desired.keys());
        if (!original) {
            qWarning("Could not read the window spacing from PlasmaZones");
            return;
        }
        m_journal = {{QStringLiteral("originalGaps"), *original},
                     {QStringLiteral("hadLibrary"), kwin.contains(LibraryKey)},
                     {QStringLiteral("library"), kwin.value(LibraryKey)}};
    }
    const auto previous = m_journal;
    m_journal[QStringLiteral("pendingGaps")] = desired;
    // Retain the last successful values while journaling the next write.
    // Recovery accepts either set: a crash may happen before or after the
    // daemon applies the write, before the journal records its completion.
    if (!saveJournal()) {
        m_journal = previous;
        qWarning("Could not journal the shell desktop style");
        return;
    }
    if (!writeGaps(desired)) {
        qWarning("Could not apply the shell window spacing");
        return;
    }
    m_settings.clear();
    m_journal[QStringLiteral("appliedGaps")] = desired;
    m_journal.remove(QStringLiteral("pendingGaps"));
    if (!saveJournal()) {
        qWarning("Could not record the applied shell window spacing");
        return;
    }
    const bool changed = kwin.value(LibraryKey).toString() != Library;
    kwin.setValue(LibraryKey, Library);
    kwin.sync();
    if (kwin.status() != QSettings::NoError) {
        qWarning("Could not apply the shell window decoration");
        return;
    }
    m_settings = settings;
    if (changed)
        reconfigure();
}
bool DesktopStyleController::restore()
{
    if (m_journal.isEmpty())
        return true;
    const auto original = m_journal.value(QStringLiteral("originalGaps")).toMap();
    const auto applied = m_journal.value(QStringLiteral("appliedGaps")).toMap();
    const auto pending = m_journal.value(QStringLiteral("pendingGaps")).toMap();
    auto owned = applied;
    for (auto it = pending.cbegin(); it != pending.cend(); ++it)
        owned.insert(it.key(), it.value());
    if (!owned.isEmpty()) {
        const auto current = readGaps(owned.keys());
        if (!current)
            return false;
        QVariantMap revert;
        for (auto it = owned.cbegin(); it != owned.cend(); ++it) {
            const auto value = current->value(it.key());
            const bool matchesApplied = applied.contains(it.key()) && sameValue(value, applied.value(it.key()));
            const bool matchesPending = pending.contains(it.key()) && sameValue(value, pending.value(it.key()));
            // A value someone changed since is theirs now, and stays.
            if ((matchesApplied || matchesPending) && original.contains(it.key()))
                revert.insert(it.key(), original.value(it.key()));
        }
        if (!revert.isEmpty() && !writeGaps(revert))
            return false;
    }
    m_settings.clear();
    QSettings kwin(m_kwinPath, QSettings::IniFormat);
    if (kwin.value(LibraryKey).toString() == Library) {
        if (m_journal.value(QStringLiteral("hadLibrary")).toBool())
            kwin.setValue(LibraryKey, m_journal.value(QStringLiteral("library")));
        else
            kwin.remove(LibraryKey);
        kwin.sync();
        if (kwin.status() != QSettings::NoError)
            return false;
        reconfigure();
    }
    const auto previous = std::exchange(m_journal, {});
    if (!saveJournal()) {
        m_journal = previous;
        return false;
    }
    m_settings.clear();
    return true;
}
}
