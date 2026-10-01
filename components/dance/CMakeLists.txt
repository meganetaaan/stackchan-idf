idf_component_register(
    SRCS
        "dance.cpp"
        "clap_tempo_estimator.cpp"
        "head_stroke_detector.cpp"
        "dance_mode_input.cpp"
    INCLUDE_DIRS "include"
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
