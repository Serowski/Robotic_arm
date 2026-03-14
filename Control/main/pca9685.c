#include "pca9685.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include <math.h>

static const char *TAG = "PCA9685";

// --- INICJALIZACJA I2C I PCA9685 ---
esp_err_t pca9685_init(pca9685_t *pca, i2c_port_t i2c_port, int sda_pin, int scl_pin)
{
    pca->i2c_port = i2c_port;
    pca->address = PCA9685_ADDR;
    pca->frequency = PCA9685_DEFAULT_FREQ;

    // Inicjalizacja I2C
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = sda_pin,
        .scl_io_num = scl_pin,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 400000, // 400 kHz
    };

    esp_err_t err = i2c_param_config(i2c_port, &conf);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Błąd konfiguracji I2C: %s", esp_err_to_name(err));
        return err;
    }

    err = i2c_driver_install(i2c_port, I2C_MODE_MASTER, 0, 0, 0);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Błąd instalacji sterownika I2C: %s", esp_err_to_name(err));
        return err;
    }

    // Resetuj wszystkie kanały
    for (int i = 0; i < PCA9685_CHANNELS; i++)
    {
        pca->channel_values[i] = 0;
    }

    // Konfiguracja MODE1 - wyjdź z sleep mode i włącz Auto-Increment
    uint8_t mode1 = 0x20; // Bit 5 = Auto-Increment (AI), Sleep = 0 (normalny tryb)
    err = pca9685_write_register(pca, PCA9685_MODE1, &mode1, 1);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Błąd zapisu do MODE1: %s", esp_err_to_name(err));
        return err;
    }

    // Konfiguracja MODE2
    uint8_t mode2 = 0x04; // Logika wyjścia aktywna high, totem-pole
    err = pca9685_write_register(pca, PCA9685_MODE2, &mode2, 1);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Błąd zapisu do MODE2: %s", esp_err_to_name(err));
        return err;
    }

    // Ustaw domyślną częstotliwość
    err = pca9685_set_frequency(pca, PCA9685_DEFAULT_FREQ);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Błąd ustawiania częstotliwości: %s", esp_err_to_name(err));
        return err;
    }

    // Wyłącz wszystkie kanały na starcie
    err = pca9685_disable_all(pca);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Błąd wyłączania kanałów: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "PCA9685 zainicjalizowany na adresie 0x%02X", pca->address);
    return ESP_OK;
}

// --- ZAPIS DO REJESTRU ---
esp_err_t pca9685_write_register(pca9685_t *pca, uint8_t reg_addr, const uint8_t *data, size_t len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();

    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (pca->address << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg_addr, true);
    i2c_master_write(cmd, (uint8_t *)data, len, true);
    i2c_master_stop(cmd);

    esp_err_t err = i2c_master_cmd_begin(pca->i2c_port, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Błąd zapisu do rejestru 0x%02X: %s", reg_addr, esp_err_to_name(err));
    }
    return err;
}

// --- ODCZYT Z REJESTRU ---
esp_err_t pca9685_read_register(pca9685_t *pca, uint8_t reg_addr, uint8_t *data, size_t len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();

    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (pca->address << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg_addr, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (pca->address << 1) | I2C_MASTER_READ, true);
    i2c_master_read(cmd, data, len, I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd);

    esp_err_t err = i2c_master_cmd_begin(pca->i2c_port, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Błąd odczytu rejestru 0x%02X: %s", reg_addr, esp_err_to_name(err));
    }
    return err;
}

// --- USTAWIENIE CZĘSTOTLIWOŚCI PWM ---
esp_err_t pca9685_set_frequency(pca9685_t *pca, uint16_t freq)
{
    // Formuła: prescale = (25MHz / (4096 * freq)) - 1
    // 25MHz to wewnętrzny zegar PCA9685
    uint8_t prescale = (uint8_t)((25000000 / (4096 * freq)) - 1);

    // Przełącz do sleep mode
    uint8_t mode1;
    esp_err_t err = pca9685_read_register(pca, PCA9685_MODE1, &mode1, 1);
    if (err != ESP_OK)
        return err;

    mode1 = (mode1 & 0x7F) | 0x10; // Ustaw sleep bit
    err = pca9685_write_register(pca, PCA9685_MODE1, &mode1, 1);
    if (err != ESP_OK)
        return err;

    // Zapisz prescale
    err = pca9685_write_register(pca, PCA9685_PRESCALE, &prescale, 1);
    if (err != ESP_OK)
        return err;

    // Wyjdź ze sleep mode
    mode1 = (mode1 & 0xEF); // Wyczyść sleep bit
    err = pca9685_write_register(pca, PCA9685_MODE1, &mode1, 1);
    if (err != ESP_OK)
        return err;

    pca->frequency = freq;
    ESP_LOGI(TAG, "Częstotliwość ustawiona na %d Hz (prescale: %d)", freq, prescale);
    return ESP_OK;
}

// --- USTAWIENIE PWM NA KANALE ---
esp_err_t pca9685_set_pwm(pca9685_t *pca, uint8_t channel, uint16_t on, uint16_t off)
{
    if (channel >= PCA9685_CHANNELS)
    {
        ESP_LOGE(TAG, "Nieważny kanał: %d", channel);
        return ESP_ERR_INVALID_ARG;
    }

    // Ograniczyć wartości do 0-4095
    on = (on > 4095) ? 4095 : on;
    off = (off > 4095) ? 4095 : off;

    // Adres rejestru: 0x06 + 4 * kanał
    uint8_t reg_on_l = PCA9685_CH0_ON_L + (channel * 4);
    uint8_t reg_off_l = PCA9685_CH0_OFF_L + (channel * 4);

    // Zapisz bajty ON
    uint8_t on_data[2] = {(uint8_t)(on & 0xFF), (uint8_t)((on >> 8) & 0x1F)};
    esp_err_t err = pca9685_write_register(pca, reg_on_l, on_data, 2);
    if (err != ESP_OK)
        return err;

    // Zapisz bajty OFF
    uint8_t off_data[2] = {(uint8_t)(off & 0xFF), (uint8_t)((off >> 8) & 0x1F)};
    err = pca9685_write_register(pca, reg_off_l, off_data, 2);
    if (err != ESP_OK)
        return err;

    pca->channel_values[channel] = off; // Zapamiętaj wartość
    return ESP_OK;
}

// --- USTAWIENIE KĄTA SERWOMECHANIZMU ---
// Dla serwomechanizmu RC (dostosowany):
// - 0° -> 700us impuls (~143 @ 50Hz)
// - 90° -> ~1500us impuls (~307 @ 50Hz)
// - 180° -> 2300us impuls (~471 @ 50Hz)
esp_err_t pca9685_set_servo_angle(pca9685_t *pca, uint8_t channel, float angle)
{
    if (channel >= PCA9685_CHANNELS)
    {
        ESP_LOGE(TAG, "Nieważny kanał: %d", channel);
        return ESP_ERR_INVALID_ARG;
    }

    // Ograniczyć kąt do 0-180°
    angle = (angle < 0) ? 0 : (angle > 180) ? 180
                                            : angle;

    // Mapowanie: 0° = 143, 180° = 471 @ 50Hz
    uint16_t pwm_value = (uint16_t)(143 + (angle / 180.0f) * 328);

    // Ustaw ON = 0, OFF = wartość PWM
    return pca9685_set_pwm(pca, channel, 0, pwm_value);
}

// --- WYŁĄCZENIE KANAŁU ---
esp_err_t pca9685_disable_channel(pca9685_t *pca, uint8_t channel)
{
    if (channel >= PCA9685_CHANNELS)
    {
        ESP_LOGE(TAG, "Nieważny kanał: %d", channel);
        return ESP_ERR_INVALID_ARG;
    }

    // Ustaw ALL_CALL_I2C na OFF
    uint8_t reg_off_l = PCA9685_CH0_OFF_L + (channel * 4);
    uint8_t off_data[2] = {0x00, 0x10}; // Bit 4 = all call off
    return pca9685_write_register(pca, reg_off_l, off_data, 2);
}

// --- WYŁĄCZENIE WSZYSTKICH KANAŁÓW ---
esp_err_t pca9685_disable_all(pca9685_t *pca)
{
    uint8_t off_data[2] = {0x00, 0x10}; // Wyłącz wszystko
    return pca9685_write_register(pca, PCA9685_ALL_LED_OFF_L, off_data, 2);
}
