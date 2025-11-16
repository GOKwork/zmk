#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/logging/log.h>

#include <zmk/keymap.h>

LOG_MODULE_REGISTER(stick_layers, LOG_LEVEL_INF);

/* ========= 設定ここから ========= */

/* 使う ADC デバイス (nRF52 の SAADC は通常 "ADC_0" か "ADC_1" ) */
#define STICK_ADC_LABEL      "ADC_0"

/* 使うチャネル番号（SAADCの物理チャネル）。
 * 例: P0.29 = AIN5 → channel 5 に対応することが多い。
 * 実際のボード定義に合わせて必要なら変えてね。
 */
#define STICK_ADC_CHANNEL    5

/* 読み値のビット幅 (12bit → 0〜4095) */
#define STICK_RESOLUTION     12

/* サンプル周期（ミリ秒） */
#define STICK_POLL_INTERVAL_MS  15

/* 中立付近＆しきい値（後で調整する値） */
#define STICK_CENTER_RAW     2048    /* だいたい真ん中くらいの仮値 */
#define STICK_DEADZONE       300     /* ここより内側は「中立」扱い */
#define STICK_THRESHOLD      800     /* これより外なら確実に「倒してる」 */

/* レイヤー番号（好きに変えてOK） */
#define STICK_LAYER_RIGHT    1
#define STICK_LAYER_LEFT     2

/* ========= 設定ここまで ========= */

/* ADC 読み取り用バッファ */
static int16_t stick_sample_buf;

/* 最後に判定した方向 */
enum stick_dir {
    STICK_DIR_CENTER = 0,
    STICK_DIR_LEFT   = -1,
    STICK_DIR_RIGHT  = 1,
};

static enum stick_dir current_dir = STICK_DIR_CENTER;

static const struct device *stick_adc_dev;

/* ADC チャネル設定 */
static struct adc_channel_cfg stick_channel_cfg = {
    .gain             = ADC_GAIN_1,
    .reference        = ADC_REF_INTERNAL,
    .acquisition_time = ADC_ACQ_TIME_DEFAULT,
    .channel_id       = STICK_ADC_CHANNEL,
#if defined(CONFIG_ADC_CONFIGURABLE_INPUTS)
    /* nRF52 の SAADC で AIN5 を使う例 (P0.29) */
    .input_positive   = NRF_SAADC_INPUT_AIN5,
#endif
};

/* 1回分のサンプリング設定 */
static struct adc_sequence stick_seq = {
    .channels    = BIT(STICK_ADC_CHANNEL),
    .buffer      = &stick_sample_buf,
    .buffer_size = sizeof(stick_sample_buf),
    .resolution  = STICK_RESOLUTION,
};

/* 方向に合わせてレイヤーを切り替える */
static void stick_update_layers(enum stick_dir new_dir)
{
    if (new_dir == current_dir) {
        return;
    }

    /* まず全部OFF */
    zmk_keymap_layer_deactivate(STICK_LAYER_RIGHT);
    zmk_keymap_layer_deactivate(STICK_LAYER_LEFT);

    switch (new_dir) {
    case STICK_DIR_RIGHT:
        zmk_keymap_layer_activate(STICK_LAYER_RIGHT);
        LOG_INF("Stick: RIGHT → layer %d ON", STICK_LAYER_RIGHT);
        break;
    case STICK_DIR_LEFT:
        zmk_keymap_layer_activate(STICK_LAYER_LEFT);
        LOG_INF("Stick: LEFT → layer %d ON", STICK_LAYER_LEFT);
        break;
    case STICK_DIR_CENTER:
    default:
        LOG_INF("Stick: CENTER → all stick layers OFF");
        break;
    }

    current_dir = new_dir;
}

/* ADC 読み取りと方向判定 */
static void stick_poll_once(void)
{
    int ret = adc_read(stick_adc_dev, &stick_seq);
    if (ret < 0) {
        LOG_WRN("adc_read failed: %d", ret);
        return;
    }

    /* ADCの値は signed でも実質 0〜4095 の範囲に入るはず */
    int32_t raw = stick_sample_buf;
    if (raw < 0) {
        raw = 0;
    }

    /* 中立を基準にオフセット値にする */
    int32_t delta = raw - STICK_CENTER_RAW;

    enum stick_dir dir = STICK_DIR_CENTER;

    if (delta > STICK_THRESHOLD) {
        dir = STICK_DIR_RIGHT;
    } else if (delta < -STICK_THRESHOLD) {
        dir = STICK_DIR_LEFT;
    } else if (ABS(delta) < STICK_DEADZONE) {
        dir = STICK_DIR_CENTER;
    } else {
        /* デッドゾーン外だが閾値未満 → 前の状態を維持 */
        dir = current_dir;
    }

    stick_update_layers(dir);
}

/* ポーリング用スレッド */
static void stick_thread(void)
{
    while (1) {
        stick_poll_once();
        k_msleep(STICK_POLL_INTERVAL_MS);
    }
}

K_THREAD_DEFINE(stick_thread_id, 1024, stick_thread, NULL, NULL, NULL,
                K_PRIO_COOP(10), 0, 0);

/* 初期化：ADC デバイスとチャネル設定 */
static int stick_init(const struct device *dev)
{
    ARG_UNUSED(dev);

    stick_adc_dev = device_get_binding(STICK_ADC_LABEL);
    if (!stick_adc_dev) {
        LOG_ERR("Failed to get ADC dev %s", STICK_ADC_LABEL);
        return -ENODEV;
    }

    int ret = adc_channel_setup(stick_adc_dev, &stick_channel_cfg);
    if (ret < 0) {
        LOG_ERR("adc_channel_setup failed: %d", ret);
        return ret;
    }

    LOG_INF("stick_layers initialized");

    /* ★テスト：起動したら即レイヤー1をONにしてみる */
    zmk_keymap_layer_activate(STICK_LAYER_RIGHT);

    return 0;
}

SYS_INIT(stick_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
