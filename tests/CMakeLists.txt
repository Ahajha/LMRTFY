FetchContent_Declare(
	doctest
	GIT_REPOSITORY "https://github.com/doctest/doctest"
	GIT_TAG "2.4.7"
)

FetchContent_MakeAvailable(doctest)

add_executable(tests test.cpp)

target_link_libraries(tests PUBLIC lmrtfy doctest)

target_compile_features(tests PRIVATE cxx_std_17)

if (MSVC)
	add_definitions(/W4 /WX)
else()
	add_definitions(-Wall -Wextra -Wshadow -Wpedantic -Werror)
endif()
