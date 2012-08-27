# @@@LICENSE
#
#      Copyright (c) 2011-2012 Hewlett-Packard Development Company, L.P.
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

cmake_minimum_required(VERSION 2.6)

install(FILES node_fork_server.upstart DESTINATION /etc/event.d RENAME node_fork_server)

install(FILES fork_server.js DESTINATION /usr/palm/nodejs)

install(PROGRAMS jslauncher run-js-service DESTINATION /usr/bin)

install(FILES bootstrap-node.js palm_bus_config.json sources.json DESTINATION /usr/palm/services/jsservicelauncher)

# Create empty directory
install(DIRECTORY DESTINATION /usr/palm/frameworks/private/javascripts)

add_executable(node_spawner node_spawner.cc)

find_library(BOOST_PO boost_program_options)
find_path(BOOST_INCLUDE_DIRS "boost/program_options.hpp")
include_directories(${BOOST_INCLUDE_DIRS})

find_library(PBNJSON_CPP_LIBRARIES pbnjson_cpp)
find_path(PBNJSON_CPP_INCLUDE_DIRS "pbnjson.hpp")
include_directories(${PBNJSON_CPP_INCLUDE_DIRS})

target_link_libraries(node_spawner ${BOOST_PO} ${PBNJSON_CPP_LIBRARIES} ${RT})

install(TARGETS node_spawner
        DESTINATION bin
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE)
