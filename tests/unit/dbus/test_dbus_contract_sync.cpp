// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_dbus_contract_sync.cpp
 * @brief XML ↔ adaptor contract tripwire for the hand-written D-Bus adaptors.
 *
 * The XML files under dbus/ are install-only artifacts ("Keep in sync with
 * the XMLs on disk" — root CMakeLists), while the adaptors are handwritten
 * with Q_CLASSINFO. Nothing enforced the symmetry mechanically, and the
 * PR-608 audit caught exactly the drift class that invites: a capability
 * documented against a deleted method, internal helpers exposed as bus slots,
 * and renamed args. This test pins, for every linkable handwritten
 * (XML, adaptor) pair — the daemon's twelve src/dbus interfaces plus
 * phosphor-screens' org.plasmazones.Screen (thirteen in total):
 *
 *  1. Every XML method exists as a bus-exposed metaobject method with
 *     matching in/out argument types, names, and return mapping.
 *  2. Every bus-exposed metaobject method (public slots + Q_INVOKABLEs
 *     declared on the adaptor itself) appears in the XML — internal helpers
 *     must live in plain `public:` sections, never under Q_SLOTS.
 *  3. Signals match 1:1, modulo an explicit per-interface off-contract
 *     allowlist (e.g. WindowTracking's in-process windowClosedNotification).
 *  4. Q_PROPERTYs match the XML <property> entries (name, type, access).
 *
 * Bus-exposure model mirrors QDBusAbstractAdaptor's introspection: public
 * slots, public Q_INVOKABLEs, and signals declared on the adaptor subclass
 * are exported; plain public methods are not. Out-args follow QtDBus
 * convention: a non-void return is the FIRST out argument, then non-const
 * reference parameters in declaration order.
 *
 * NOT covered (documented gap, not an oversight): the settings/editor apps'
 * single-slot launch forwarders — SettingsAppAdaptor (org.plasmazones
 * .SettingsController, dbus/org.plasmazones.SettingsApp.xml) and
 * EditorAppAdaptor (org.plasmazones.EditorController, dbus/org.plasmazones
 * .EditorApp.xml). Their classes are app-internal (compiled into the app
 * binaries, not exported from any linkable target), so their
 * staticMetaObjects cannot be linked here without dragging each app's
 * controller graph into the test. Each is one forwarder slot; drift there
 * surfaces immediately as a launch failure in the owning app.
 */

#include <PhosphorProtocol/Registration.h>
#include <PhosphorScreens/DBusScreenAdaptor.h>

#include <QDBusMetaType>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QMetaMethod>
#include <QMetaProperty>
#include <QSet>
#include <QTest>
#include <QXmlStreamReader>

#include "dbus/autotileadaptor/autotileadaptor.h"
#include "dbus/compositorbridgeadaptor.h"
#include "dbus/controladaptor.h"
#include "dbus/layoutadaptor/layoutadaptor.h"
#include "dbus/overlayadaptor.h"
#include "dbus/settingsadaptor/settingsadaptor.h"
#include "dbus/shaderadaptor.h"
#include "dbus/snapadaptor/snapadaptor.h"
#include "dbus/windowdragadaptor/windowdragadaptor.h"
#include "dbus/ruleadaptor.h"
#include "dbus/windowtrackingadaptor/windowtrackingadaptor.h"
#include "dbus/zonedetectionadaptor.h"

using namespace PlasmaZones;

namespace {

struct XmlArg
{
    QString name;
    QString dbusType;
    QString direction; // "in" / "out" (signals: treated as out)
    QString qtTypeName; // org.qtproject.QtDBus.QtTypeName.* annotation value, if any
    QString qtTypeNameLabel; // the annotation's index suffix ("In0", "Out1", ...)
};

struct XmlMethod
{
    QString name;
    QList<XmlArg> args;
};

struct XmlProperty
{
    QString name;
    QString dbusType;
    QString access; // "read" / "readwrite"
};

struct XmlInterface
{
    QString name;
    QHash<QString, XmlMethod> methods;
    QHash<QString, XmlMethod> signalEntries;
    QHash<QString, XmlProperty> properties;
    /// Names that appeared more than once — D-Bus allows overloads but this
    /// harness's name-keyed model doesn't; an overload must fail loudly
    /// instead of being half-checked.
    QStringList duplicateNames;
};

/// Map a (normalized) C++ parameter type to its D-Bus signature. Custom
/// marshalled types are matched through the XML's QtTypeName annotation
/// instead — see compareArg().
QString dbusTypeFor(const QString& cppTypeIn)
{
    QString cppType = cppTypeIn;
    cppType.remove(QLatin1Char('&'));
    if (cppType.startsWith(QLatin1String("const "))) {
        cppType = cppType.mid(6);
    }
    static const QHash<QString, QString> kMap = {
        {QStringLiteral("QString"), QStringLiteral("s")},
        {QStringLiteral("int"), QStringLiteral("i")},
        {QStringLiteral("bool"), QStringLiteral("b")},
        {QStringLiteral("uint"), QStringLiteral("u")},
        {QStringLiteral("double"), QStringLiteral("d")},
        {QStringLiteral("qlonglong"), QStringLiteral("x")},
        {QStringLiteral("qulonglong"), QStringLiteral("t")},
        {QStringLiteral("QStringList"), QStringLiteral("as")},
        {QStringLiteral("QVariantMap"), QStringLiteral("a{sv}")},
        {QStringLiteral("QVariantList"), QStringLiteral("av")},
        {QStringLiteral("QByteArray"), QStringLiteral("ay")},
        {QStringLiteral("QDBusUnixFileDescriptor"), QStringLiteral("h")},
        {QStringLiteral("QDBusVariant"), QStringLiteral("v")},
    };
    return kMap.value(cppType);
}

XmlInterface parseInterfaceXml(const QString& path)
{
    XmlInterface iface;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return iface;
    }
    QXmlStreamReader xml(&f);
    XmlMethod current;
    bool inMethod = false;
    bool inSignal = false;
    bool inArg = false;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            const auto el = xml.name();
            const auto attrs = xml.attributes();
            if (el == QLatin1String("interface")) {
                iface.name = attrs.value(QLatin1String("name")).toString();
            } else if (el == QLatin1String("method") || el == QLatin1String("signal")) {
                current = XmlMethod{attrs.value(QLatin1String("name")).toString(), {}};
                inMethod = (el == QLatin1String("method"));
                inSignal = !inMethod;
                inArg = false;
            } else if (el == QLatin1String("arg") && (inMethod || inSignal)) {
                XmlArg arg;
                arg.name = attrs.value(QLatin1String("name")).toString();
                arg.dbusType = attrs.value(QLatin1String("type")).toString();
                arg.direction = attrs.value(QLatin1String("direction")).toString();
                if (arg.direction.isEmpty()) {
                    // Method args default to "in" per the D-Bus spec; signal
                    // args are always out.
                    arg.direction = inSignal ? QStringLiteral("out") : QStringLiteral("in");
                }
                current.args.append(arg);
                inArg = true;
            } else if (el == QLatin1String("annotation") && inArg) {
                // Attribute QtTypeName annotations ONLY while nested inside
                // an <arg> element — a method-level annotation placed after
                // the args (Qt's other canonical placement) must not be
                // silently attached to the last arg.
                const QString annName = attrs.value(QLatin1String("name")).toString();
                const QLatin1String prefix("org.qtproject.QtDBus.QtTypeName.");
                if (annName.startsWith(prefix)) {
                    current.args.last().qtTypeName = attrs.value(QLatin1String("value")).toString();
                    current.args.last().qtTypeNameLabel = annName.mid(prefix.size());
                }
            } else if (el == QLatin1String("property")) {
                XmlProperty p;
                p.name = attrs.value(QLatin1String("name")).toString();
                p.dbusType = attrs.value(QLatin1String("type")).toString();
                p.access = attrs.value(QLatin1String("access")).toString();
                if (iface.properties.contains(p.name)) {
                    iface.duplicateNames.append(p.name);
                }
                iface.properties.insert(p.name, p);
            }
        } else if (xml.isEndElement()) {
            const auto el = xml.name();
            if (el == QLatin1String("method") && inMethod) {
                if (iface.methods.contains(current.name)) {
                    iface.duplicateNames.append(current.name);
                }
                iface.methods.insert(current.name, current);
                inMethod = false;
            } else if (el == QLatin1String("signal") && inSignal) {
                if (iface.signalEntries.contains(current.name)) {
                    iface.duplicateNames.append(current.name);
                }
                iface.signalEntries.insert(current.name, current);
                inSignal = false;
            } else if (el == QLatin1String("arg")) {
                inArg = false;
            }
        }
    }
    return iface;
}

/// Bus-exposed methods of the adaptor subclass itself: public slots and
/// public Q_INVOKABLEs from methodOffset() up, with default-argument clones
/// collapsed to the full-signature entry. Genuine OVERLOADS (same name,
/// non-prefix parameter lists) are reported through @p overloads — this
/// harness's name-keyed model would only half-check them, so they must fail
/// loudly instead.
QHash<QString, QMetaMethod> busMethods(const QMetaObject& mo, QStringList* overloads = nullptr)
{
    QHash<QString, QMetaMethod> out;
    for (int i = mo.methodOffset(); i < mo.methodCount(); ++i) {
        const QMetaMethod m = mo.method(i);
        if (m.access() != QMetaMethod::Public) {
            continue;
        }
        if (m.methodType() != QMetaMethod::Slot && m.methodType() != QMetaMethod::Method) {
            continue;
        }
        const QString name = QString::fromLatin1(m.name());
        auto it = out.find(name);
        if (it == out.end()) {
            out.insert(name, m);
            continue;
        }
        // Same-name entry: legal only as a default-arg CLONE, whose
        // parameter types are a strict prefix of the full signature's.
        const QMetaMethod& shorter = (it->parameterCount() < m.parameterCount()) ? it.value() : m;
        const QMetaMethod& longer = (it->parameterCount() < m.parameterCount()) ? m : it.value();
        bool isClone = shorter.parameterCount() < longer.parameterCount();
        for (int p = 0; isClone && p < shorter.parameterCount(); ++p) {
            isClone = (shorter.parameterTypes().at(p) == longer.parameterTypes().at(p));
        }
        if (!isClone && overloads) {
            overloads->append(name);
        }
        out.insert(name, longer);
    }
    return out;
}

QHash<QString, QMetaMethod> busSignals(const QMetaObject& mo)
{
    QHash<QString, QMetaMethod> out;
    for (int i = mo.methodOffset(); i < mo.methodCount(); ++i) {
        const QMetaMethod m = mo.method(i);
        if (m.methodType() == QMetaMethod::Signal) {
            out.insert(QString::fromLatin1(m.name()), m);
        }
    }
    return out;
}

QString normalizeCppType(QByteArray t)
{
    QString s = QString::fromLatin1(t);
    s.remove(QLatin1Char('&'));
    if (s.startsWith(QLatin1String("const "))) {
        s = s.mid(6);
    }
    return s;
}

/// Compare one XML arg against a C++ type. Custom marshalled types must
/// carry a QtTypeName annotation naming the exact C++ type; basic types map
/// through dbusTypeFor().
void compareArg(const QString& iface, const QString& method, const XmlArg& xmlArg, const QByteArray& cppType)
{
    const QString cpp = normalizeCppType(cppType);
    if (!xmlArg.qtTypeName.isEmpty()) {
        QVERIFY2(xmlArg.qtTypeName == cpp,
                 qPrintable(QStringLiteral("%1.%2 arg '%3': QtTypeName '%4' != C++ type '%5'")
                                .arg(iface, method, xmlArg.name, xmlArg.qtTypeName, cpp)));
        // The XML's D-Bus signature string must match the registered
        // marshaller's actual wire signature — the annotation VALUE alone
        // can be right while the `type` attribute drifts (the a{ss}→a{sv}
        // class of bug). Requires the metatype to be registered with
        // QtDBus (initTestCase calls PhosphorProtocol::registerWireTypes).
        const QMetaType mt = QMetaType::fromName(cpp.toLatin1());
        QVERIFY2(mt.isValid(),
                 qPrintable(QStringLiteral("%1.%2 arg '%3': annotated type '%4' is not a registered "
                                           "metatype — cannot verify its wire signature")
                                .arg(iface, method, xmlArg.name, cpp)));
        const char* sig = QDBusMetaType::typeToSignature(mt);
        QVERIFY2(sig,
                 qPrintable(QStringLiteral("%1.%2 arg '%3': annotated type '%4' has no QtDBus marshaller "
                                           "registered (qDBusRegisterMetaType missing)")
                                .arg(iface, method, xmlArg.name, cpp)));
        QVERIFY2(xmlArg.dbusType == QString::fromLatin1(sig),
                 qPrintable(QStringLiteral("%1.%2 arg '%3': XML signature '%4' != registered wire signature '%5' "
                                           "for annotated type '%6'")
                                .arg(iface, method, xmlArg.name, xmlArg.dbusType, QString::fromLatin1(sig), cpp)));
        return;
    }
    const QString mapped = dbusTypeFor(cpp);
    QVERIFY2(!mapped.isEmpty(),
             qPrintable(QStringLiteral("%1.%2 arg '%3': C++ type '%4' has no basic D-Bus mapping "
                                       "and the XML carries no QtTypeName annotation")
                            .arg(iface, method, xmlArg.name, cpp)));
    QVERIFY2(mapped == xmlArg.dbusType,
             qPrintable(QStringLiteral("%1.%2 arg '%3': XML type '%4' != mapped '%5' "
                                       "(C++ '%6')")
                            .arg(iface, method, xmlArg.name, xmlArg.dbusType, mapped, cpp)));
}

} // namespace

class TestDBusContractSync : public QObject
{
    Q_OBJECT

    QString xmlPath(const QString& interfaceName) const
    {
        return QStringLiteral(PLASMAZONES_DBUS_XML_DIR "/") + interfaceName + QStringLiteral(".xml");
    }

    void verifyContract(const QMetaObject& mo, const QString& interfaceName,
                        const QSet<QString>& offContractSignals = {}, const QSet<QString>& offContractMethods = {})
    {
        const XmlInterface iface = parseInterfaceXml(xmlPath(interfaceName));
        QVERIFY2(!iface.name.isEmpty(), qPrintable(QStringLiteral("could not parse %1").arg(xmlPath(interfaceName))));
        QCOMPARE(iface.name, interfaceName);

        // The adaptor's Q_CLASSINFO must name the same interface the XML does.
        const int ci = mo.indexOfClassInfo("D-Bus Interface");
        QVERIFY(ci >= 0);
        QCOMPARE(QString::fromLatin1(mo.classInfo(ci).value()), interfaceName);

        // The name-keyed model can't represent overloads — fail loudly on
        // any duplicate instead of half-checking it.
        QVERIFY2(iface.duplicateNames.isEmpty(),
                 qPrintable(QStringLiteral("%1: duplicate XML method/signal/property names (overloads are not "
                                           "supported by this harness): %2")
                                .arg(interfaceName, iface.duplicateNames.join(QLatin1String(", ")))));
        QStringList overloads;
        const QHash<QString, QMetaMethod> methods = busMethods(mo, &overloads);
        QVERIFY2(overloads.isEmpty(),
                 qPrintable(QStringLiteral("%1: adaptor declares genuine overloads (not default-arg clones) — "
                                           "unsupported by this harness: %2")
                                .arg(interfaceName, overloads.join(QLatin1String(", ")))));
        const QHash<QString, QMetaMethod> signals_ = busSignals(mo);

        // A method and a signal sharing one name on the same interface breaks
        // QtDBus dispatch — the per-map duplicate checks above can't see the
        // cross-map collision, so check it explicitly.
        for (auto it = iface.methods.constBegin(); it != iface.methods.constEnd(); ++it) {
            QVERIFY2(!iface.signalEntries.contains(it.key()),
                     qPrintable(QStringLiteral("%1: '%2' is declared as BOTH a method and a signal in the XML")
                                    .arg(interfaceName, it.key())));
        }
        for (auto it = methods.constBegin(); it != methods.constEnd(); ++it) {
            QVERIFY2(!signals_.contains(it.key()),
                     qPrintable(QStringLiteral("%1: adaptor declares '%2' as BOTH a bus method and a signal")
                                    .arg(interfaceName, it.key())));
        }

        // 1 + 2. Method bijection with full argument verification.
        for (auto it = iface.methods.constBegin(); it != iface.methods.constEnd(); ++it) {
            QVERIFY2(methods.contains(it.key()),
                     qPrintable(QStringLiteral("%1: XML method '%2' has no bus-exposed adaptor method")
                                    .arg(interfaceName, it.key())));
            const QMetaMethod m = methods.value(it.key());

            QList<XmlArg> inArgs;
            QList<XmlArg> outArgs;
            for (const XmlArg& a : it->args) {
                (a.direction == QLatin1String("out") ? outArgs : inArgs).append(a);
            }

            // Split C++ parameters into ins (by-value / const-ref) and outs
            // (non-const refs), preserving order. A `const QDBusMessage&`
            // parameter is QtDBus's delayed-reply context injection — it is
            // NOT a wire argument and never appears in the XML.
            QList<QByteArray> cppIns;
            QList<QByteArray> cppOuts;
            const QList<QByteArray> paramTypes = m.parameterTypes();
            for (const QByteArray& t : paramTypes) {
                if (t == "QDBusMessage" || t == "const QDBusMessage&") {
                    continue;
                }
                if (t.endsWith('&') && !t.startsWith("const ")) {
                    cppOuts.append(t);
                } else {
                    cppIns.append(t);
                }
            }

            QVERIFY2(inArgs.size() == cppIns.size(),
                     qPrintable(QStringLiteral("%1.%2: %3 XML in-args vs %4 C++ in-params")
                                    .arg(interfaceName, it.key())
                                    .arg(inArgs.size())
                                    .arg(cppIns.size())));
            for (int i = 0; i < inArgs.size(); ++i) {
                compareArg(interfaceName, it.key(), inArgs[i], cppIns[i]);
                // The annotation's index digit is part of the contract:
                // qdbusxml2cpp keys generated typedefs on In<N>/Out<N>, so a
                // wrong digit breaks generated consumers even when the value
                // string is right.
                if (!inArgs[i].qtTypeNameLabel.isEmpty()) {
                    QCOMPARE(inArgs[i].qtTypeNameLabel, QStringLiteral("In%1").arg(i));
                }
            }

            // QtDBus out mapping: non-void return first, then ref params.
            const bool hasReturn = qstrcmp(m.typeName(), "void") != 0;
            const int expectedOuts = cppOuts.size() + (hasReturn ? 1 : 0);
            QVERIFY2(outArgs.size() == expectedOuts,
                     qPrintable(QStringLiteral("%1.%2: %3 XML out-args vs %4 expected (return %5 + %6 ref params)")
                                    .arg(interfaceName, it.key())
                                    .arg(outArgs.size())
                                    .arg(expectedOuts)
                                    .arg(hasReturn ? QStringLiteral("yes") : QStringLiteral("no"))
                                    .arg(cppOuts.size())));
            int outIdx = 0;
            if (hasReturn) {
                compareArg(interfaceName, it.key(), outArgs[outIdx], QByteArray(m.typeName()));
                if (!outArgs[outIdx].qtTypeNameLabel.isEmpty()) {
                    QCOMPARE(outArgs[outIdx].qtTypeNameLabel, QStringLiteral("Out%1").arg(outIdx));
                }
                ++outIdx;
            }
            for (const QByteArray& t : cppOuts) {
                compareArg(interfaceName, it.key(), outArgs[outIdx], t);
                if (!outArgs[outIdx].qtTypeNameLabel.isEmpty()) {
                    QCOMPARE(outArgs[outIdx].qtTypeNameLabel, QStringLiteral("Out%1").arg(outIdx));
                }
                ++outIdx;
            }

            // Arg NAMES are part of the published contract. In-args map to
            // C++ params 1:1; out-args map to the ref-param tail (the
            // return-value out-arg has no C++ param name — QtDBus
            // introspection synthesizes one — so it is skipped).
            const QList<QByteArray> paramNames = m.parameterNames();
            {
                int inXmlIdx = 0;
                int outXmlIdx = hasReturn ? 1 : 0; // return value occupies out slot 0
                for (int nameIdx = 0; nameIdx < paramTypes.size(); ++nameIdx) {
                    const QByteArray& t = paramTypes[nameIdx];
                    if (t == "QDBusMessage" || t == "const QDBusMessage&") {
                        continue; // context injection — no wire arg
                    }
                    const bool isOut = t.endsWith('&') && !t.startsWith("const ");
                    const QString cppName = QString::fromLatin1(paramNames.value(nameIdx));
                    if (isOut) {
                        QVERIFY2(outArgs[outXmlIdx].name == cppName,
                                 qPrintable(QStringLiteral("%1.%2: XML out-arg name '%3' != C++ ref-param name '%4' "
                                                           "(live introspection publishes the C++ name)")
                                                .arg(interfaceName, it.key(), outArgs[outXmlIdx].name, cppName)));
                        ++outXmlIdx;
                        continue;
                    }
                    QVERIFY2(inArgs[inXmlIdx].name == cppName,
                             qPrintable(QStringLiteral("%1.%2: XML in-arg name '%3' != C++ param name '%4'")
                                            .arg(interfaceName, it.key(), inArgs[inXmlIdx].name, cppName)));
                    ++inXmlIdx;
                }
            }
        }
        for (auto it = methods.constBegin(); it != methods.constEnd(); ++it) {
            if (offContractMethods.contains(it.key())) {
                continue;
            }
            QVERIFY2(iface.methods.contains(it.key()),
                     qPrintable(QStringLiteral("%1: bus-exposed adaptor method '%2' is missing from the contract "
                                               "XML — either add it to the XML, move it to a plain public: "
                                               "section (internal helpers must not sit under Q_SLOTS), or "
                                               "document it off-contract and extend this test's allowlist")
                                    .arg(interfaceName, it.key())));
        }

        // 3. Signal bijection (modulo the documented off-contract set).
        for (auto it = iface.signalEntries.constBegin(); it != iface.signalEntries.constEnd(); ++it) {
            QVERIFY2(
                signals_.contains(it.key()),
                qPrintable(QStringLiteral("%1: XML signal '%2' has no adaptor signal").arg(interfaceName, it.key())));
            const QMetaMethod m = signals_.value(it.key());
            QVERIFY2(it->args.size() == m.parameterCount(),
                     qPrintable(QStringLiteral("%1 signal %2: %3 XML args vs %4 C++ params")
                                    .arg(interfaceName, it.key())
                                    .arg(it->args.size())
                                    .arg(m.parameterCount())));
            const QList<QByteArray> sigTypes = m.parameterTypes();
            const QList<QByteArray> sigNames = m.parameterNames();
            for (int i = 0; i < it->args.size(); ++i) {
                compareArg(interfaceName, it.key(), it->args[i], sigTypes[i]);
                if (!it->args[i].qtTypeNameLabel.isEmpty()) {
                    QCOMPARE(it->args[i].qtTypeNameLabel, QStringLiteral("Out%1").arg(i));
                }
                // Signal arg NAMES are part of the introspected contract,
                // same as method in-arg names. An unnamed XML arg is legal
                // D-Bus — only compare when the XML names it.
                if (!it->args[i].name.isEmpty()) {
                    QVERIFY2(it->args[i].name == QString::fromLatin1(sigNames.value(i)),
                             qPrintable(QStringLiteral("%1 signal %2: XML arg name '%3' != C++ param name '%4'")
                                            .arg(interfaceName, it.key(), it->args[i].name,
                                                 QString::fromLatin1(sigNames.value(i)))));
                }
            }
        }
        for (auto it = signals_.constBegin(); it != signals_.constEnd(); ++it) {
            if (offContractSignals.contains(it.key())) {
                continue;
            }
            QVERIFY2(iface.signalEntries.contains(it.key()),
                     qPrintable(QStringLiteral("%1: adaptor signal '%2' is missing from the contract XML (add it, "
                                               "or document it off-contract and extend this test's allowlist)")
                                    .arg(interfaceName, it.key())));
        }

        // 4. Property bijection.
        for (auto it = iface.properties.constBegin(); it != iface.properties.constEnd(); ++it) {
            const int pi = mo.indexOfProperty(it.key().toLatin1().constData());
            QVERIFY2(
                pi >= mo.propertyOffset(),
                qPrintable(
                    QStringLiteral("%1: XML property '%2' has no adaptor Q_PROPERTY").arg(interfaceName, it.key())));
            const QMetaProperty p = mo.property(pi);
            const QString mapped = dbusTypeFor(QString::fromLatin1(p.typeName()));
            QVERIFY2(mapped == it->dbusType,
                     qPrintable(QStringLiteral("%1 property %2: XML type '%3' != mapped '%4'")
                                    .arg(interfaceName, it.key(), it->dbusType, mapped)));
            const QString expectedAccess = p.isWritable() ? QStringLiteral("readwrite") : QStringLiteral("read");
            QCOMPARE(it->access, expectedAccess);
        }
        for (int i = mo.propertyOffset(); i < mo.propertyCount(); ++i) {
            const QString name = QString::fromLatin1(mo.property(i).name());
            QVERIFY2(iface.properties.contains(name),
                     qPrintable(QStringLiteral("%1: adaptor property '%2' is missing from the contract XML")
                                    .arg(interfaceName, name)));
        }
    }

private Q_SLOTS:

    void initTestCase()
    {
        // compareArg verifies XML signature strings against the registered
        // QtDBus marshallers for QtTypeName-annotated args — the wire types
        // must be registered first, exactly as the daemon does at startup.
        PhosphorProtocol::registerWireTypes();
    }

    void testCompositorBridgeContract()
    {
        verifyContract(CompositorBridgeAdaptor::staticMetaObject, QStringLiteral("org.plasmazones.CompositorBridge"));
    }

    void testWindowTrackingContract()
    {
        // windowClosedNotification is a documented in-process Qt signal —
        // bus-visible but deliberately off the published contract (see its
        // doc comment in windowtrackingadaptor.h).
        verifyContract(WindowTrackingAdaptor::staticMetaObject, QStringLiteral("org.plasmazones.WindowTracking"),
                       {QStringLiteral("windowClosedNotification")});
    }

    void testAutotileContract()
    {
        verifyContract(AutotileAdaptor::staticMetaObject, QStringLiteral("org.plasmazones.Autotile"));
    }

    void testSnapContract()
    {
        verifyContract(SnapAdaptor::staticMetaObject, QStringLiteral("org.plasmazones.Snap"));
    }

    void testControlContract()
    {
        verifyContract(ControlAdaptor::staticMetaObject, QStringLiteral("org.plasmazones.Control"));
    }

    void testLayoutRegistryContract()
    {
        verifyContract(LayoutAdaptor::staticMetaObject, QStringLiteral("org.plasmazones.LayoutRegistry"));
    }

    void testOverlayContract()
    {
        verifyContract(OverlayAdaptor::staticMetaObject, QStringLiteral("org.plasmazones.Overlay"));
    }

    void testSettingsContract()
    {
        verifyContract(SettingsAdaptor::staticMetaObject, QStringLiteral("org.plasmazones.Settings"));
    }

    void testShaderContract()
    {
        verifyContract(ShaderAdaptor::staticMetaObject, QStringLiteral("org.plasmazones.Shader"));
    }

    void testWindowDragContract()
    {
        // clearForCompositorReconnect is a documented off-contract bus
        // method: the effect invokes it cross-process at shutdown, but it is
        // deliberately absent from the published XML (see its doc comment in
        // windowdragadaptor.h — it must STAY a slot).
        verifyContract(WindowDragAdaptor::staticMetaObject, QStringLiteral("org.plasmazones.WindowDrag"), {},
                       {QStringLiteral("clearForCompositorReconnect")});
    }

    void testRulesContract()
    {
        verifyContract(RuleAdaptor::staticMetaObject, QStringLiteral("org.plasmazones.Rules"));
    }

    void testZoneDetectionContract()
    {
        verifyContract(ZoneDetectionAdaptor::staticMetaObject, QStringLiteral("org.plasmazones.ZoneDetection"));
    }

    void testScreenContract()
    {
        verifyContract(PhosphorScreens::DBusScreenAdaptor::staticMetaObject, QStringLiteral("org.plasmazones.Screen"));
    }

    void testAllXmlFilesCovered()
    {
        // Completeness tripwire: every XML under dbus/ must be either tested
        // above or on the documented out-of-scope list (the app-internal
        // launch adaptors — see the header comment). A 16th XML added later
        // must not go silently unchecked.
        static const QSet<QString> covered = {
            QStringLiteral("org.plasmazones.Autotile.xml"),      QStringLiteral("org.plasmazones.CompositorBridge.xml"),
            QStringLiteral("org.plasmazones.Control.xml"),       QStringLiteral("org.plasmazones.LayoutRegistry.xml"),
            QStringLiteral("org.plasmazones.Overlay.xml"),       QStringLiteral("org.plasmazones.Screen.xml"),
            QStringLiteral("org.plasmazones.Settings.xml"),      QStringLiteral("org.plasmazones.Shader.xml"),
            QStringLiteral("org.plasmazones.Snap.xml"),          QStringLiteral("org.plasmazones.WindowDrag.xml"),
            QStringLiteral("org.plasmazones.Rules.xml"),         QStringLiteral("org.plasmazones.WindowTracking.xml"),
            QStringLiteral("org.plasmazones.ZoneDetection.xml"),
        };
        static const QSet<QString> documentedOutOfScope = {
            // App-internal single-slot launch forwarders; rationale in the
            // file header's "NOT covered" paragraph.
            QStringLiteral("org.plasmazones.SettingsApp.xml"),
            QStringLiteral("org.plasmazones.EditorApp.xml"),
        };
        const QDir xmlDir(QStringLiteral(PLASMAZONES_DBUS_XML_DIR));
        const QStringList onDisk = xmlDir.entryList({QStringLiteral("*.xml")}, QDir::Files);
        for (const QString& f : onDisk) {
            QVERIFY2(covered.contains(f) || documentedOutOfScope.contains(f),
                     qPrintable(QStringLiteral("dbus/%1 is neither contract-tested nor on the documented "
                                               "out-of-scope list — add a test slot or document it")
                                    .arg(f)));
        }
        // The reverse direction: a covered entry whose file vanished means a
        // rename slipped past the suite.
        for (const QString& f : covered + documentedOutOfScope) {
            QVERIFY2(xmlDir.exists(f),
                     qPrintable(QStringLiteral("dbus/%1 is on the coverage list but missing on disk").arg(f)));
        }
    }
};

QTEST_GUILESS_MAIN(TestDBusContractSync)
#include "test_dbus_contract_sync.moc"
