# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# CPack: source tarball generation (`cpack --config CPackSourceConfig.cmake`
# or `make package_source`). Distro builds in release.yml still hand-roll
# tarballs for filename-control reasons; this exists so a release engineer
# can also bootstrap a tarball locally without the workflow.
#
# Extracted from the top-level CMakeLists.txt to keep that file under
# the 800-line cap. Include after feature_summary() at the very end of
# the root configure.
set(CPACK_PACKAGE_NAME "${PROJECT_NAME}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VENDOR "fuddlesworth")
set(CPACK_PACKAGE_CONTACT "fuddlesworth <fuddlesworth@users.noreply.github.com>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Window tiling and autotiling for KDE Plasma")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_SOURCE_GENERATOR "TXZ;TGZ")
set(CPACK_SOURCE_PACKAGE_FILE_NAME "${PROJECT_NAME}-${PROJECT_VERSION}")
set(CPACK_SOURCE_IGNORE_FILES
    "/\\\\.git/"
    "/\\\\.github/"
    "/\\\\.claude/"
    "/\\\\.zed/"
    "/\\\\.ccache/"
    "/build/"
    "/build-.*/"
    "/install/"
    "/dist/"
    "/node_modules/"
    "/\\\\.cache/"
    "/\\\\.idea/"
    "/\\\\.vscode/"
    "/\\\\.DS_Store"
    "/CMakeUserPresets\\\\.json$"
    "/CMakeLists\\\\.txt\\\\.user"
    "/compile_commands\\\\.json$"
    "\\\\.qm$"
    "\\\\.swp$"
    "~$"
)
include(CPack)
