// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorConfig/IGroupPathResolver.h>
#include <PhosphorConfig/JsonBackend.h>

#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

#include <memory>
#include <optional>

namespace PhosphorConfig::tests {

/// Resolver that captures every group name routing through it — exercises
/// the IGroupPathResolver plug-in hook without pulling in PlasmaZones.
class CapturingResolver : public IGroupPathResolver
{
public:
    std::optional<QStringList> toJsonPath(const QString& groupName) const override
    {
        if (groupName.startsWith(QStringLiteral("Screen:"))) {
            const QString id = groupName.mid(7);
            if (id.isEmpty()) {
                return QStringList{};
            }
            return QStringList{QStringLiteral("Screens"), id};
        }
        return std::nullopt;
    }

    QStringList reservedRootKeys() const override
    {
        return {QStringLiteral("Screens")};
    }

    QStringList enumerate(const QJsonObject& root) const override
    {
        QStringList out;
        const QJsonObject screens = root.value(QStringLiteral("Screens")).toObject();
        for (auto it = screens.constBegin(); it != screens.constEnd(); ++it) {
            out << QStringLiteral("Screen:") + it.key();
        }
        return out;
    }
};

class TestJsonBackend : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        m_tmp = std::make_unique<QTemporaryDir>();
        QVERIFY(m_tmp->isValid());
        m_path = m_tmp->filePath(QStringLiteral("config.json"));
    }

    void cleanup()
    {
        m_tmp.reset();
    }

    void emptyFile_returnsDefaults()
    {
        JsonBackend b(m_path);
        auto g = b.group(QStringLiteral("NoSuchGroup"));
        QCOMPARE(g->readInt(QStringLiteral("missing"), 42), 42);
        QCOMPARE(g->readString(QStringLiteral("missing"), QStringLiteral("d")), QStringLiteral("d"));
        QCOMPARE(g->readBool(QStringLiteral("missing"), true), true);
        QCOMPARE(g->readDouble(QStringLiteral("missing"), 3.14), 3.14);
    }

    void roundTrip_flatGroup()
    {
        {
            JsonBackend b(m_path);
            auto g = b.group(QStringLiteral("Window"));
            g->writeInt(QStringLiteral("Width"), 800);
            g->writeBool(QStringLiteral("Maximized"), true);
            g->writeString(QStringLiteral("Title"), QStringLiteral("PhosphorConfig"));
            g.reset();
            b.sync();
        }

        JsonBackend b(m_path);
        auto g = b.group(QStringLiteral("Window"));
        QCOMPARE(g->readInt(QStringLiteral("Width"), 0), 800);
        QCOMPARE(g->readBool(QStringLiteral("Maximized"), false), true);
        QCOMPARE(g->readString(QStringLiteral("Title")), QStringLiteral("PhosphorConfig"));
    }

    void roundTrip_dotPathGroup()
    {
        {
            JsonBackend b(m_path);
            auto g = b.group(QStringLiteral("Snapping.Behavior.ZoneSpan"));
            g->writeBool(QStringLiteral("Enabled"), true);
            g->writeInt(QStringLiteral("Modifier"), 2);
            g.reset();
            b.sync();
        }

        QFile f(m_path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
        QVERIFY(root.contains(QStringLiteral("Snapping")));
        const QJsonObject zoneSpan = root.value(QStringLiteral("Snapping"))
                                         .toObject()
                                         .value(QStringLiteral("Behavior"))
                                         .toObject()
                                         .value(QStringLiteral("ZoneSpan"))
                                         .toObject();
        QCOMPARE(zoneSpan.value(QStringLiteral("Enabled")).toBool(), true);
        QCOMPARE(zoneSpan.value(QStringLiteral("Modifier")).toInt(), 2);
    }

    void resolver_routesGroupsToNestedPath()
    {
        auto resolver = std::make_shared<CapturingResolver>();

        {
            JsonBackend b(m_path);
            b.setPathResolver(resolver);
            auto g = b.group(QStringLiteral("Screen:DP-1"));
            g->writeInt(QStringLiteral("Width"), 2560);
            g.reset();
            b.sync();
        }

        QFile f(m_path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
        QVERIFY(root.contains(QStringLiteral("Screens")));
        QCOMPARE(root.value(QStringLiteral("Screens"))
                     .toObject()
                     .value(QStringLiteral("DP-1"))
                     .toObject()
                     .value(QStringLiteral("Width"))
                     .toInt(),
                 2560);
    }

    void resolver_malformedGroupIsDroppedWithWarning()
    {
        auto resolver = std::make_shared<CapturingResolver>();
        JsonBackend b(m_path);
        b.setPathResolver(resolver);

        auto g = b.group(QStringLiteral("Screen:"));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("refusing write to malformed group")));
        g->writeInt(QStringLiteral("x"), 1); // Should warn and no-op
    }

    void groupList_enumeratesDotPathsAndResolverGroups()
    {
        auto resolver = std::make_shared<CapturingResolver>();

        JsonBackend b(m_path);
        b.setPathResolver(resolver);
        {
            auto g = b.group(QStringLiteral("A.B"));
            g->writeInt(QStringLiteral("k"), 1);
        }
        {
            auto g = b.group(QStringLiteral("Screen:DP-1"));
            g->writeInt(QStringLiteral("k"), 1);
        }

        const auto groups = b.groupList();
        QVERIFY(groups.contains(QStringLiteral("A")));
        QVERIFY(groups.contains(QStringLiteral("A.B")));
        QVERIFY(groups.contains(QStringLiteral("Screen:DP-1")));
        // The resolver-reserved "Screens" root should NOT appear as a dot-path group.
        QVERIFY(!groups.contains(QStringLiteral("Screens")));
    }

    void versionStamp_freshInstall()
    {
        {
            JsonBackend b(m_path);
            b.setVersionStamp(QStringLiteral("_version"), 3);
            auto g = b.group(QStringLiteral("Something"));
            g->writeInt(QStringLiteral("k"), 1);
            g.reset();
            b.sync();
        }

        QFile f(m_path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
        QCOMPARE(root.value(QStringLiteral("_version")).toInt(), 3);
    }

    void atomicWrite_exposedForExternalUse()
    {
        QJsonObject root;
        root[QStringLiteral("k")] = 42;
        QVERIFY(JsonBackend::writeJsonAtomically(m_path, root));

        QFile f(m_path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QJsonObject readBack = QJsonDocument::fromJson(f.readAll()).object();
        QCOMPARE(readBack.value(QStringLiteral("k")).toInt(), 42);
    }

    void keyList_returnsScalarLeavesOnly()
    {
        JsonBackend b(m_path);
        auto parent = b.group(QStringLiteral("Parent"));
        parent->writeInt(QStringLiteral("scalarA"), 1);
        parent->writeString(QStringLiteral("scalarB"), QStringLiteral("x"));
        parent.reset();

        // Create a sub-group so "Parent" now has both scalars and an object child.
        auto child = b.group(QStringLiteral("Parent.Child"));
        child->writeInt(QStringLiteral("inner"), 2);
        child.reset();

        auto parentAgain = b.group(QStringLiteral("Parent"));
        const QStringList keys = parentAgain->keyList();
        QVERIFY(keys.contains(QStringLiteral("scalarA")));
        QVERIFY(keys.contains(QStringLiteral("scalarB")));
        // Child is a sub-group, not a scalar key — must NOT appear.
        QVERIFY(!keys.contains(QStringLiteral("Child")));
    }

private:
    std::unique_ptr<QTemporaryDir> m_tmp;
    QString m_path;
};

} // namespace PhosphorConfig::tests

QTEST_MAIN(PhosphorConfig::tests::TestJsonBackend)
#include "test_json_backend.moc"
