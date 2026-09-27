# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/home/maikeu/esp/v6.1/esp-idf/components/bootloader/subproject"
  "/media/maikeu/b1e73891-ddde-49a0-9382-903accb68b49/GitClones/KryonOS/build-cyd/bootloader"
  "/media/maikeu/b1e73891-ddde-49a0-9382-903accb68b49/GitClones/KryonOS/build-cyd/bootloader-prefix"
  "/media/maikeu/b1e73891-ddde-49a0-9382-903accb68b49/GitClones/KryonOS/build-cyd/bootloader-prefix/tmp"
  "/media/maikeu/b1e73891-ddde-49a0-9382-903accb68b49/GitClones/KryonOS/build-cyd/bootloader-prefix/src/bootloader-stamp"
  "/media/maikeu/b1e73891-ddde-49a0-9382-903accb68b49/GitClones/KryonOS/build-cyd/bootloader-prefix/src"
  "/media/maikeu/b1e73891-ddde-49a0-9382-903accb68b49/GitClones/KryonOS/build-cyd/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/media/maikeu/b1e73891-ddde-49a0-9382-903accb68b49/GitClones/KryonOS/build-cyd/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/media/maikeu/b1e73891-ddde-49a0-9382-903accb68b49/GitClones/KryonOS/build-cyd/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()
