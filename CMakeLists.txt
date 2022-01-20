cmake_minimum_required(VERSION 3.14)
project(lmrtfy
	DESCRIPTION "A C++20 thread pool library"
	LANGUAGES CXX)

add_library(lmrtfy INTERFACE)

target_include_directories(lmrtfy INTERFACE include)

target_compile_features(lmrtfy INTERFACE cxx_std_20)

include(FetchContent)

FetchContent_Declare(
	Function2
	GIT_REPOSITORY "https://github.com/Naios/function2"
	GIT_TAG "4.2.0"
)

FetchContent_MakeAvailable(Function2)

find_package( Threads )

target_link_libraries(lmrtfy INTERFACE function2 ${CMAKE_THREAD_LIBS_INIT})

# Tests only available if this is the main app
if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
  add_subdirectory(tests)
endif()
