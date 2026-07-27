# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

find_package(Qt6 6.6 REQUIRED COMPONENTS Test)

# Shared test isolation: a private D-Bus session (so a test that touches D-Bus
# cannot activate the INSTALLED daemon, which then holds the test's stdout pipe
# open and hangs ctest after the test itself passed) plus a per-target XDG
# sandbox (so nothing writes into the developer's real ~/.config/plasmazones).
include(${CMAKE_SOURCE_DIR}/cmake/PhosphorTestIsolation.cmake)
function(_phosphorservicenotifications_test target source)
    add_executable(${target} ${source})
    target_link_libraries(${target}
        PRIVATE
            Qt6::Test
            PhosphorServiceNotifications::PhosphorServiceNotifications
    )
    add_test(NAME ${target} COMMAND ${target})
    phosphor_apply_test_isolation(${target})
    # APPEND, never a bare SET: `set_tests_properties(... PROPERTIES
    # ENVIRONMENT ...)` REPLACES the whole list and would drop the XDG
    # sandbox `phosphor_apply_test_isolation` just installed.
    phosphor_append_test_environment(${target} "QT_QPA_PLATFORM=offscreen")
    set_tests_properties(${target} PROPERTIES
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
