# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: LGPL-2.1-or-later

# phosphor_add_facet_module_metadata(<target> METADATA <metadata.json>
#                                    [TS_FILES <file.ts>...] [ALLOW_INVALID])
#
# Generates a Facet module's metadata for the plugin <target>, with the
# phosphor-facet-metadata tool, into ${CMAKE_CURRENT_BINARY_DIR}/<target>_facet/:
#
#   facet_metadata.json  the metadata with `Key[<lang>]` for every string the
#                        catalogs translate. The plugin embeds it with
#                        Q_PLUGIN_METADATA(IID PhosphorFacetModule_iid
#                        FILE "facet_metadata.json"); the directory is on the
#                        target's include path, where moc looks.
#   anchors.json         each page's anchors and anchor version. Attach it to
#                        the module's releases: FacetTestHost compares the next
#                        release against it (setPreviousAnchors).
#   facet_strings.cpp    the translatable strings for lupdate. Not compiled.
#
# Call it after the target's sources are added. The paths are the target's
# FACET_METADATA_FILE, FACET_ANCHORS_FILE and
# FACET_STRINGS_FILE properties. Invalid metadata fails the build, unless
# ALLOW_INVALID is given (for tests that load invalid modules on purpose).
function(phosphor_add_facet_module_metadata target)
    cmake_parse_arguments(ARG "ALLOW_INVALID" "METADATA" "TS_FILES" ${ARGN})
    if(NOT ARG_METADATA)
        message(FATAL_ERROR "phosphor_add_facet_module_metadata(${target}): METADATA is required")
    endif()
    if(TARGET phosphor-facet-metadata)
        set(tool phosphor-facet-metadata)
    else()
        set(tool PhosphorControl::phosphor-facet-metadata)
    endif()
    get_filename_component(source "${ARG_METADATA}" ABSOLUTE)
    set(catalogs "")
    foreach(ts IN LISTS ARG_TS_FILES)
        get_filename_component(ts "${ts}" ABSOLUTE)
        list(APPEND catalogs "${ts}")
    endforeach()
    set(flags "")
    if(ARG_ALLOW_INVALID)
        set(flags --allow-invalid)
    endif()

    set(outDir "${CMAKE_CURRENT_BINARY_DIR}/${target}_facet")
    set(metadataOut "${outDir}/facet_metadata.json")
    set(anchorsOut "${outDir}/anchors.json")
    set(stringsOut "${outDir}/facet_strings.cpp")
    add_custom_command(OUTPUT "${metadataOut}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${outDir}"
        COMMAND ${tool} ${flags} localize "${source}" "${metadataOut}" ${catalogs}
        DEPENDS "${source}" ${catalogs} ${tool}
        COMMENT "Generating Facet module metadata for ${target}"
        VERBATIM)
    add_custom_command(OUTPUT "${anchorsOut}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${outDir}"
        COMMAND ${tool} ${flags} anchors "${source}" "${anchorsOut}"
        DEPENDS "${source}" ${tool}
        VERBATIM)
    add_custom_command(OUTPUT "${stringsOut}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${outDir}"
        COMMAND ${tool} ${flags} strings "${source}" "${stringsOut}"
        DEPENDS "${source}" ${tool}
        VERBATIM)
    add_custom_target(${target}_facet_metadata DEPENDS "${metadataOut}" "${anchorsOut}" "${stringsOut}")
    add_dependencies(${target} ${target}_facet_metadata)
    # moc reads the file while generating the plugin's metadata section, so
    # it must exist before AUTOMOC runs, and a change to it must re-run moc.
    set_property(TARGET ${target} APPEND PROPERTY AUTOGEN_TARGET_DEPENDS ${target}_facet_metadata "${metadataOut}")
    # A plugin class declared in a .cpp includes its moc output, which is not a
    # declared build output, so ninja would only see the regenerated file on
    # the next build. Making the sources depend on the metadata recompiles
    # them in the same build.
    get_target_property(sources ${target} SOURCES)
    foreach(source_file IN LISTS sources)
        if(source_file MATCHES "\\.(cpp|cc|cxx)$")
            set_property(SOURCE "${source_file}" TARGET_DIRECTORY ${target} APPEND PROPERTY OBJECT_DEPENDS "${metadataOut}")
        endif()
    endforeach()
    target_include_directories(${target} PRIVATE "${outDir}")
    set_target_properties(${target} PROPERTIES
        FACET_METADATA_FILE "${metadataOut}"
        FACET_ANCHORS_FILE "${anchorsOut}"
        FACET_STRINGS_FILE "${stringsOut}")
endfunction()
