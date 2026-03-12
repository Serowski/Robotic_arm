#ifndef PCA9685_H
#define PCA9685_H

#include "driver/i2c.h"

// --- KONFIGURACJA PCA9685 ---
#define PCA9685_ADDR 0x40       // Adres I2C (domyślnie 0x40)
#define PCA9685_CHANNELS 16     // Liczba kanałów PWM (0-15)
#define PCA9685_RESOLUTION 4096 // 12-bitowa rozdzielczość (0-4095)
#define PCA9685_DEFAULT_FREQ 50 // Domyślna częstotliwość (Hz) - dla serwomechanizmów RC

// --- REJESTRY PCA9685 ---
#define PCA9685_MODE1 0x00
#define PCA9685_MODE2 0x01
#define PCA9685_CH0_ON_L 0x06
#define PCA9685_CH0_ON_H 0x07
#define PCA9685_CH0_OFF_L 0x08
#define PCA9685_CH0_OFF_H 0x09
#define PCA9685_PRESCALE 0xFE
#define PCA9685_ALL_LED_ON_L 0xFA
#define PCA9685_ALL_LED_ON_H 0xFB
#define PCA9685_ALL_LED_OFF_L 0xFC
#define PCA9685_ALL_LED_OFF_H 0xFD

// --- STRUKTURA DANYCH ---
typedef struct
{
    i2c_port_t i2c_port;
    uint8_t address;
    uint16_t frequency;
    uint16_t channel_values[PCA9685_CHANNELS]; // Aktualne wartości PWM (0-4095)
} pca9685_t;

// --- DEKLARACJE FUNKCJI ---

/**
 * @brief Inicjalizuje PCA9685
 * @param pca Wskaźnik na strukturę PCA9685
 * @param i2c_port Port I2C do użycia
 * @param sda_pin Pin SDA
 * @param scl_pin Pin SCL
 * @return ESP_OK jeśli sukces
 */
esp_err_t pca9685_init(pca9685_t *pca, i2c_port_t i2c_port, int sda_pin, int scl_pin);

/**
 * @brief Ustawia częstotliwość PWM
 * @param pca Wskaźnik na strukturę PCA9685
 * @param freq Częstotliwość w Hz (25-1600)
 * @return ESP_OK jeśli sukces
 */
esp_err_t pca9685_set_frequency(pca9685_t *pca, uint16_t freq);

/**
 * @brief Ustawia wartość PWM na kanale (0-4095)
 * @param pca Wskaźnik na strukturę PCA9685
 * @param channel Numer kanału (0-15)
 * @param on Czas włączenia (0-4095)
 * @param off Czas wyłączenia (0-4095)
 * @return ESP_OK jeśli sukces
 */
esp_err_t pca9685_set_pwm(pca9685_t *pca, uint8_t channel, uint16_t on, uint16_t off);

/**
 * @brief Ustawia pozycję serwomechanizmu na kanale (w stopniach)
 * @param pca Wskaźnik na strukturę PCA9685
 * @param channel Numer kanału (0-15)
 * @param angle Kąt w stopniach (0-180)
 * @return ESP_OK jeśli sukces
 */
esp_err_t pca9685_set_servo_angle(pca9685_t *pca, uint8_t channel, float angle);

/**
 * @brief Wyłącza kanał (ustawia wartość 0)
 * @param pca Wskaźnik na strukturę PCA9685
 * @param channel Numer kanału (0-15)
 * @return ESP_OK jeśli sukces
 */
esp_err_t pca9685_disable_channel(pca9685_t *pca, uint8_t channel);

/**
 * @brief Wyłącza wszystkie kanały
 * @param pca Wskaźnik na strukturę PCA9685
 * @return ESP_OK jeśli sukces
 */
esp_err_t pca9685_disable_all(pca9685_t *pca);

/**
 * @brief Czyta rejestr z PCA9685
 * @param pca Wskaźnik na strukturę PCA9685
 * @param reg_addr Adres rejestru
 * @param data Wskaźnik na bufor danych
 * @param len Liczba bajtów do przeczytania
 * @return ESP_OK jeśli sukces
 */
esp_err_t pca9685_read_register(pca9685_t *pca, uint8_t reg_addr, uint8_t *data, size_t len);

/**
 * @brief Pisze do rejestru PCA9685
 * @param pca Wskaźnik na strukturę PCA9685
 * @param reg_addr Adres rejestru
 * @param data Wskaźnik na dane do wysłania
 * @param len Liczba bajtów do wysłania
 * @return ESP_OK jeśli sukces
 */
esp_err_t pca9685_write_register(pca9685_t *pca, uint8_t reg_addr, const uint8_t *data, size_t len);

#endif // PCA9685_H
