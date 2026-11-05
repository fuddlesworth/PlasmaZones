// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_compositor_only_gate.cpp
 * @brief DbusHelpers::admitCompositorReport admits the registered compositor
 *        bridge and no other bus caller (#1155), registerBridge admits only
 *        the compositor's process and keeps one bridge at a time, and every
 *        method the interface XML documents as compositor-only gates its slot.
 *
 * The adaptors are exported through a BusHost, as Daemon exports them, beside
 * a CompositorBridgeAdaptor, and called by second connections on the private
 * test bus.
 *
 *  1. Before any bridge registered over the bus, every bus caller is refused
 *     with AccessDenied and the slot does nothing, a call naming no interface
 *     included; a direct call always runs.
 *  2. The peer that registered is admitted and another is refused.
 *  3. A slot reached from inside another bus call is admitted, whether that
 *     call is a slot of the same interface or one of another interface with
 *     the same member name: the outer slot answers for its caller.
 *  4. A second peer cannot register while the first is on the bus. Once the
 *     first has left, the second registers and the first name is refused.
 *  5. Registration over the bus is held to the compositor identity check:
 *     with none installed every one is refused, and a peer that fails it
 *     cannot register first and keep the compositor out.
 *  6. CompositorBridge.reportGesture and reportModifierState answer to the
 *     same gate, reportGesture before anything else.
 *  7. A source tripwire over the shipped adaptors: every method an interface
 *     XML documents as compositor-only (AccessDenied for any caller but the
 *     compositor) gates its slot, and every gated slot whose method the XML
 *     lists is documented so.
 */

#include <QDBusAbstractAdaptor>
#include <QDBusConnectionInterface>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTest>
#include <QXmlStreamReader>

#include <PhosphorProtocol/BridgeMarshalling.h>
#include <PhosphorProtocol/Registration.h>
#include <PhosphorProtocol/ServiceConstants.h>

#include "dbus/compositorbridgeadaptor.h"
#include "dbus/dbushelpers.h"

#include "helpers/BusPeer.h"

#include <algorithm>
#include <optional>

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::BusHost;
using PlasmaZones::TestHelpers::BusPeer;
using PlasmaZones::TestHelpers::isBusError;

namespace CompositorGateTestSupport {

const QString BridgeInterface = QStringLiteral("org.plasmazones.CompositorBridge");
const QString ProbeInterface = QStringLiteral("org.plasmazones.test.GateProbe");
const QString OtherInterface = QStringLiteral("org.plasmazones.test.OtherProbe");
const QString Rejected = QStringLiteral("REJECTED");

/// An adaptor with one compositor-only slot and one open slot that reaches it.
class GateProbe : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.plasmazones.test.GateProbe")

public:
    explicit GateProbe(QObject* parent)
        : QDBusAbstractAdaptor(parent)
    {
    }
    int reports = 0;

public Q_SLOTS:
    void report()
    {
        if (!DbusHelpers::admitCompositorReport(this, QLatin1StringView(__func__))) {
            return;
        }
        ++reports;
    }
    /// Open to any caller, and reaches the gated slot of the same interface
    /// in-process, as a slot that forwards to a compositor-only sibling does.
    void outer()
    {
        report();
    }
};

/// Another interface with an open slot of the gated slot's member name, which
/// reaches the gated one in-process: only the interface tells the two calls
/// apart.
class OtherProbe : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.plasmazones.test.OtherProbe")

public:
    OtherProbe(QObject* parent, GateProbe* gated)
        : QDBusAbstractAdaptor(parent)
        , m_gated(gated)
    {
    }

public Q_SLOTS:
    void report()
    {
        m_gated->report();
    }

private:
    GateProbe* const m_gated;
};

struct Fixture
{
    /// With @p identityCheck, every registrant passes the compositor identity
    /// check, so a test can register an ordinary bus peer.
    explicit Fixture(bool identityCheck = true)
    {
        if (identityCheck) {
            bridge->setCompositorIdentityCheck([](const QString&) {
                return true;
            });
        }
    }
    BusHost host;
    CompositorBridgeAdaptor* bridge = new CompositorBridgeAdaptor(&host);
    GateProbe* probe = new GateProbe(&host);
};

/// Registers @p peer as the bridge over the bus: the session id the daemon
/// handed back ("REJECTED" for a refused registration), or nullopt when no
/// reply came, which no assertion may read as an answer.
std::optional<QString> registerOverBus(BusPeer& peer, const QStringList& capabilities = {})
{
    const QDBusMessage reply =
        peer.call(BridgeInterface, QStringLiteral("registerBridge"),
                  {QStringLiteral("kwin"), QString::number(PhosphorProtocol::Service::ApiVersion), capabilities});
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        return std::nullopt;
    }
    return qdbus_cast<PhosphorProtocol::BridgeRegistrationResult>(reply.arguments().constFirst()).sessionId;
}

/// The registration was answered and accepted.
bool registers(BusPeer& peer, const QStringList& capabilities = {})
{
    const std::optional<QString> session = registerOverBus(peer, capabilities);
    return session && !session->isEmpty() && *session != Rejected;
}

/// The registration was answered and refused.
bool isRefused(BusPeer& peer)
{
    const std::optional<QString> session = registerOverBus(peer);
    return session && *session == Rejected;
}

QString readText(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly | QIODevice::Text) ? QString::fromUtf8(file.readAll()) : QString();
}

QStringList filesUnder(const QString& directory, const QStringList& nameFilters)
{
    QStringList files;
    QDirIterator it(directory, nameFilters, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        files.append(it.next());
    }
    return files;
}

/// (interface, method) → the method's own DocString, over every interface XML
/// in @p xmlDirectory. An argument's DocString is not the method's.
QHash<QPair<QString, QString>, QString> methodDocs(const QString& xmlDirectory)
{
    QHash<QPair<QString, QString>, QString> docs;
    for (const QString& path : filesUnder(xmlDirectory, {QStringLiteral("*.xml")})) {
        QXmlStreamReader xml(readText(path));
        QString interface;
        QString method;
        bool inArg = false;
        while (!xml.atEnd()) {
            const QXmlStreamReader::TokenType token = xml.readNext();
            if (token == QXmlStreamReader::StartElement) {
                const QStringView name = xml.name();
                const QXmlStreamAttributes attributes = xml.attributes();
                if (name == QLatin1String("interface")) {
                    interface = attributes.value(QLatin1String("name")).toString();
                } else if (name == QLatin1String("method")) {
                    method = attributes.value(QLatin1String("name")).toString();
                    docs.insert({interface, method}, QString());
                } else if (name == QLatin1String("arg")) {
                    inArg = true;
                } else if (name == QLatin1String("annotation") && !method.isEmpty() && !inArg
                           && attributes.value(QLatin1String("name")) == QLatin1String("org.gtk.GDBus.DocString")) {
                    docs.insert({interface, method}, attributes.value(QLatin1String("value")).toString());
                }
            } else if (token == QXmlStreamReader::EndElement) {
                if (xml.name() == QLatin1String("arg")) {
                    inArg = false;
                } else if (xml.name() == QLatin1String("method")) {
                    method.clear();
                }
            }
        }
    }
    return docs;
}

} // namespace CompositorGateTestSupport

using namespace CompositorGateTestSupport;

class TestCompositorOnlyGate : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        PhosphorProtocol::registerWireTypes();
    }

    void beforeRegistrationEveryBusCallerIsRefused()
    {
        Fixture f;
        BusPeer peer(&f.host, QStringLiteral("Early"));
        QVERIFY(peer.ready());

        const QDBusMessage reply = peer.call(ProbeInterface, QStringLiteral("report"));
        QVERIFY2(isBusError(reply, QDBusError::AccessDenied), qPrintable(reply.errorMessage()));
        QCOMPARE(f.probe->reports, 0);

        // A call naming no interface is dispatched by member name, and gated
        // the same way.
        const QDBusMessage unnamed = peer.call(QString(), QStringLiteral("report"));
        QVERIFY2(isBusError(unnamed, QDBusError::AccessDenied), qPrintable(unnamed.errorMessage()));
        QCOMPARE(f.probe->reports, 0);

        f.probe->report();
        QCOMPARE(f.probe->reports, 1);
    }

    void theRegisteredPeerIsAdmittedAndAnotherIsRefused()
    {
        Fixture f;
        BusPeer bridgePeer(&f.host, QStringLiteral("Bridge"));
        BusPeer stranger(&f.host, QStringLiteral("Stranger"));
        QVERIFY(bridgePeer.ready() && stranger.ready());
        QVERIFY(registers(bridgePeer));

        const QDBusMessage admitted = bridgePeer.call(ProbeInterface, QStringLiteral("report"));
        QCOMPARE(admitted.type(), QDBusMessage::ReplyMessage);
        QCOMPARE(f.probe->reports, 1);

        const QDBusMessage refused = stranger.call(ProbeInterface, QStringLiteral("report"));
        QVERIFY2(isBusError(refused, QDBusError::AccessDenied), qPrintable(refused.errorMessage()));
        const QDBusMessage unnamed = stranger.call(QString(), QStringLiteral("report"));
        QVERIFY2(isBusError(unnamed, QDBusError::AccessDenied), qPrintable(unnamed.errorMessage()));
        QCOMPARE(f.probe->reports, 1);
    }

    void aSlotReachedFromAnotherCallIsAdmitted()
    {
        Fixture f;
        BusPeer stranger(&f.host, QStringLiteral("Outer"));
        QVERIFY(stranger.ready());

        const QDBusMessage reply = stranger.call(ProbeInterface, QStringLiteral("outer"));
        QCOMPARE(reply.type(), QDBusMessage::ReplyMessage);
        QCOMPARE(f.probe->reports, 1);
    }

    // The member matches and only the interface differs, so this passes only
    // through the gate's own-interface test.
    void aSlotReachedFromAnotherInterfaceIsAdmitted()
    {
        Fixture f;
        new OtherProbe(&f.host, f.probe);
        BusPeer stranger(&f.host, QStringLiteral("Cross"));
        QVERIFY(stranger.ready());

        const QDBusMessage reply = stranger.call(OtherInterface, QStringLiteral("report"));
        QVERIFY2(reply.type() == QDBusMessage::ReplyMessage, qPrintable(reply.errorMessage()));
        QCOMPARE(f.probe->reports, 1);
    }

    void oneBridgeAtATime()
    {
        Fixture f;
        BusPeer first(&f.host, QStringLiteral("First"));
        BusPeer second(&f.host, QStringLiteral("Second"));
        QVERIFY(first.ready() && second.ready());
        const QString firstName = first.service();
        QVERIFY(registers(first));

        QVERIFY(isRefused(second));
        QVERIFY(f.bridge->isBridgeService(firstName));
        QVERIFY2(isBusError(second.call(ProbeInterface, QStringLiteral("report")), QDBusError::AccessDenied),
                 "a refused registration admits nothing");

        // The same peer registering again, as the effect does on a reload.
        QVERIFY(registers(first));
        QVERIFY(f.bridge->isBridgeService(firstName));

        // Gone from the bus, as a KWin that exited is: the next one registers.
        first.leave();
        QTRY_VERIFY(!QDBusConnection::sessionBus().interface()->isServiceRegistered(firstName));
        QVERIFY(registers(second));
        QVERIFY(f.bridge->isBridgeService(second.service()));
        QVERIFY(!f.bridge->isBridgeService(firstName));
        QCOMPARE(second.call(ProbeInterface, QStringLiteral("report")).type(), QDBusMessage::ReplyMessage);
        QCOMPARE(f.probe->reports, 1);
    }

    // With no identity check installed, no registration over the bus is taken.
    void registrationWithoutAnIdentityCheckIsRefused()
    {
        Fixture f(/*identityCheck=*/false);
        BusPeer peer(&f.host, QStringLiteral("Unchecked"));
        QVERIFY(peer.ready());

        QVERIFY(isRefused(peer));
        QVERIFY(!f.bridge->isBridgeRegistered());
        QVERIFY(!f.bridge->isBridgeService(peer.service()));
        QVERIFY(isBusError(peer.call(ProbeInterface, QStringLiteral("report")), QDBusError::AccessDenied));
    }

    // First come used to be enough: a peer registered before the compositor
    // held the registration while it stayed on the bus, and the compositor's
    // own registration was refused as the second one.
    void aPeerThatIsNotTheCompositorCannotRegisterFirst()
    {
        Fixture f;
        BusPeer squatter(&f.host, QStringLiteral("Squatter"));
        BusPeer compositor(&f.host, QStringLiteral("Compositor"));
        QVERIFY(squatter.ready() && compositor.ready());
        f.bridge->setCompositorIdentityCheck([compositorName = compositor.service()](const QString& busName) {
            return busName == compositorName;
        });

        QVERIFY(isRefused(squatter));
        QVERIFY(!f.bridge->isBridgeService(squatter.service()));

        QVERIFY2(registers(compositor), "the compositor registers with the squatter still on the bus");
        QVERIFY(f.bridge->isBridgeService(compositor.service()));
        QVERIFY(isBusError(squatter.call(ProbeInterface, QStringLiteral("report")), QDBusError::AccessDenied));
        QCOMPARE(compositor.call(ProbeInterface, QStringLiteral("report")).type(), QDBusMessage::ReplyMessage);
        QCOMPARE(f.probe->reports, 1);
    }

    void aGestureFromAStrangerIsRefused()
    {
        Fixture f;
        BusPeer bridgePeer(&f.host, QStringLiteral("GestureBridge"));
        BusPeer stranger(&f.host, QStringLiteral("GestureStranger"));
        QVERIFY(bridgePeer.ready() && stranger.ready());
        QVERIFY(registers(bridgePeer, {QStringLiteral("gestures")}));
        QSignalSpy gestures(f.bridge, &CompositorBridgeAdaptor::gestureReported);

        const QVariantList swipe{QStringLiteral("swipe"), QStringLiteral("up"), 3u};
        const QDBusMessage refused = stranger.call(BridgeInterface, QStringLiteral("reportGesture"), swipe);
        QVERIFY2(isBusError(refused, QDBusError::AccessDenied), qPrintable(refused.errorMessage()));
        QCOMPARE(gestures.count(), 0);

        QCOMPARE(bridgePeer.call(BridgeInterface, QStringLiteral("reportGesture"), swipe).type(),
                 QDBusMessage::ReplyMessage);
        QCOMPARE(gestures.count(), 1);
    }

    // The caller is checked before the capability: a stranger's gesture while
    // no gestures-capable bridge is registered used to get a silent success.
    void aGestureFromAStrangerIsRefusedWithoutTheCapability()
    {
        Fixture f;
        BusPeer bridgePeer(&f.host, QStringLiteral("PlainBridge"));
        BusPeer stranger(&f.host, QStringLiteral("PlainStranger"));
        QVERIFY(bridgePeer.ready() && stranger.ready());
        QVERIFY(registers(bridgePeer));

        const QVariantList swipe{QStringLiteral("swipe"), QStringLiteral("up"), 3u};
        const QDBusMessage refused = stranger.call(BridgeInterface, QStringLiteral("reportGesture"), swipe);
        QVERIFY2(isBusError(refused, QDBusError::AccessDenied), qPrintable(refused.errorMessage()));
    }

    void modifierStateFromAStrangerIsRefused()
    {
        Fixture f;
        BusPeer bridgePeer(&f.host, QStringLiteral("ModifierBridge"));
        BusPeer stranger(&f.host, QStringLiteral("ModifierStranger"));
        QVERIFY(bridgePeer.ready() && stranger.ready());
        QVERIFY(registers(bridgePeer, {QStringLiteral("modifiers")}));
        QSignalSpy modifiers(f.bridge, &CompositorBridgeAdaptor::modifierStateChanged);

        const QVariantList state{int(Qt::ShiftModifier), int(Qt::LeftButton)};
        const QDBusMessage refused = stranger.call(BridgeInterface, QStringLiteral("reportModifierState"), state);
        QVERIFY2(isBusError(refused, QDBusError::AccessDenied), qPrintable(refused.errorMessage()));
        QCOMPARE(modifiers.count(), 0);

        QCOMPARE(bridgePeer.call(BridgeInterface, QStringLiteral("reportModifierState"), state).type(),
                 QDBusMessage::ReplyMessage);
        QCOMPARE(modifiers.count(), 1);
    }

    // The gate is one statement in each slot and nothing ties it to the
    // documented contract. A slot that loses it, or a method documented as
    // compositor-only whose slot never had it, fails here. Scraped from the
    // sources: no daemon fixture constructs every adaptor.
    void documentedCompositorOnlyMethodsAreGated()
    {
        const QDir testDirectory = QFileInfo(QStringLiteral(__FILE__)).absoluteDir();
        const QString appRoot = QDir::cleanPath(testDirectory.filePath(QStringLiteral("../../..")));
        const QString repoRoot = QDir::cleanPath(appRoot + QStringLiteral("/.."));
        const QString xmlDirectory = appRoot + QStringLiteral("/dbus");
        const QString adaptorSources = appRoot + QStringLiteral("/src/dbus");
        const QString screenAdaptor = repoRoot + QStringLiteral("/phosphor-libs/libs/phosphor-screens");
        QVERIFY2(QFileInfo::exists(xmlDirectory + QStringLiteral("/org.plasmazones.Snap.xml")),
                 qPrintable(QStringLiteral("no interface XML under %1").arg(xmlDirectory)));
        QVERIFY2(QFileInfo::exists(screenAdaptor + QStringLiteral("/src/dbusscreenadaptor.cpp")),
                 qPrintable(QStringLiteral("no screen adaptor under %1").arg(screenAdaptor)));

        // Interface → adaptor class, from the headers' Q_CLASSINFO.
        QHash<QString, QString> classForInterface;
        const QRegularExpression classInfo(
            QStringLiteral("class\\s+(?:\\w+_EXPORT\\s+)?(\\w+)\\s*:\\s*public\\s+QDBusAbstractAdaptor[\\s\\S]*?"
                           "Q_CLASSINFO\\(\"D-Bus Interface\",\\s*\"([^\"]+)\"\\)"));
        QStringList headers = filesUnder(adaptorSources, {QStringLiteral("*.h")});
        headers.append(screenAdaptor + QStringLiteral("/include/PhosphorScreens/DBusScreenAdaptor.h"));
        for (const QString& header : std::as_const(headers)) {
            const QString text = readText(header);
            for (auto it = classInfo.globalMatch(text); it.hasNext();) {
                const QRegularExpressionMatch match = it.next();
                classForInterface.insert(match.captured(2), match.captured(1));
            }
        }
        QVERIFY(classForInterface.contains(QStringLiteral("org.plasmazones.Snap")));

        // (class, method) → the bodies of its out-of-line definitions, each
        // from the line naming it to the closing brace in the first column.
        QHash<QPair<QString, QString>, QStringList> bodies;
        const QRegularExpression definition(QStringLiteral("^(?=[^\\s#/])[^\\n;]*?\\b(\\w+)::(\\w+)\\("),
                                            QRegularExpression::MultilineOption);
        QStringList sources = filesUnder(adaptorSources, {QStringLiteral("*.cpp")});
        sources.append(screenAdaptor + QStringLiteral("/src/dbusscreenadaptor.cpp"));
        for (const QString& source : std::as_const(sources)) {
            const QString text = readText(source);
            for (auto it = definition.globalMatch(text); it.hasNext();) {
                const QRegularExpressionMatch match = it.next();
                const qsizetype end = text.indexOf(QLatin1String("\n}\n"), match.capturedStart());
                bodies[{match.captured(1), match.captured(2)}].append(
                    text.mid(match.capturedStart(), end < 0 ? -1 : end - match.capturedStart()));
            }
        }
        // Either gate the bus surface uses: the shared report gate (directly
        // or through the screen adaptor's injected one), or the thumbnail
        // entries' compositor-process check.
        const QRegularExpression anyGate(
            QStringLiteral("admitCompositorReport\\(|m_compositorReportGate\\(|authenticateKwinSender\\(\\)"));
        const QRegularExpression reportGate(QStringLiteral("admitCompositorReport\\(this|m_compositorReportGate\\("));
        const auto gated = [&bodies](const QString& className, const QString& method, const QRegularExpression& gate) {
            const QStringList found = bodies.value({className, method});
            return std::any_of(found.cbegin(), found.cend(), [&gate](const QString& body) {
                return gate.match(body).hasMatch();
            });
        };

        const QHash<QPair<QString, QString>, QString> docs = methodDocs(xmlDirectory);
        // Compositor-only is AccessDenied for a caller that is not the
        // compositor. AccessDenied alone is not: the drop proxy verbs answer
        // it to a peer that does not own the proxy, which is no compositor
        // gate.
        const auto compositorOnly = [](const QString& doc) {
            return doc.contains(QLatin1String("AccessDenied"))
                && (doc.contains(QLatin1String("registered compositor bridge"))
                    || doc.contains(QLatin1String("compositor's own process")));
        };
        QStringList problems;
        int documented = 0;
        for (auto it = docs.cbegin(); it != docs.cend(); ++it) {
            if (!compositorOnly(it.value())) {
                continue;
            }
            ++documented;
            const QString className = classForInterface.value(it.key().first);
            if (className.isEmpty()) {
                problems.append(QStringLiteral("no adaptor class for %1").arg(it.key().first));
            } else if (!gated(className, it.key().second, anyGate)) {
                problems.append(QStringLiteral("%1.%2 is documented compositor-only, but %3::%2 is not gated")
                                    .arg(it.key().first, it.key().second, className));
            }
        }
        for (auto it = classForInterface.cbegin(); it != classForInterface.cend(); ++it) {
            for (auto body = bodies.cbegin(); body != bodies.cend(); ++body) {
                if (body.key().first != it.value() || !gated(it.value(), body.key().second, reportGate)) {
                    continue;
                }
                // A gated slot the XML does not list is off-contract, which
                // test_dbus_contract_sync governs.
                const auto doc = docs.constFind({it.key(), body.key().second});
                if (doc != docs.cend() && !compositorOnly(doc.value())) {
                    problems.append(QStringLiteral("%1::%2 is gated, but %3.%2 is not documented compositor-only")
                                        .arg(it.value(), body.key().second, it.key()));
                }
            }
        }
        QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QLatin1Char('\n'))));
        QVERIFY2(compositorOnly(docs.value({QStringLiteral("org.plasmazones.Snap"), QStringLiteral("windowSnapped")})),
                 "the scrape found no documented compositor-only method; the check above is vacuous");
        QVERIFY(documented > 0);
    }
};

QTEST_GUILESS_MAIN(TestCompositorOnlyGate)
#include "test_compositor_only_gate.moc"
