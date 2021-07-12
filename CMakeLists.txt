cmake_minimum_required(VERSION 3.12)
project(lmrtfy
	DESCRIPTION "A C++17 thread pool library"
	LANGUAGES CXX)

add_library(lmrtfy INTERFACE)

target_include_directories(lmrtfy INTERFACE include)

target_compile_features(lmrtfy INTERFACE cxx_std_17)

add_subdirectory(extern/function2)

target_link_libraries(lmrtfy INTERFACE function2)
