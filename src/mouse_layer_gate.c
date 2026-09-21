#define DT_DRV_COMPAT typeglide_mouse_layer_gate

#include <zephyr/device.h>
#include <drivers/input_processor.h>

#include <zmk/keymap.h>

struct mouse_layer_gate_config {
    uint8_t layer;
};

static int mouse_layer_gate_handle_event(const struct device *dev,
                                         struct input_event *event,
                                         uint32_t param1,
                                         uint32_t param2,
                                         struct zmk_input_processor_state *state) {
    const struct mouse_layer_gate_config *cfg = dev->config;

    ARG_UNUSED(event);
    ARG_UNUSED(param1);
    ARG_UNUSED(param2);
    ARG_UNUSED(state);

    if (zmk_keymap_layer_active(cfg->layer)) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    return ZMK_INPUT_PROC_STOP;
}

static const struct zmk_input_processor_driver_api mouse_layer_gate_api = {
    .handle_event = mouse_layer_gate_handle_event,
};

#define MOUSE_LAYER_GATE_INST(n)                                             \
    static const struct mouse_layer_gate_config mouse_layer_gate_config_##n = { \
        .layer = DT_INST_PROP(n, layer),                                    \
    };                                                                       \
                                                                             \
    DEVICE_DT_INST_DEFINE(n, NULL, NULL, NULL,                              \
                          &mouse_layer_gate_config_##n,                     \
                          POST_KERNEL,                                       \
                          CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,              \
                          &mouse_layer_gate_api);

DT_INST_FOREACH_STATUS_OKAY(MOUSE_LAYER_GATE_INST)
