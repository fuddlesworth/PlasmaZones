// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Smoke test for phosphor-service-notifications. Pins the plumbing contract
// (QML-registration idempotency, the static spec identifiers, server info,
// capabilities, id allocation with replaces_id reuse, the close-reason signal)
// and the milestone-3 ingest contract (hint decode, image-path decode,
// replace-in-place, the live-notification accessor).
//
// Every server instance is built on a PRIVATE peer D-Bus connection rather than
// the session bus, so the test never registers (or hijacks) the real
// org.freedesktop.Notifications name. The peer transport gives registerObject a
// real connection; well-known-name acquisition is a bus-daemon concept that
// does not apply peer-to-peer, so nameAcquired() is not asserted here (it is
// exercised manually via the CLI demo in milestone 7).
//
// The image-data (iiibiiay) struct decode needs a real wire round-trip (an
// in-process QDBusArgument is in marshalling mode and cannot be read back), so
// it is covered by the milestone-8 wire test; the image-path -> file branch is
// fully in-process and is pinned here.

#include <PhosphorServiceNotifications/Notification.h>
#include <PhosphorServiceNotifications/NotificationModel.h>
#include <PhosphorServiceNotifications/NotificationServer.h>
#include <PhosphorServiceNotifications/QmlRegistration.h>

#include <QAbstractItemModelTester>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusServer>
#include <QDeadlineTimer>
#include <QImage>
#include <QSet>
#include <QSignalSpy>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>
#include <QVariantMap>

#include <iterator>
#include <memory>

using namespace PhosphorServiceNotifications;

class NotificationsSmokeTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void registerQmlTypesIsIdempotent();
    void staticIdentifiers();
    void serverInformation();
    void capabilitiesAdvertiseExactSet();
    void notifyAllocatesMonotonicNonZeroIds();
    void notifyHonoursReplacesId();
    void closeNotificationEmitsForLiveIdOnly();
    void notifyDecodesScalarHints();
    void notifyDefaultsWhenHintsAbsent();
    void notifyDecodesImagePathFromFile();
    void notifyImagePathMissingIsGraceful();
    void replacesIdUpdatesInPlaceAndEmitsChanged();
    void notificationsAccessorTracksLifecycle();
    void explicitTimeoutExpiresWithReasonExpired();
    void timeoutZeroNeverExpires();
    void defaultTimeoutExpiresForNormal();
    void criticalDefaultNeverExpires();
    void dismissEmitsReasonDismissed();
    void invokeActionEmitsAndDismissesNonResident();
    void invokeActionKeepsResidentOpen();
    void invokeActionEmitsActivationTokenBeforeAction();
    void replaceUpdatesExpiry();
    void invokeActionUnknownIdIsNoop();
    void modelSeedsFromServer();
    void modelInsertsOnAdd();
    void modelRemovesOnClose();
    void modelDataChangedOnReplace();
    void modelRoleNamesPinned();
    void modelClearsWhenServerDestroyed();

private:
    std::unique_ptr<NotificationServer> makeServer();

    std::unique_ptr<QDBusServer> m_peer;
    int m_connCounter = 0;
};

void NotificationsSmokeTest::initTestCase()
{
    // A private peer bus: server instances bind their object to a connection
    // that has nothing to do with the session bus, so no test run can grab the
    // real notifications name. Anonymous auth keeps the handshake dependency
    // free.
    m_peer = std::make_unique<QDBusServer>(QStringLiteral("unix:tmpdir=/tmp"));
    m_peer->setAnonymousAuthenticationAllowed(true);
}

std::unique_ptr<NotificationServer> NotificationsSmokeTest::makeServer()
{
    // A fresh, uniquely-named peer connection per server so two servers never
    // collide on the spec object path within one connection.
    const QString name = QStringLiteral("pz-notif-test-%1").arg(m_connCounter++);
    QDBusConnection client = QDBusConnection::connectToPeer(m_peer->address(), name);

    // Pump the handshake. The logic under test does not actually depend on the
    // transport completing (the slots are called directly), so a timeout is not
    // fatal; it just gives registerObject a connected transport when available.
    QDeadlineTimer deadline(2000);
    while (!client.isConnected() && !deadline.hasExpired())
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);

    return std::make_unique<NotificationServer>(client, NotificationServer::serviceName());
}

void NotificationsSmokeTest::registerQmlTypesIsIdempotent()
{
    // The std::call_once guard must make a second call a no-op (no crash, no
    // duplicate-registration fault). A hot-reloading shell relies on this.
    registerQmlTypes();
    registerQmlTypes();
}

void NotificationsSmokeTest::staticIdentifiers()
{
    QCOMPARE(NotificationServer::serviceName(), QStringLiteral("org.freedesktop.Notifications"));
    QCOMPARE(NotificationServer::objectPath(), QStringLiteral("/org/freedesktop/Notifications"));
}

void NotificationsSmokeTest::serverInformation()
{
    auto server = makeServer();
    QString vendor;
    QString version;
    QString specVersion;
    const QString name = server->GetServerInformation(vendor, version, specVersion);
    QCOMPARE(name, QStringLiteral("Phosphor"));
    QCOMPARE(vendor, QStringLiteral("phosphor-works"));
    QCOMPARE(specVersion, QStringLiteral("1.3"));
    QVERIFY(!version.isEmpty());
}

void NotificationsSmokeTest::capabilitiesAdvertiseExactSet()
{
    auto server = makeServer();
    const QStringList caps = server->GetCapabilities();
    // body / actions / icon-static / persistence are honestly backed today;
    // body-markup stays out until a renderer exists (GetCapabilities documents
    // the same). Pin the full set so a capability silently dropped from — or
    // added to — the server is caught.
    QCOMPARE(QSet<QString>(caps.begin(), caps.end()),
             (QSet<QString>{QStringLiteral("body"), QStringLiteral("actions"), QStringLiteral("icon-static"),
                            QStringLiteral("persistence")}));
}

void NotificationsSmokeTest::notifyAllocatesMonotonicNonZeroIds()
{
    auto server = makeServer();
    const uint first = server->Notify(QStringLiteral("app"), 0, QString(), QStringLiteral("hi"), QString(), {}, {}, -1);
    const uint second =
        server->Notify(QStringLiteral("app"), 0, QString(), QStringLiteral("again"), QString(), {}, {}, -1);
    QVERIFY(first != 0);
    QVERIFY(second != 0);
    QVERIFY(second > first);
}

void NotificationsSmokeTest::notifyHonoursReplacesId()
{
    auto server = makeServer();
    const uint id = server->Notify(QStringLiteral("app"), 0, QString(), QStringLiteral("first"), QString(), {}, {}, -1);

    // replaces_id pointing at a live notification reuses that id in place.
    const uint replaced =
        server->Notify(QStringLiteral("app"), id, QString(), QStringLiteral("updated"), QString(), {}, {}, -1);
    QCOMPARE(replaced, id);

    // replaces_id pointing at an unknown id allocates a fresh one rather than
    // honouring the stale value.
    const uint stale = 999999;
    const uint fresh =
        server->Notify(QStringLiteral("app"), stale, QString(), QStringLiteral("new"), QString(), {}, {}, -1);
    QVERIFY(fresh != stale);
    QVERIFY(fresh != 0);
}

void NotificationsSmokeTest::closeNotificationEmitsForLiveIdOnly()
{
    auto server = makeServer();
    QSignalSpy spy(server.get(), &NotificationServer::NotificationClosed);
    QVERIFY(spy.isValid());

    const uint id =
        server->Notify(QStringLiteral("app"), 0, QString(), QStringLiteral("closeme"), QString(), {}, {}, -1);

    // Closing a live id emits exactly once with reason 3 (closed by call).
    server->CloseNotification(id);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toUInt(), id);
    QCOMPARE(spy.at(0).at(1).toUInt(), 3u);

    // Closing the same (now dead) id again is a no-op: no further signal.
    server->CloseNotification(id);
    QCOMPARE(spy.count(), 1);

    // Closing an id that was never issued is a no-op.
    server->CloseNotification(424242);
    QCOMPARE(spy.count(), 1);
}

void NotificationsSmokeTest::notifyDecodesScalarHints()
{
    auto server = makeServer();
    Notification* added = nullptr;
    connect(server.get(), &NotificationServer::notificationAdded, this, [&added](Notification* n) {
        added = n;
    });

    QVariantMap hints;
    hints.insert(QStringLiteral("urgency"), QVariant::fromValue<uint>(2));
    hints.insert(QStringLiteral("category"), QStringLiteral("im.received"));
    hints.insert(QStringLiteral("desktop-entry"), QStringLiteral("org.phosphor.Chat"));
    hints.insert(QStringLiteral("transient"), true);
    hints.insert(QStringLiteral("resident"), true);
    hints.insert(QStringLiteral("suppress-sound"), true);
    hints.insert(QStringLiteral("value"), 42);

    server->Notify(QStringLiteral("Chat"), 0, QString(), QStringLiteral("Ping"), QStringLiteral("hello"),
                   {QStringLiteral("default"), QStringLiteral("Open")}, hints, 5000);

    QVERIFY(added != nullptr);
    QCOMPARE(added->appName(), QStringLiteral("Chat"));
    QCOMPARE(added->summary(), QStringLiteral("Ping"));
    QCOMPARE(added->body(), QStringLiteral("hello"));
    QCOMPARE(added->actions(), (QStringList{QStringLiteral("default"), QStringLiteral("Open")}));
    QCOMPARE(added->urgency(), Notification::Critical);
    QCOMPARE(added->category(), QStringLiteral("im.received"));
    QCOMPARE(added->desktopEntry(), QStringLiteral("org.phosphor.Chat"));
    QVERIFY(added->transient());
    QVERIFY(added->resident());
    QVERIFY(added->suppressSound());
    QCOMPARE(added->value(), 42);
    QCOMPARE(added->expireTimeout(), 5000);
    QVERIFY(added->timestamp().isValid());
    // The raw hint map is retained for advanced bindings.
    QCOMPARE(added->hints().value(QStringLiteral("category")).toString(), QStringLiteral("im.received"));
}

void NotificationsSmokeTest::notifyDefaultsWhenHintsAbsent()
{
    auto server = makeServer();
    Notification* added = nullptr;
    connect(server.get(), &NotificationServer::notificationAdded, this, [&added](Notification* n) {
        added = n;
    });

    server->Notify(QStringLiteral("app"), 0, QString(), QStringLiteral("bare"), QString(), {}, {}, -1);

    QVERIFY(added != nullptr);
    QCOMPARE(added->urgency(), Notification::Normal); // default when no urgency hint
    QCOMPARE(added->value(), -1); // -1 sentinel when no value hint
    QVERIFY(!added->transient());
    QVERIFY(!added->resident());
    QVERIFY(!added->suppressSound());
    QVERIFY(added->category().isEmpty());
    QVERIFY(added->image().isNull());
    QVERIFY(!added->hasImage());
}

void NotificationsSmokeTest::notifyDecodesImagePathFromFile()
{
    // The image-path -> file branch is fully in-process and deterministic (the
    // image-data struct branch is covered by the milestone-8 wire test).
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("art.png"));
    QImage source(4, 3, QImage::Format_RGB888);
    source.fill(Qt::red);
    QVERIFY(source.save(path, "PNG"));

    auto server = makeServer();
    Notification* added = nullptr;
    connect(server.get(), &NotificationServer::notificationAdded, this, [&added](Notification* n) {
        added = n;
    });

    QVariantMap hints;
    hints.insert(QStringLiteral("image-path"), path);
    server->Notify(QStringLiteral("app"), 0, QString(), QStringLiteral("art"), QString(), {}, hints, -1);

    QVERIFY(added != nullptr);
    QVERIFY(added->hasImage());
    QCOMPARE(added->image().size(), QSize(4, 3));
}

void NotificationsSmokeTest::notifyImagePathMissingIsGraceful()
{
    auto server = makeServer();
    Notification* added = nullptr;
    connect(server.get(), &NotificationServer::notificationAdded, this, [&added](Notification* n) {
        added = n;
    });

    QVariantMap hints;
    hints.insert(QStringLiteral("image-path"), QStringLiteral("/nonexistent/phosphor/does-not-exist.png"));
    server->Notify(QStringLiteral("app"), 0, QString(), QStringLiteral("noimg"), QString(), {}, hints, -1);

    QVERIFY(added != nullptr);
    QVERIFY(added->image().isNull()); // missing file decodes to a null image, no crash
}

void NotificationsSmokeTest::replacesIdUpdatesInPlaceAndEmitsChanged()
{
    auto server = makeServer();
    Notification* added = nullptr;
    int addedCount = 0;
    connect(server.get(), &NotificationServer::notificationAdded, this, [&](Notification* n) {
        added = n;
        ++addedCount;
    });

    const uint id = server->Notify(QStringLiteral("app"), 0, QString(), QStringLiteral("first"),
                                   QStringLiteral("body1"), {}, {}, -1);
    QVERIFY(added != nullptr);
    QCOMPARE(addedCount, 1);
    QSignalSpy changedSpy(added, &Notification::changed);
    QVERIFY(changedSpy.isValid());
    Notification* original = added;

    const uint replaced = server->Notify(QStringLiteral("app"), id, QString(), QStringLiteral("second"),
                                         QStringLiteral("body2"), {}, {}, -1);

    QCOMPARE(replaced, id);
    QCOMPARE(addedCount, 1); // a replace is NOT a new add
    QCOMPARE(changedSpy.count(), 1); // it mutates in place and fires changed() once
    QCOMPARE(original->summary(), QStringLiteral("second"));
    QCOMPARE(original->body(), QStringLiteral("body2"));
    // The same object identity is preserved across the replace.
    QCOMPARE(server->notifications().size(), 1);
    QCOMPARE(server->notifications().constFirst(), original);
}

void NotificationsSmokeTest::notificationsAccessorTracksLifecycle()
{
    auto server = makeServer();
    const uint a = server->Notify(QStringLiteral("app"), 0, QString(), QStringLiteral("a"), QString(), {}, {}, -1);
    const uint b = server->Notify(QStringLiteral("app"), 0, QString(), QStringLiteral("b"), QString(), {}, {}, -1);

    auto live = server->notifications();
    QCOMPARE(live.size(), 2);
    // Ascending id order (QMap-backed).
    QCOMPARE(live.at(0)->id(), a);
    QCOMPARE(live.at(1)->id(), b);

    server->CloseNotification(a);
    live = server->notifications();
    QCOMPARE(live.size(), 1);
    QCOMPARE(live.at(0)->id(), b);
}

void NotificationsSmokeTest::explicitTimeoutExpiresWithReasonExpired()
{
    auto server = makeServer();
    QSignalSpy spy(server.get(), &NotificationServer::NotificationClosed);
    const uint id = server->Notify(QStringLiteral("app"), 0, QString(), QStringLiteral("ttl"), QString(), {}, {}, 30);

    QVERIFY(spy.wait(2000)); // the 30ms timer fires once the event loop runs
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toUInt(), id);
    QCOMPARE(spy.at(0).at(1).toUInt(), 1u); // reason 1 == expired
    QVERIFY(server->notifications().isEmpty());
}

void NotificationsSmokeTest::timeoutZeroNeverExpires()
{
    auto server = makeServer();
    QSignalSpy spy(server.get(), &NotificationServer::NotificationClosed);
    server->Notify(QStringLiteral("app"), 0, QString(), QStringLiteral("sticky"), QString(), {}, {}, 0);

    QVERIFY(!spy.wait(200)); // expire_timeout 0 means never; no close arrives
    QCOMPARE(server->notifications().size(), 1);
}

void NotificationsSmokeTest::defaultTimeoutExpiresForNormal()
{
    auto server = makeServer();
    server->setDefaultExpireTimeout(30); // shrink the -1 default for the test
    QSignalSpy spy(server.get(), &NotificationServer::NotificationClosed);
    // expire_timeout -1 + Normal urgency (no hint) resolves to the default.
    server->Notify(QStringLiteral("app"), 0, QString(), QStringLiteral("def"), QString(), {}, {}, -1);

    QVERIFY(spy.wait(2000));
    QCOMPARE(spy.at(0).at(1).toUInt(), 1u);
    QVERIFY(server->notifications().isEmpty());
}

void NotificationsSmokeTest::criticalDefaultNeverExpires()
{
    auto server = makeServer();
    server->setDefaultExpireTimeout(30);
    QSignalSpy spy(server.get(), &NotificationServer::NotificationClosed);

    QVariantMap hints;
    hints.insert(QStringLiteral("urgency"), QVariant::fromValue<uint>(2)); // Critical
    // -1 + Critical resolves to "never", ignoring the (short) default.
    server->Notify(QStringLiteral("app"), 0, QString(), QStringLiteral("crit"), QString(), {}, hints, -1);

    QVERIFY(!spy.wait(200));
    QCOMPARE(server->notifications().size(), 1);
}

void NotificationsSmokeTest::dismissEmitsReasonDismissed()
{
    auto server = makeServer();
    QSignalSpy spy(server.get(), &NotificationServer::NotificationClosed);
    const uint id = server->Notify(QStringLiteral("app"), 0, QString(), QStringLiteral("d"), QString(), {}, {}, 0);

    server->dismissNotification(id);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toUInt(), id);
    QCOMPARE(spy.at(0).at(1).toUInt(), 2u); // reason 2 == dismissed
    QVERIFY(server->notifications().isEmpty());
}

void NotificationsSmokeTest::invokeActionEmitsAndDismissesNonResident()
{
    auto server = makeServer();
    QSignalSpy actionSpy(server.get(), &NotificationServer::ActionInvoked);
    QSignalSpy closeSpy(server.get(), &NotificationServer::NotificationClosed);
    const uint id = server->Notify(QStringLiteral("app"), 0, QString(), QStringLiteral("a"), QString(),
                                   {QStringLiteral("default"), QStringLiteral("Open")}, {}, 0);

    server->invokeAction(id, QStringLiteral("default"));

    QCOMPARE(actionSpy.count(), 1);
    QCOMPARE(actionSpy.at(0).at(0).toUInt(), id);
    QCOMPARE(actionSpy.at(0).at(1).toString(), QStringLiteral("default"));
    // A non-resident notification is dismissed after the action.
    QCOMPARE(closeSpy.count(), 1);
    QCOMPARE(closeSpy.at(0).at(1).toUInt(), 2u);
    QVERIFY(server->notifications().isEmpty());
}

void NotificationsSmokeTest::invokeActionKeepsResidentOpen()
{
    auto server = makeServer();
    QSignalSpy actionSpy(server.get(), &NotificationServer::ActionInvoked);
    QSignalSpy closeSpy(server.get(), &NotificationServer::NotificationClosed);

    QVariantMap hints;
    hints.insert(QStringLiteral("resident"), true);
    const uint id = server->Notify(QStringLiteral("app"), 0, QString(), QStringLiteral("r"), QString(),
                                   {QStringLiteral("more")}, hints, 0);

    server->invokeAction(id, QStringLiteral("more"));

    QCOMPARE(actionSpy.count(), 1);
    QCOMPARE(closeSpy.count(), 0); // resident stays open
    QCOMPARE(server->notifications().size(), 1);
}

void NotificationsSmokeTest::invokeActionEmitsActivationTokenBeforeAction()
{
    auto server = makeServer();
    QStringList sequence;
    connect(server.get(), &NotificationServer::ActivationToken, this, [&sequence](uint, const QString&) {
        sequence << QStringLiteral("token");
    });
    connect(server.get(), &NotificationServer::ActionInvoked, this, [&sequence](uint, const QString&) {
        sequence << QStringLiteral("action");
    });
    QSignalSpy tokenSpy(server.get(), &NotificationServer::ActivationToken);

    const uint id = server->Notify(QStringLiteral("app"), 0, QString(), QStringLiteral("a"), QString(),
                                   {QStringLiteral("default")}, {}, 0);
    server->invokeAction(id, QStringLiteral("default"), QStringLiteral("tok-123"));

    QCOMPARE(tokenSpy.count(), 1);
    QCOMPARE(tokenSpy.at(0).at(1).toString(), QStringLiteral("tok-123"));
    // The activation token must precede the action so the app can raise first.
    QCOMPARE(sequence, (QStringList{QStringLiteral("token"), QStringLiteral("action")}));
}

void NotificationsSmokeTest::replaceUpdatesExpiry()
{
    auto server = makeServer();
    QSignalSpy spy(server.get(), &NotificationServer::NotificationClosed);
    // Arm a short timer, then replace it with a never-expire (0). This is not a
    // timing race: both Notify calls are synchronous with no event-loop pump
    // between them, so the 40ms timer's slot cannot run before the replace
    // cancels the timer (armExpiry removes it). The 40ms is illustrative only.
    const uint id = server->Notify(QStringLiteral("app"), 0, QString(), QStringLiteral("first"), QString(), {}, {}, 40);
    server->Notify(QStringLiteral("app"), id, QString(), QStringLiteral("second"), QString(), {}, {}, 0);

    QVERIFY(!spy.wait(300)); // the replace cancelled the original countdown
    QCOMPARE(server->notifications().size(), 1);
}

void NotificationsSmokeTest::invokeActionUnknownIdIsNoop()
{
    auto server = makeServer();
    QSignalSpy actionSpy(server.get(), &NotificationServer::ActionInvoked);
    server->invokeAction(999999, QStringLiteral("default")); // no such notification
    QCOMPARE(actionSpy.count(), 0);
}

void NotificationsSmokeTest::modelSeedsFromServer()
{
    auto server = makeServer();
    const uint a =
        server->Notify(QStringLiteral("AppA"), 0, QString(), QStringLiteral("first"), QStringLiteral("b1"), {}, {}, 0);
    server->Notify(QStringLiteral("AppB"), 0, QString(), QStringLiteral("second"), QString(), {}, {}, 0);

    NotificationModel model;
    QAbstractItemModelTester tester(&model); // pins the QAbstractItemModel contract
    model.setServer(server.get());

    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.property("count").toInt(), 2);
    QCOMPARE(model.data(model.index(0), NotificationModel::IdRole).toUInt(), a);
    QCOMPARE(model.data(model.index(0), NotificationModel::AppNameRole).toString(), QStringLiteral("AppA"));
    QCOMPARE(model.data(model.index(0), NotificationModel::SummaryRole).toString(), QStringLiteral("first"));
    QCOMPARE(model.data(model.index(1), NotificationModel::AppNameRole).toString(), QStringLiteral("AppB"));
}

void NotificationsSmokeTest::modelInsertsOnAdd()
{
    auto server = makeServer();
    NotificationModel model;
    QAbstractItemModelTester tester(&model);
    model.setServer(server.get());
    QCOMPARE(model.rowCount(), 0);

    QSignalSpy insertSpy(&model, &QAbstractListModel::rowsInserted);
    QSignalSpy countSpy(&model, &NotificationModel::countChanged);

    const uint id = server->Notify(QStringLiteral("app"), 0, QString(), QStringLiteral("new"), QString(), {}, {}, 0);

    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(insertSpy.count(), 1);
    QCOMPARE(countSpy.count(), 1);
    QCOMPARE(model.data(model.index(0), NotificationModel::IdRole).toUInt(), id);
}

void NotificationsSmokeTest::modelRemovesOnClose()
{
    auto server = makeServer();
    NotificationModel model;
    QAbstractItemModelTester tester(&model);
    model.setServer(server.get());
    const uint id = server->Notify(QStringLiteral("app"), 0, QString(), QStringLiteral("x"), QString(), {}, {}, 0);
    QCOMPARE(model.rowCount(), 1);

    QSignalSpy removeSpy(&model, &QAbstractListModel::rowsRemoved);
    server->CloseNotification(id);

    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(removeSpy.count(), 1);
}

void NotificationsSmokeTest::modelDataChangedOnReplace()
{
    auto server = makeServer();
    NotificationModel model;
    QAbstractItemModelTester tester(&model);
    model.setServer(server.get());
    const uint id = server->Notify(QStringLiteral("app"), 0, QString(), QStringLiteral("before"), QString(), {}, {}, 0);

    QSignalSpy dataSpy(&model, &QAbstractListModel::dataChanged);
    QSignalSpy insertSpy(&model, &QAbstractListModel::rowsInserted);
    QSignalSpy removeSpy(&model, &QAbstractListModel::rowsRemoved);

    server->Notify(QStringLiteral("app"), id, QString(), QStringLiteral("after"), QString(), {}, {}, 0);

    QCOMPARE(model.rowCount(), 1); // replace, not insert
    QCOMPARE(insertSpy.count(), 0);
    QCOMPARE(removeSpy.count(), 0);
    QCOMPARE(dataSpy.count(), 1);
    QCOMPARE(model.data(model.index(0), NotificationModel::SummaryRole).toString(), QStringLiteral("after"));
}

void NotificationsSmokeTest::modelRoleNamesPinned()
{
    // Pin ALL role-name mappings, not a sample — QML delegates key on the
    // name strings, so a rename or reorder anywhere in the enum silently
    // breaks bindings (mirrors the exhaustive pinning in the pipewire
    // smoke test).
    NotificationModel model;
    const QHash<int, QByteArray> roles = model.roleNames();
    struct RolePin
    {
        NotificationModel::Roles role;
        const char* name;
    };
    const RolePin pins[] = {
        {NotificationModel::NotificationRole, "notification"},
        {NotificationModel::IdRole, "id"},
        {NotificationModel::AppNameRole, "appName"},
        {NotificationModel::AppIconRole, "appIcon"},
        {NotificationModel::SummaryRole, "summary"},
        {NotificationModel::BodyRole, "body"},
        {NotificationModel::ActionsRole, "actions"},
        {NotificationModel::UrgencyRole, "urgency"},
        {NotificationModel::CategoryRole, "category"},
        {NotificationModel::DesktopEntryRole, "desktopEntry"},
        {NotificationModel::ImageRole, "image"},
        {NotificationModel::HasImageRole, "hasImage"},
        {NotificationModel::ResidentRole, "resident"},
        {NotificationModel::TransientRole, "transient"},
        {NotificationModel::SuppressSoundRole, "suppressSound"},
        {NotificationModel::ValueRole, "value"},
        {NotificationModel::ExpireTimeoutRole, "expireTimeout"},
        {NotificationModel::TimestampRole, "timestamp"},
    };
    QCOMPARE(roles.size(), static_cast<int>(std::size(pins)));
    for (const RolePin& pin : pins) {
        QCOMPARE(roles.value(pin.role), QByteArray(pin.name));
    }
    // Enum is contiguous from Qt::UserRole + 1.
    QCOMPARE(static_cast<int>(NotificationModel::NotificationRole), static_cast<int>(Qt::UserRole) + 1);
    QCOMPARE(static_cast<int>(NotificationModel::TimestampRole),
             static_cast<int>(Qt::UserRole) + static_cast<int>(std::size(pins)));
}

void NotificationsSmokeTest::modelClearsWhenServerDestroyed()
{
    auto server = makeServer();
    server->Notify(QStringLiteral("app"), 0, QString(), QStringLiteral("x"), QString(), {}, {}, 0);

    NotificationModel model;
    QAbstractItemModelTester tester(&model);
    model.setServer(server.get());
    QCOMPARE(model.rowCount(), 1);

    // Destroying the server (its owner) must reset the model rather than dangle.
    server.reset();
    QCOMPARE(model.rowCount(), 0);
    QVERIFY(model.server() == nullptr);
}

QTEST_GUILESS_MAIN(NotificationsSmokeTest)
#include "test_smoke.moc"
