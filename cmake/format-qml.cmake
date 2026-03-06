# SPDX-FileCopyrightText: 2026 PlasmaZones contributors
# SPDX-License-Identifier: GPL-3.0
#
# Run qmlformat -i on each file. Ignores per-file failures so one bad
# QML does not fail the format-qml target. Invoked as:
#   cmake -DQMLFORMAT=/path/to/qmlformat -DFILES="path1;path2;..." -P format-qml.cmake

if(NOT QMLFORMAT OR NOT FILES)
    return()
endif()

foreach(f IN LISTS FILES)
    if(EXISTS "${f}")
        execute_process(
            COMMAND "${QMLFORMAT}" -i "${f}"
            RESULT_VARIABLE r
            ERROR_QUIET
            OUTPUT_QUIET
        )
    endif()
endforeach()
