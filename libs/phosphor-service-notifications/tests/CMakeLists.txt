# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

find_package(Qt6 6.6 REQUIRED COMPONENTS Test)

function(_phosphorservicenotifications_test target source)
    add_executable(${target} ${source})
    target_link_libraries(${target}
        PRIVATE
            Qt6::Test
            PhosphorServiceNotifications::PhosphorServiceNotifications
    )
    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(${target}
        PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
            LABELS "phosphorservicenotifications"
    )
endfunction()

# Smoke test: exercises the milestone-1 contract (QML registration idempotency,
# the static service/object identifiers, GetServerInformation / GetCapabilities,
# id allocation incl. replaces_id reuse, and the CloseNotification close-reason
# signal) against a server built on a private peer bus, so it never touches the
# real org.freedesktop.Notifications name on the developer's session.
_phosphorservicenotifications_test(test_phosphorservicenotifications_smoke test_smoke.cpp)

# QML facade test: a real QQmlEngine imports the module and proves the
# imperative registration yields a usable facade (NotificationModel instantiates
# and binds, the Notification.Urgency enum resolves, model.count is reactive).
_phosphorservicenotifications_test(test_phosphorservicenotifications_qmlfacade test_qmlfacade.cpp)

# Wire test: drives Notify / CloseNotification / GetServerInformation across a
# peer-to-peer D-Bus connection so arguments go through real marshalling. This
# is the only place the image-data (iiibiiay) hint decode can be exercised (an
# in-process QDBusArgument is in marshalling mode and cannot be read back).
_phosphorservicenotifications_test(test_phosphorservicenotifications_wire test_wire.cpp)
