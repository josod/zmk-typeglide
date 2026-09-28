zephyr_library()

zephyr_include_directories(include)

zephyr_library_sources(
    src/joystick_4way.c
    src/mouse_layer_gate.c
    src/adc_debug.c
    src/input_processor_stop.c
    src/events/joystick_state_changed.c

)


zephyr_library_sources_ifdef(
    CONFIG_ZMK_SPLIT_ROLE_CENTRAL
    src/events/gesture_timeout.c
    src/behaviors/behavior_caps_word_or_scroll.c
)
