// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#include "DesktopStyleController.h"
#include "config/configbackends.h"
#include "config/configdefaults.h"
#include <KPluginMetaData>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <utility>

namespace PhosphorShellApp {
namespace {
using CD = PlasmaZones::ConfigDefaults;
const QString LibraryKey = QStringLiteral("org.kde.kdecoration2/library");
const QString Library = QStringLiteral("org.phosphor.decoration");
QString configPath(const QString& name)
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + u'/' + name;
}
}
DesktopStyleController::DesktopStyleController(QObject* parent)
    : DesktopStyleController(configPath(QStringLiteral("kwinrc")),
                             configPath(QStringLiteral("phosphor-shell/desktop-style-session.json")),
                             PlasmaZones::createDefaultConfigBackend(),
                             KPluginMetaData::findPluginById(QStringLiteral("org.kde.kdecoration3"), Library).isValid(),
                             QDBusConnection::sessionBus(), parent)
{
}
DesktopStyleController::DesktopStyleController(QString kwinPath, QString journalPath,
                                               std::unique_ptr<PhosphorConfig::IBackend> backend, bool available,
                                               QDBusConnection bus, QObject* parent)
    : QObject(parent)
    , m_kwinPath(std::move(kwinPath))
    , m_journalPath(std::move(journalPath))
    , m_backend(std::move(backend))
    , m_bus(std::move(bus))
    , m_available(available)
{
    QFile journal(m_journalPath);
    if (journal.open(QIODevice::ReadOnly)) {
        const auto document = QJsonDocument::fromJson(journal.read(16385)).object();
        if (document.value(QStringLiteral("version")).toInt() == 1)
            m_journal = document.value(QStringLiteral("state")).toObject().toVariantMap();
    }
}
DesktopStyleController::~DesktopStyleController()
{
    restore();
}
QVariantMap DesktopStyleController::gaps(const QVariantMap& settings)
{
    const int gap = qBound(6, settings.value(QStringLiteral("gap"), 16).toInt(), 30);
    const bool bottom = settings.value(QStringLiteral("edge")).toString() == QStringLiteral("bottom");
    return {{CD::innerGapKey(), gap},
            {CD::usePerSideOuterGapKey(), true},
            {CD::outerGapLeftKey(), 36 + gap / 2},
            {CD::outerGapRightKey(), 36 + gap / 2},
            {CD::outerGapTopKey(), (bottom ? 36 : 14) + gap / 2},
            {CD::outerGapBottomKey(), (bottom ? 14 : 54) + gap / 2}};
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
void DesktopStyleController::reloadPlacement()
{
    m_bus.asyncCall(QDBusMessage::createMethodCall(QStringLiteral("org.plasmazones"), QStringLiteral("/PlasmaZones"),
                                                   QStringLiteral("org.plasmazones.Settings"),
                                                   QStringLiteral("reloadSettings")));
}
void DesktopStyleController::apply(const QVariantMap& settings)
{
    if (!settings.value(QStringLiteral("desktopStyle"), true).toBool()) {
        restore();
        m_settings = settings;
        return;
    }
    if (!m_available || (settings == m_settings && !m_journal.isEmpty()))
        return;
    m_backend->reparseConfiguration();
    const auto desired = gaps(settings);
    QSettings kwin(m_kwinPath, QSettings::IniFormat);
    if (m_journal.isEmpty()) {
        QVariantMap original;
        const auto group = m_backend->group(CD::gapsGroup());
        for (auto it = desired.cbegin(); it != desired.cend(); ++it) {
            if (group->hasKey(it.key()))
                original.insert(it.key(), group->readJson(it.key()).toVariant());
        }
        m_journal = {{QStringLiteral("originalGaps"), original},
                     {QStringLiteral("hadLibrary"), kwin.contains(LibraryKey)},
                     {QStringLiteral("library"), kwin.value(LibraryKey)}};
    }
    const auto previous = m_journal;
    m_journal[QStringLiteral("pendingGaps")] = desired;
    // Retain the last successful values while journaling the next write.
    // Recovery accepts either set: a crash may happen before or after the
    // atomic backend commit, before the journal records its completion.
    if (!saveJournal()) {
        m_journal = previous;
        qWarning("Could not journal the shell desktop style");
        return;
    }
    {
        const auto group = m_backend->group(CD::gapsGroup());
        for (auto it = desired.cbegin(); it != desired.cend(); ++it)
            group->writeJson(it.key(), QJsonValue::fromVariant(it.value()));
    }
    const bool spacingChanged = m_backend->isDirty() || m_settings.isEmpty();
    if (!m_backend->commit()) {
        qWarning("Could not apply the shell window spacing");
        return;
    }
    m_settings.clear();
    if (spacingChanged)
        reloadPlacement();
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
    m_backend->reparseConfiguration();
    const auto original = m_journal.value(QStringLiteral("originalGaps")).toMap();
    const auto applied = m_journal.value(QStringLiteral("appliedGaps")).toMap();
    const auto pending = m_journal.value(QStringLiteral("pendingGaps")).toMap();
    auto owned = applied;
    for (auto it = pending.cbegin(); it != pending.cend(); ++it)
        owned.insert(it.key(), it.value());
    {
        const auto group = m_backend->group(CD::gapsGroup());
        for (auto it = owned.cbegin(); it != owned.cend(); ++it) {
            const auto current = group->readJson(it.key());
            const bool matchesApplied =
                applied.contains(it.key()) && current == QJsonValue::fromVariant(applied.value(it.key()));
            const bool matchesPending =
                pending.contains(it.key()) && current == QJsonValue::fromVariant(pending.value(it.key()));
            if (!matchesApplied && !matchesPending)
                continue;
            if (original.contains(it.key()))
                group->writeJson(it.key(), QJsonValue::fromVariant(original.value(it.key())));
            else
                group->deleteKey(it.key());
        }
    }
    const bool spacingChanged = m_backend->isDirty();
    if (!m_backend->commit())
        return false;
    m_settings.clear();
    if (spacingChanged)
        reloadPlacement();
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
