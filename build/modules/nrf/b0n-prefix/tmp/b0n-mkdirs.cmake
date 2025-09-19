# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "/Users/akw/Documents/GitHub/healthypi-move-workspace/nrf/samples/nrf5340/netboot")
  file(MAKE_DIRECTORY "/Users/akw/Documents/GitHub/healthypi-move-workspace/nrf/samples/nrf5340/netboot")
endif()
file(MAKE_DIRECTORY
  "/Users/akw/Documents/GitHub/healthypi-move-workspace/healthypi-move-fw/build/b0n"
  "/Users/akw/Documents/GitHub/healthypi-move-workspace/healthypi-move-fw/build/modules/nrf/b0n-prefix"
  "/Users/akw/Documents/GitHub/healthypi-move-workspace/healthypi-move-fw/build/modules/nrf/b0n-prefix/tmp"
  "/Users/akw/Documents/GitHub/healthypi-move-workspace/healthypi-move-fw/build/modules/nrf/b0n-prefix/src/b0n-stamp"
  "/Users/akw/Documents/GitHub/healthypi-move-workspace/healthypi-move-fw/build/modules/nrf/b0n-prefix/src"
  "/Users/akw/Documents/GitHub/healthypi-move-workspace/healthypi-move-fw/build/modules/nrf/b0n-prefix/src/b0n-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/Users/akw/Documents/GitHub/healthypi-move-workspace/healthypi-move-fw/build/modules/nrf/b0n-prefix/src/b0n-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/Users/akw/Documents/GitHub/healthypi-move-workspace/healthypi-move-fw/build/modules/nrf/b0n-prefix/src/b0n-stamp${cfgdir}") # cfgdir has leading slash
endif()
