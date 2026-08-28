cmake_minimum_required(VERSION 3.21)

# EudoraCore — the platform-independent business logic of Eudora 6.2.4,
# extracted from the legacy Carbon/CodeWarrior sources and rebuilt as a
# modern C++20 static library for Apple Silicon (and any POSIX host).
#
# On macOS this configures for arm64 / macOS 26 by default:
#   cmake -B build -S core
# On Linux (CI / development container) the same tree builds and the unit
# tests run natively.

if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
  set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "Build architectures" FORCE)
endif()
if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "26.0" CACHE STRING "Minimum macOS version" FORCE)
endif()

project(EudoraCore
  VERSION 0.1.0
  DESCRIPTION "Eudora 6.2.4 mail engine, modernized"
  LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)

option(EUDORA_ENABLE_TLS "Build TLS transport support (requires OpenSSL)" ON)
option(EUDORA_BUILD_TESTS "Build unit tests" ON)

add_library(eudora_core STATIC)

# Both the curated public headers (include/eudora) and the module headers
# (compat/, mailstore/, mail/, net/, protocols/, filters/) are consumable;
# Swift/Cocoa frontends normally use only include/eudora.
target_include_directories(eudora_core
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR})

if(MSVC)
  target_compile_options(eudora_core PRIVATE /W4)
else()
  target_compile_options(eudora_core PRIVATE -Wall -Wextra)
endif()

# Sources are appended here as each subsystem lands.
target_sources(eudora_core PRIVATE
  compat/pstring.cpp
  compat/macroman.cpp
  compat/macdate.cpp
  compat/hashes.cpp
  mailstore/table_of_contents.cpp
  mailstore/toc_format.cpp
  mailstore/toc_io.cpp
  mailstore/line_reader.cpp
  mailstore/mbox_parser.cpp
  mailstore/compaction.cpp
  mail/mime_codec.cpp
  mail/rfc2047.cpp
  mail/lex822.cpp
  mail/header_parser.cpp
  mail/address_parser.cpp
  mail/composer.cpp
  net/line_receiver.cpp
  net/posix_transport.cpp
  protocols/md5.c
  protocols/sasl.cpp
  protocols/pop3.cpp
  protocols/smtp.cpp
  protocols/imap.cpp
  filters/regexp.cpp
  filters/filter_file.cpp
  filters/match_engine.cpp
  addressbook/nicknames.cpp
  api/eudora_core_c.cpp)

if(EUDORA_ENABLE_TLS)
  find_package(OpenSSL QUIET)
  if((OpenSSL_FOUND OR OPENSSL_FOUND)
     AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/net/tls_transport.cpp)
    target_sources(eudora_core PRIVATE net/tls_transport.cpp)
    target_link_libraries(eudora_core PUBLIC OpenSSL::SSL OpenSSL::Crypto)
    target_compile_definitions(eudora_core PUBLIC EUDORA_HAVE_TLS=1)
  else()
    message(STATUS "TLS transport disabled (OpenSSL missing or source not yet present)")
  endif()
endif()

if(EUDORA_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
