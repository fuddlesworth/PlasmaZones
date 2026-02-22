# extract-pot.cmake — extract translatable strings into a .pot file
#
# Called per-domain by the extract-pot target in CMakeLists.txt.
# Inputs (passed via -D):
#   CMAKE_SOURCE_DIR_FILE  file containing the source tree root path
#   BINARY_DIR             build directory (for temp files)
#   CMAKE_CMD              path to cmake executable
#   DOMAIN                 translation domain (e.g. plasmazonesd)
#   CPP_FILES_FILE         file listing C++ sources (one per line)
#   QML_FILES_FILE         file listing QML sources (one per line)
#   POT_FILE               output .pot path
#   GETTEXT_XGETTEXT_EXECUTABLE  path to xgettext
#   GETTEXT_MSGCAT_EXECUTABLE    path to msgcat

# Read source dir
file(READ "${CMAKE_SOURCE_DIR_FILE}" _src_dir)
string(STRIP "${_src_dir}" _src_dir)

# Read file lists (one path per line, skip blanks) and make relative to source dir
file(STRINGS "${CPP_FILES_FILE}" _cpp_abs)
file(STRINGS "${QML_FILES_FILE}" _qml_abs)

set(_cpp_files "")
foreach(_f IN LISTS _cpp_abs)
    file(RELATIVE_PATH _rel "${_src_dir}" "${_f}")
    list(APPEND _cpp_files "${_rel}")
endforeach()

set(_qml_files "")
foreach(_f IN LISTS _qml_abs)
    file(RELATIVE_PATH _rel "${_src_dir}" "${_f}")
    list(APPEND _qml_files "${_rel}")
endforeach()

set(_tmp_pots "")

# --- Extract from C++ sources ---
list(LENGTH _cpp_files _cpp_count)
if(_cpp_count GREATER 0)
    set(_cpp_pot "${BINARY_DIR}/po-${DOMAIN}-cpp.pot")
    execute_process(
        COMMAND ${GETTEXT_XGETTEXT_EXECUTABLE}
            --from-code=UTF-8
            --c++ --kde
            --keyword=i18n --keyword=i18nc:1c,2 --keyword=i18np:1,2
            --keyword=i18ncp:1c,2,3
            --keyword=ki18n --keyword=ki18nc:1c,2
            --keyword=ki18np:1,2 --keyword=ki18ncp:1c,2,3
            --package-name=${DOMAIN}
            --msgid-bugs-address=https://github.com/fuddlesworth/PlasmaZones/issues
            -o "${_cpp_pot}"
            ${_cpp_files}
        WORKING_DIRECTORY "${_src_dir}"
        RESULT_VARIABLE _rc
    )
    if(_rc EQUAL 0 AND EXISTS "${_cpp_pot}")
        list(APPEND _tmp_pots "${_cpp_pot}")
    endif()
endif()

# --- Extract from QML sources ---
list(LENGTH _qml_files _qml_count)
if(_qml_count GREATER 0)
    set(_qml_pot "${BINARY_DIR}/po-${DOMAIN}-qml.pot")
    execute_process(
        COMMAND ${GETTEXT_XGETTEXT_EXECUTABLE}
            --from-code=UTF-8
            --language=JavaScript
            --keyword=i18n --keyword=i18nc:1c,2 --keyword=i18np:1,2
            --keyword=i18ncp:1c,2,3
            --keyword=qsTr --keyword=qsTrId --keyword=qsTranslate:2
            --package-name=${DOMAIN}
            --msgid-bugs-address=https://github.com/fuddlesworth/PlasmaZones/issues
            -o "${_qml_pot}"
            ${_qml_files}
        WORKING_DIRECTORY "${_src_dir}"
        RESULT_VARIABLE _rc
    )
    if(_rc EQUAL 0 AND EXISTS "${_qml_pot}")
        list(APPEND _tmp_pots "${_qml_pot}")
    endif()
endif()

# --- Merge partial pots into final .pot ---
list(LENGTH _tmp_pots _pot_count)
if(_pot_count EQUAL 0)
    message(STATUS "[${DOMAIN}] No translatable strings found")
    return()
endif()

if(_pot_count EQUAL 1)
    # Single source — just copy
    file(COPY_FILE ${_tmp_pots} "${POT_FILE}")
else()
    # Multiple sources — merge with msgcat
    execute_process(
        COMMAND ${GETTEXT_MSGCAT_EXECUTABLE}
            --use-first
            -o "${POT_FILE}"
            ${_tmp_pots}
        RESULT_VARIABLE _rc
    )
    if(NOT _rc EQUAL 0)
        message(WARNING "[${DOMAIN}] msgcat failed (rc=${_rc})")
    endif()
endif()

# Count strings for feedback
file(STRINGS "${POT_FILE}" _msgids REGEX "^msgid ")
list(LENGTH _msgids _str_count)
message(STATUS "[${DOMAIN}] Extracted ${_str_count} strings → ${POT_FILE}")
