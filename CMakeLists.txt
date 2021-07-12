cmake_minimum_required(VERSION 3.14)
project(lmrtfy
	DESCRIPTION "A C++17 thread pool library"
	LANGUAGES CXX)

add_library(lmrtfy INTERFACE)

target_include_directories(lmrtfy INTERFACE include)

target_compile_features(lmrtfy INTERFACE cxx_std_17)

include(FetchContent)

FetchContent_Declare(
	Function2
	GIT_REPOSITORY "https://github.com/Naios/function2"
	GIT_TAG "02ca99831de59c7c3a4b834789260253cace0ced"
)

FetchContent_MakeAvailable(Function2)

target_link_libraries(lmrtfy INTERFACE function2)

# Examples only available if this is the main app
if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
  add_subdirectory(examples)
endif()
