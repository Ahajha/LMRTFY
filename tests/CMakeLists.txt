add_executable(tests test.cpp)

target_link_libraries(tests PUBLIC lmrtfy)

target_compile_features(tests PRIVATE cxx_std_17)
