# @@@LICENSE
#
#      Copyright (c) 2009-2013 LG Electronics, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# LICENSE@@@

cmake_minimum_required(VERSION 2.8.7)

project(mojoservicelauncher CXX)

include(webOS/webOS)
webos_modules_init(1 0 0 QUALIFIER RC4)
webos_component(2 0 1)

include(FindPkgConfig)

pkg_check_modules(PBNJSON_CPP REQUIRED pbnjson_cpp)
include_directories(${PBNJSON_CPP_INCLUDE_DIRS})
webos_add_compiler_flags(ALL ${PBNJSON_CPP_CFLAGS_OTHER})

find_package(Boost REQUIRED COMPONENTS program_options)
include_directories(Boost_INCLUDE_DIRS)

webos_add_compiler_flags(ALL -Wall)

install(FILES files/launch/node_fork_server DESTINATION ${WEBOS_INSTALL_SYSCONFDIR}/event.d)
install(FILES files/launch/node_fork_server.conf DESTINATION ${WEBOS_INSTALL_SYSCONFDIR}/init)
install(FILES fork_server.js DESTINATION ${WEBOS_INSTALL_WEBOS_PREFIX}/nodejs)
install(PROGRAMS jslauncher run-js-service DESTINATION ${WEBOS_INSTALL_BINDIR})
install(FILES bootstrap-node.js palm_bus_config.json sources.json DESTINATION ${WEBOS_INSTALL_WEBOS_SERVICESDIR}/jsservicelauncher)

# Create empty directory (why?)
install(DIRECTORY DESTINATION ${WEBOS_INSTALL_WEBOS_FRAMEWORKSDIR}/private/javascripts)

add_executable(node_spawner node_spawner.cc)

target_link_libraries(node_spawner ${PBNJSON_CPP_LDFLAGS} ${Boost_LIBRARIES} rt)
webos_build_program(NAME node_spawner)


