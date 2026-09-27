idf_component_register(
    SRCS
        "dance.cpp"
        "clap_tempo_estimator.cpp"
    INCLUDE_DIRS "include"
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
