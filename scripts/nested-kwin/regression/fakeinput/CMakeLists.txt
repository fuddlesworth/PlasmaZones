# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.16)
project(pz_fakeinput LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Gui)
find_package(KWayland REQUIRED)
add_executable(fakeinput main.cpp)
target_link_libraries(fakeinput PRIVATE Qt6::Gui Plasma::KWaylandClient)
