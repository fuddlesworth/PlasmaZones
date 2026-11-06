// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorConfig/HostDirectory.h>

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>

using namespace PhosphorConfig;

namespace {

HostDescriptor descriptor(const QString& id, const QStringList& desktops)
{
    HostDescriptor d;
    d.id = id;
    d.name = id;
    d.busName = QStringLiteral("org.phosphor_works.Placement");
    d.backend = QStringLiteral("json");
    d.configPath = QStringLiteral("$XDG_CONFIG_HOME/%1/config.json").arg(id);
    d.desktops = desktops;
    return d;
}

HostRecord record(const QString& host, qint64 pid)
{
    return HostRecord{host, QStringLiteral("/recorded/%1.json").arg(host), pid, QDateTime::currentDateTimeUtc()};
}

} // namespace

class TestHostDirectory : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        QVERIFY(m_root.emplace().isValid());
        const auto setDir = [this](const char* variable, const QString& name) {
            const QString path = m_root->filePath(name);
            QDir().mkpath(path);
            qputenv(variable, path.toUtf8());
        };
        setDir("XDG_CONFIG_HOME", QStringLiteral("config"));
        setDir("XDG_DATA_HOME", QStringLiteral("data"));
        setDir("XDG_STATE_HOME", QStringLiteral("state"));
        setDir("XDG_DATA_DIRS", QStringLiteral("system"));
    }

    void parsesADescriptorAndResolvesItsPath()
    {
        QString error;
        const std::optional<HostDescriptor> parsed =
            HostDescriptor::parse(QJsonDocument::fromJson(R"({ "Id": "plasmazones", "Name": "PlasmaZones",
                "BusName": "org.phosphor_works.Placement", "Backend": "json",
                "ConfigPath": "$XDG_CONFIG_HOME/plasmazones/config.json", "Desktops": ["KDE"] })")
                                      .object(),
                                  &error);
        QVERIFY2(parsed, qPrintable(error));
        QCOMPARE(parsed->desktops, QStringList{QStringLiteral("KDE")});
        QCOMPARE(parsed->resolvedConfigPath(), m_root->filePath(QStringLiteral("config/plasmazones/config.json")));
    }

    void refusesABadDescriptor_data()
    {
        QTest::addColumn<QByteArray>("json");
        QTest::addColumn<QString>("expected");
        const QByteArray good = R"("Name": "N", "BusName": "b", "Backend": "json", "ConfigPath": "$HOME/x.json")";
        QTest::newRow("bad id") << (R"({ "Id": "Bad Id", )" + good + " }") << QStringLiteral("\"Id\"");
        QTest::newRow("no name") << QByteArray(R"({ "Id": "a", "BusName": "b", "Backend": "json",
            "ConfigPath": "$HOME/x.json" })")
                                 << QStringLiteral("\"Name\"");
        QTest::newRow("no bus name") << QByteArray(R"({ "Id": "a", "Name": "N", "Backend": "json",
            "ConfigPath": "$HOME/x.json" })")
                                     << QStringLiteral("\"BusName\"");
        QTest::newRow("unknown backend") << QByteArray(R"({ "Id": "a", "Name": "N", "BusName": "b",
            "Backend": "ini", "ConfigPath": "$HOME/x.json" })")
                                         << QStringLiteral("ini is not a known backend");
        QTest::newRow("absolute path") << QByteArray(R"({ "Id": "a", "Name": "N", "BusName": "b",
            "Backend": "json", "ConfigPath": "/etc/x.json" })")
                                       << QStringLiteral("\"ConfigPath\"");
        QTest::newRow("parent segment") << QByteArray(R"({ "Id": "a", "Name": "N", "BusName": "b",
            "Backend": "json", "ConfigPath": "$HOME/../x.json" })")
                                        << QStringLiteral("\"ConfigPath\"");
        QTest::newRow("variable prefix only") << QByteArray(R"({ "Id": "a", "Name": "N", "BusName": "b",
            "Backend": "json", "ConfigPath": "$HOMEX/x.json" })")
                                              << QStringLiteral("\"ConfigPath\"");
    }

    void refusesABadDescriptor()
    {
        QFETCH(QByteArray, json);
        QFETCH(QString, expected);
        QString error;
        QVERIFY(!HostDescriptor::parse(QJsonDocument::fromJson(json).object(), &error));
        QVERIFY2(error.contains(expected), qPrintable(error));
    }

    void findsDescriptorsAndLetsTheUserOverride()
    {
        const auto install = [this](const QString& root, const QString& file, const QByteArray& json) {
            const QString dir = m_root->filePath(root + QStringLiteral("/phosphor-works/placement-hosts"));
            QDir().mkpath(dir);
            QFile out(dir + QLatin1Char('/') + file);
            QVERIFY(out.open(QIODevice::WriteOnly));
            out.write(json);
        };
        install(QStringLiteral("system"), QStringLiteral("plasmazones.json"),
                R"({ "Id": "plasmazones", "Name": "System", "BusName": "b", "Backend": "json",
                     "ConfigPath": "$XDG_CONFIG_HOME/pz.json" })");
        install(QStringLiteral("system"), QStringLiteral("phosphor.json"),
                R"({ "Id": "phosphor", "Name": "Phosphor", "BusName": "b", "Backend": "json",
                     "ConfigPath": "$XDG_CONFIG_HOME/ph.json" })");
        install(QStringLiteral("system"), QStringLiteral("broken.json"), "{ not json");
        install(QStringLiteral("data"), QStringLiteral("plasmazones.json"),
                R"({ "Id": "plasmazones", "Name": "User", "BusName": "b", "Backend": "json",
                     "ConfigPath": "$XDG_CONFIG_HOME/pz.json" })");

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("skipping .*broken.json")));
        const QList<HostDescriptor> found = HostDirectory(QStringLiteral("placement")).descriptors();
        QCOMPARE(found.size(), 2);
        QCOMPARE(found.at(0).id, QStringLiteral("phosphor"));
        QCOMPARE(found.at(1).id, QStringLiteral("plasmazones"));
        QCOMPARE(found.at(1).name, QStringLiteral("User"));
        QVERIFY(HostDirectory(QStringLiteral("other")).descriptors().isEmpty());
    }

    void writesAndReadsTheRecord()
    {
        const HostDirectory directory(QStringLiteral("placement"));
        QCOMPARE(directory.recordPath(), m_root->filePath(QStringLiteral("state/phosphor-works/placement-host.json")));
        QVERIFY(!directory.record());
        const HostDescriptor host = descriptor(QStringLiteral("plasmazones"), {});
        QVERIFY(directory.writeRecord(host.id, host.resolvedConfigPath(), 4242));
        const std::optional<HostRecord> read = directory.record();
        QVERIFY(read);
        QCOMPARE(read->host, QStringLiteral("plasmazones"));
        QCOMPARE(read->configPath, m_root->filePath(QStringLiteral("config/plasmazones/config.json")));
        QCOMPARE(read->pid, 4242);
        QVERIFY(read->written.isValid());

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("refusing to record host")));
        QVERIFY(!directory.writeRecord(QStringLiteral("plasmazones"), QStringLiteral("relative.json"), 1));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("refusing to record host")));
        QVERIFY(!directory.writeRecord(QStringLiteral("../x"), QStringLiteral("/a.json"), 1));
        QCOMPARE(directory.record()->pid, 4242);

        QFile corrupt(directory.recordPath());
        QVERIFY(corrupt.open(QIODevice::WriteOnly));
        corrupt.write("{ \"Host\": \"../x\" }");
        corrupt.close();
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("ignoring the unreadable host record")));
        QVERIFY(!directory.record());
    }

    void resolvesInOrder()
    {
        const HostDescriptor kde = descriptor(QStringLiteral("plasmazones"), {QStringLiteral("KDE")});
        const HostDescriptor phosphor = descriptor(QStringLiteral("phosphor"), {QStringLiteral("Phosphor")});
        const HostDescriptor alsoKde = descriptor(QStringLiteral("other"), {QStringLiteral("kde")});
        const QList<HostDescriptor> both = {kde, phosphor};
        const QStringList onKde = {QStringLiteral("KDE")};

        // 1. The record written by the process that owns the name now.
        HostResolution r = HostDirectory::resolve(both, record(QStringLiteral("phosphor"), 7), 7, onKde);
        QCOMPARE(r.host->id, QStringLiteral("phosphor"));
        QCOMPARE(r.configPath, QStringLiteral("/recorded/phosphor.json"));
        QVERIFY(r.running);

        // A record from another process while the name is owned is stale:
        // the desktop decides.
        r = HostDirectory::resolve(both, record(QStringLiteral("phosphor"), 7), 9, onKde);
        QCOMPARE(r.host->id, QStringLiteral("plasmazones"));
        QCOMPARE(r.configPath, kde.resolvedConfigPath());
        QVERIFY(r.running);

        // 2. The record, when nothing owns the name.
        r = HostDirectory::resolve(both, record(QStringLiteral("phosphor"), 7), 0, onKde);
        QCOMPARE(r.host->id, QStringLiteral("phosphor"));
        QVERIFY(!r.running);

        // A record for a host that is no longer installed is ignored.
        r = HostDirectory::resolve(both, record(QStringLiteral("gone"), 0), 0, onKde);
        QCOMPARE(r.host->id, QStringLiteral("plasmazones"));

        // 3. The one descriptor for this desktop, ignoring case.
        r = HostDirectory::resolve(both, std::nullopt, 0, {QStringLiteral("phosphor")});
        QCOMPARE(r.host->id, QStringLiteral("phosphor"));
        QVERIFY(r.candidates.isEmpty());

        // 4. Several match: nothing is chosen, and the user is asked.
        r = HostDirectory::resolve({kde, alsoKde, phosphor}, std::nullopt, 0, onKde);
        QVERIFY(!r.host);
        QVERIFY(r.configPath.isEmpty());
        QCOMPARE(r.candidates.size(), 2);

        // None match.
        r = HostDirectory::resolve(both, std::nullopt, 0, {QStringLiteral("GNOME")});
        QVERIFY(!r.host);
        QVERIFY(r.candidates.isEmpty());
    }

    void readsTheCurrentDesktops()
    {
        qputenv("XDG_CURRENT_DESKTOP", "KDE:Plasma::");
        QCOMPARE(HostDirectory::currentDesktops(), (QStringList{QStringLiteral("KDE"), QStringLiteral("Plasma")}));
    }

private:
    std::optional<QTemporaryDir> m_root;
};

QTEST_GUILESS_MAIN(TestHostDirectory)
#include "test_host_directory.moc"
