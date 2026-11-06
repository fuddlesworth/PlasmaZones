// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PhosphorConfig/HostDirectory.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QMap>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>

namespace PhosphorConfig {

namespace {
Q_LOGGING_CATEGORY(lcHostDirectory, "phosphorconfig.hostdirectory")

const QRegularExpression& idPattern()
{
    static const QRegularExpression pattern(QStringLiteral("^[a-z0-9][a-z0-9-]*$"));
    return pattern;
}

QJsonObject readObject(const QString& path, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = file.errorString();
        return {};
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (!document.isObject()) {
        *error = parseError.error != QJsonParseError::NoError ? parseError.errorString()
                                                              : QStringLiteral("not a JSON object");
        return {};
    }
    return document.object();
}
} // namespace

std::optional<HostDescriptor> HostDescriptor::parse(const QJsonObject& json, QString* error)
{
    const auto fail = [error](const QString& message) -> std::optional<HostDescriptor> {
        if (error) {
            *error = message;
        }
        return std::nullopt;
    };
    HostDescriptor descriptor;
    descriptor.id = json.value(QLatin1String("Id")).toString();
    descriptor.name = json.value(QLatin1String("Name")).toString();
    descriptor.busName = json.value(QLatin1String("BusName")).toString();
    descriptor.backend = json.value(QLatin1String("Backend")).toString();
    descriptor.configPath = json.value(QLatin1String("ConfigPath")).toString();
    for (const QJsonValue& desktop : json.value(QLatin1String("Desktops")).toArray()) {
        if (desktop.isString() && !desktop.toString().isEmpty()) {
            descriptor.desktops.append(desktop.toString());
        }
    }
    if (!idPattern().match(descriptor.id).hasMatch()) {
        return fail(QStringLiteral("\"Id\" must match [a-z0-9][a-z0-9-]*"));
    }
    if (descriptor.name.isEmpty()) {
        return fail(QStringLiteral("\"Name\" is missing"));
    }
    if (descriptor.busName.isEmpty()) {
        return fail(QStringLiteral("\"BusName\" is missing"));
    }
    if (descriptor.backend != QLatin1String("json")) {
        return fail(QStringLiteral("\"Backend\" %1 is not a known backend (json)").arg(descriptor.backend));
    }
    if (descriptor.resolvedConfigPath().isEmpty()) {
        return fail(QStringLiteral("\"ConfigPath\" %1 must start with $XDG_CONFIG_HOME, $XDG_DATA_HOME, "
                                   "$XDG_STATE_HOME or $HOME and contain no ..")
                        .arg(descriptor.configPath));
    }
    return descriptor;
}

QString HostDescriptor::resolvedConfigPath() const
{
    const QList<std::pair<QLatin1String, QStandardPaths::StandardLocation>> roots = {
        {QLatin1String("$XDG_CONFIG_HOME"), QStandardPaths::GenericConfigLocation},
        {QLatin1String("$XDG_DATA_HOME"), QStandardPaths::GenericDataLocation},
        {QLatin1String("$XDG_STATE_HOME"), QStandardPaths::GenericStateLocation},
        {QLatin1String("$HOME"), QStandardPaths::HomeLocation},
    };
    for (const auto& [variable, location] : roots) {
        if (configPath != variable && !configPath.startsWith(variable + QLatin1Char('/'))) {
            continue;
        }
        const QString rest = configPath.mid(variable.size());
        if (rest.split(QLatin1Char('/')).contains(QLatin1String(".."))) {
            return QString();
        }
        const QString root = QStandardPaths::writableLocation(location);
        if (root.isEmpty() || !QDir::isAbsolutePath(root)) {
            return QString();
        }
        return QDir::cleanPath(root + rest);
    }
    return QString();
}

HostDirectory::HostDirectory(QString api)
    : m_api(std::move(api))
{
}

QString HostDirectory::api() const
{
    return m_api;
}

QList<HostDescriptor> HostDirectory::descriptors() const
{
    const QString subdirectory = QStringLiteral("phosphor-works/%1-hosts").arg(m_api);
    // locateAll lists the per-user directory first, so the first descriptor
    // seen for an id is the one that wins.
    QMap<QString, HostDescriptor> byId;
    const QStringList directories =
        QStandardPaths::locateAll(QStandardPaths::GenericDataLocation, subdirectory, QStandardPaths::LocateDirectory);
    for (const QString& directory : directories) {
        const QFileInfoList files = QDir(directory).entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
        for (const QFileInfo& file : files) {
            QString error;
            const QJsonObject json = readObject(file.absoluteFilePath(), &error);
            std::optional<HostDescriptor> descriptor;
            if (error.isEmpty()) {
                descriptor = HostDescriptor::parse(json, &error);
            }
            if (!descriptor) {
                qCWarning(lcHostDirectory).noquote()
                    << "HostDirectory: skipping" << file.absoluteFilePath() << "-" << error;
                continue;
            }
            if (byId.contains(descriptor->id)) {
                qCInfo(lcHostDirectory).noquote() << "HostDirectory:" << file.absoluteFilePath()
                                                  << "is overridden by an earlier descriptor for" << descriptor->id;
                continue;
            }
            byId.insert(descriptor->id, *descriptor);
        }
    }
    return byId.values();
}

QString HostDirectory::recordPath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation)
        + QStringLiteral("/phosphor-works/%1-host.json").arg(m_api);
}

std::optional<HostRecord> HostDirectory::record() const
{
    const QString path = recordPath();
    if (!QFile::exists(path)) {
        return std::nullopt;
    }
    QString error;
    const QJsonObject json = readObject(path, &error);
    HostRecord record;
    record.host = json.value(QLatin1String("Host")).toString();
    record.configPath = json.value(QLatin1String("ConfigPath")).toString();
    record.pid = json.value(QLatin1String("Pid")).toInteger();
    record.written = QDateTime::fromString(json.value(QLatin1String("Written")).toString(), Qt::ISODate);
    if (!error.isEmpty() || !idPattern().match(record.host).hasMatch() || !QDir::isAbsolutePath(record.configPath)) {
        qCWarning(lcHostDirectory).noquote() << "HostDirectory: ignoring the unreadable host record" << path
                                             << (error.isEmpty() ? QString() : QStringLiteral("- ") + error);
        return std::nullopt;
    }
    return record;
}

bool HostDirectory::writeRecord(const QString& hostId, const QString& configPath, qint64 pid) const
{
    const QString path = recordPath();
    if (!idPattern().match(hostId).hasMatch() || !QDir::isAbsolutePath(configPath)) {
        qCWarning(lcHostDirectory).noquote()
            << "HostDirectory: refusing to record host" << hostId << "with config path" << configPath;
        return false;
    }
    QJsonObject json;
    json.insert(QLatin1String("Host"), hostId);
    json.insert(QLatin1String("ConfigPath"), configPath);
    json.insert(QLatin1String("Pid"), pid);
    json.insert(QLatin1String("Written"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(json).toJson()) < 0 || !file.commit()) {
        qCWarning(lcHostDirectory).noquote() << "HostDirectory: could not write" << path << "-" << file.errorString();
        return false;
    }
    return true;
}

HostResolution HostDirectory::resolve(const QList<HostDescriptor>& descriptors, const std::optional<HostRecord>& record,
                                      qint64 busOwnerPid, const QStringList& currentDesktops)
{
    HostResolution resolution;
    resolution.running = busOwnerPid > 0;

    if (record) {
        const bool recordIsTheOwner = busOwnerPid > 0 && record->pid == busOwnerPid;
        if (recordIsTheOwner || busOwnerPid <= 0) {
            for (const HostDescriptor& descriptor : descriptors) {
                if (descriptor.id == record->host) {
                    resolution.host = descriptor;
                    resolution.configPath = record->configPath;
                    return resolution;
                }
            }
        }
    }

    for (const HostDescriptor& descriptor : descriptors) {
        const bool matches = std::any_of(currentDesktops.cbegin(), currentDesktops.cend(), [&](const QString& desktop) {
            return descriptor.desktops.contains(desktop, Qt::CaseInsensitive);
        });
        if (matches) {
            resolution.candidates.append(descriptor);
        }
    }
    if (resolution.candidates.size() == 1) {
        resolution.host = resolution.candidates.takeFirst();
        resolution.configPath = resolution.host->resolvedConfigPath();
    }
    return resolution;
}

QStringList HostDirectory::currentDesktops()
{
    return qEnvironmentVariable("XDG_CURRENT_DESKTOP").split(QLatin1Char(':'), Qt::SkipEmptyParts);
}

} // namespace PhosphorConfig
