add_executable(examples examples.cpp)

target_link_libraries(examples PUBLIC lmrtfy)

target_compile_features(examples PRIVATE cxx_std_17)
