cmake_minimum_required(VERSION 2.8.4)
project(v8interpreter)

set(SOURCE_FILES v8interpreter.cpp)

include_directories(/usr/local/include ../apone/mods)

if(APPLE)
  set(ICUPATH /usr/local/opt/icu4c/lib)
  find_library(V8_BASE v8_base ${V8LIBS})
  find_library(V8_LIBBASE v8_libbase ${V8LIBS})
  find_library(V8_SNAPSHOT v8_snapshot ${V8LIBS})
  find_library(ICUDATA icudata ${ICUPATH})
  find_library(ICUUC icuuc ${ICUPATH})
  find_library(ICUI18N icui18n ${ICUPATH})
else()
  set(PWD /home/sasq/project/v8interpreter)
  set(SD ${PWD}/v8build/v8/out/native/obj.target)
set(V8_STATIC_LIBS
  ${SD}/third_party/icu/libicudata.a
  ${SD}/third_party/icu/libicui18n.a
  ${SD}/third_party/icu/libicuuc.a
  ${SD}/tools/gyp/libv8_base.a
  ${SD}/tools/gyp/libv8_external_snapshot.a
  ${SD}/tools/gyp/libv8_libbase.a
  ${SD}/tools/gyp/libv8_libplatform.a
PARENT_SCOPE)
  set(V8_STATIC_DIR ${PWD}/v8build/v8/out/native/obj.target PARENT_SCOPE)
  include_directories(${PWD}/v8build/v8/include)
endif()

add_definitions(-DUSE_APONE)

#find_path(V8PATH v8.h)

FILE(GLOB_RECURSE IncFiles "*.h")
add_custom_target(headers SOURCES ${IncFiles})

set(V8_LIBS ${V8_BASE} ${V8_LIBBASE} ${V8_SNAPSHOT} ${ICUDATA} ${ICUUC} ${ICUI18N})

add_library(v8interpreter ${SOURCE_FILES})
target_link_libraries(v8interpreter ${V8_LIBS})
