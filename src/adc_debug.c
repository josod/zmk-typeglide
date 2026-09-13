
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/adc.h>

LOG_MODULE_REGISTER(typeglide_adc_debug, CONFIG_ZMK_LOG_LEVEL);

#define USER_NODE DT_PATH(zephyr_user)

static const struct adc_dt_spec adc_x =
    ADC_DT_SPEC_GET_BY_IDX(USER_NODE, 0);

static const struct adc_dt_spec adc_y =
    ADC_DT_SPEC_GET_BY_IDX(USER_NODE, 1);

static int16_t raw_x;
static int16_t raw_y;

static int read_adc(const struct adc_dt_spec *spec, int16_t *raw)
{
    struct adc_sequence sequence = {
        .buffer = raw,
        .buffer_size = sizeof(*raw),
    };

    int ret = adc_sequence_init_dt(spec, &sequence);

    if (ret < 0) {
        return ret;
    }

    return adc_read_dt(spec, &sequence);
}

static void adc_debug_thread(void)
{
    int ret;

    if (!adc_is_ready_dt(&adc_x)) {
        LOG_ERR("ADC X device is not ready");
        return;
    }

    if (!adc_is_ready_dt(&adc_y)) {
        LOG_ERR("ADC Y device is not ready");
        return;
    }

    ret = adc_channel_setup_dt(&adc_x);

    if (ret < 0) {
        LOG_ERR("ADC X setup failed: %d", ret);
        return;
    }

    ret = adc_channel_setup_dt(&adc_y);

    if (ret < 0) {
        LOG_ERR("ADC Y setup failed: %d", ret);
        return;
    }

    LOG_INF("ADC DEBUG: started");
    LOG_INF("ADC X: channel=%d", adc_x.channel_id);
    LOG_INF("ADC Y: channel=%d", adc_y.channel_id);

    LOG_INF("ADC CALIBRATION: starting in 3 seconds...");
     k_sleep(K_SECONDS(3));

     int16_t min_x = 4095;
     int16_t max_x = 0;
     int16_t min_y = 4095;
     int16_t max_y = 0;

     int64_t start = k_uptime_get();

     LOG_INF("ADC CALIBRATION: MOVE JOYSTICK THROUGH FULL RANGE");

     while (1) {
         ret = read_adc(&adc_x, &raw_x);

         if (ret < 0) {
             LOG_ERR("ADC X read failed: %d", ret);
             k_sleep(K_SECONDS(1));
             continue;
         }

         ret = read_adc(&adc_y, &raw_y);

         if (ret < 0) {
             LOG_ERR("ADC Y read failed: %d", ret);
             k_sleep(K_SECONDS(1));
             continue;
         }

         LOG_INF("ADC_RAW X=%d Y=%d", raw_x, raw_y);

         k_sleep(K_SECONDS(1));
     }

     LOG_INF(
         "ADC CAL RESULT: X=%d..%d Y=%d..%d",
         min_x, max_x, min_y, max_y);
}

K_THREAD_DEFINE(
    adc_debug_thread_id,
    1024,
    adc_debug_thread,
    NULL,
    NULL,
    NULL,
    7,
    0,
    0
);
