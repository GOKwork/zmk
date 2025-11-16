#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zmk/keymap.h>

LOG_MODULE_REGISTER(stick_layers_test, LOG_LEVEL_INF);

/* どのレイヤーをON/OFFするか（keymap側で layer_1 を定義してある前提） */
#define TEST_LAYER 1

/* 2秒おきにON/OFFする */
#define TOGGLE_INTERVAL_MS 2000

static void stick_test_thread(void)
{
    bool on = false;

    while (1) {
        if (on) {
            zmk_keymap_layer_deactivate(TEST_LAYER);
            LOG_INF("TEST: layer %d OFF", TEST_LAYER);
        } else {
            zmk_keymap_layer_activate(TEST_LAYER);
            LOG_INF("TEST: layer %d ON", TEST_LAYER);
        }

        on = !on;
        k_msleep(TOGGLE_INTERVAL_MS);
    }
}

K_THREAD_DEFINE(stick_test_thread_id, 1024, stick_test_thread,
                NULL, NULL, NULL,
                K_PRIO_COOP(10), 0, 0);
