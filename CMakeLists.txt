cmake_minimum_required(VERSION 2.8.4)
project(v8interpreter)

set(SOURCE_FILES v8interpreter.cpp)

if(APPLE)
  set(ICUPATH /usr/local/opt/icu4c/lib)
  include_directories(/usr/local/include)
endif()

find_library(V8_BASE v8_base)
find_library(V8_LIBBASE v8_libbase)
find_library(V8_SNAPSHOT v8_snapshot)
find_library(ICUDATA icudata ${ICUPATH})
find_library(ICUUC icuuc ${ICUPATH})
find_library(ICUI18N icui18n ${ICUPATH})

find_path(V8PATH v8.h)
message(${V8PATH})

FILE(GLOB_RECURSE IncFiles "*.h")
add_custom_target(headers SOURCES ${IncFiles})

set(V8_LIBS ${V8_BASE} ${V8_LIBBASE} ${V8_SNAPSHOT} ${ICUDATA} ${ICUUC} ${ICUI18N})

add_library(v8interpreter ${SOURCE_FILES})
target_link_libraries(v8interpreter ${V8_LIBS})
