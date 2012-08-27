MojoServiceLauncher
Summary

OpenwebOS component responsible for launching the services

Dependencies

Below are the tools and libraries required to build.

- boost
- pbnjson 


How to Build on Linux
Building

Once you have downloaded the source, execute the following to build it (after changing into the directory under which it was downloaded):

$ mkdir BUILD
$ cd BUILD
$ cmake ..
$ make
$ sudo make install

The directory under which the files are installed defaults to /usr/local/bin. You can install them elsewhere by supplying a value for WEBOS_INSTALL_ROOT when invoking cmake. For example:

$ cmake -D WEBOS_INSTALL_ROOT:PATH=$HOME/projects/openwebos ..
$ make
$ make install

will install the files in subdirectories of $HOME/projects/openwebos.

Specifying WEBOS_INSTALL_ROOT also causes pkg-config to look in that tree first before searching the standard locations. You can specify additional directories to be searched prior to this one by setting the the PKG_CONFIG_PATH environment variable.
To see all of the make targets that CMake has generated, issue:
$ make help

Copyright and License Information

All content, including all source code files and documentation files in this repository except otherwise noted are:

Copyright (c) 2010-2012 Hewlett-Packard Development Company, L.P.

All content, including all source code files and documentation files in this repository except otherwise noted are: Licensed under the Apache License, Version 2.0 (the "License"); you may not use this content except in compliance with the License. You may obtain a copy of the License at

http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the specific language governing permissions and limitations under the License.

