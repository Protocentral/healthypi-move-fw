# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "/Users/akw/Documents/GitHub/healthypi-move-workspace/healthypi-move-fw/app")
  file(MAKE_DIRECTORY "/Users/akw/Documents/GitHub/healthypi-move-workspace/healthypi-move-fw/app")
endif()
file(MAKE_DIRECTORY
  "/Users/akw/Documents/GitHub/healthypi-move-workspace/healthypi-move-fw/build/app"
  "/Users/akw/Documents/GitHub/healthypi-move-workspace/healthypi-move-fw/build/_sysbuild/sysbuild/images/app-prefix"
  "/Users/akw/Documents/GitHub/healthypi-move-workspace/healthypi-move-fw/build/_sysbuild/sysbuild/images/app-prefix/tmp"
  "/Users/akw/Documents/GitHub/healthypi-move-workspace/healthypi-move-fw/build/_sysbuild/sysbuild/images/app-prefix/src/app-stamp"
  "/Users/akw/Documents/GitHub/healthypi-move-workspace/healthypi-move-fw/build/_sysbuild/sysbuild/images/app-prefix/src"
  "/Users/akw/Documents/GitHub/healthypi-move-workspace/healthypi-move-fw/build/_sysbuild/sysbuild/images/app-prefix/src/app-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/Users/akw/Documents/GitHub/healthypi-move-workspace/healthypi-move-fw/build/_sysbuild/sysbuild/images/app-prefix/src/app-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/Users/akw/Documents/GitHub/healthypi-move-workspace/healthypi-move-fw/build/_sysbuild/sysbuild/images/app-prefix/src/app-stamp${cfgdir}") # cfgdir has leading slash
endif()
