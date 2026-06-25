idf_component_register(
    SRCS
        "src/decoder.cpp"
        "vendor/ggwave/src/ggwave.cpp"
    INCLUDE_DIRS
        "include"
        "vendor/ggwave/include"
        "vendor/ggwave/src"
    REQUIRES tl_expected
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-missing-field-initializers -Wno-dangling-reference -Wno-error=format)
