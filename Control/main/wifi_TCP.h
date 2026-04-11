#ifndef WIFI_TCP_H
#define WIFI_TCP_H

#include <stdint.h>
#include "esp_err.h"

// --- KONFIGURACJA WiFi ---
#define WIFI_SSID "xxxx"    // Nazwa sieci WiFi (zmień na swoją)
#define WIFI_PASSWORD "xxx" // Hasło WiFi (zmień na swoje)
#define WIFI_MAX_RETRIES 10

// --- KONFIGURACJA TCP ---
#define SERVER_HOST "xxx.xxx.xxx.xxx" // IP komputera (serwer)
#define SERVER_PORT 5000              // Port serwera
#define TCP_RX_BUFFER 512             // Rozmiar bufora odbioru
#define TCP_TX_BUFFER 512             // Rozmiar bufora wysyłania

// --- STRUKTURY DANYCH ---
typedef struct
{
    union
    {
        struct
        {
            uint8_t type;    // 0x01 = Stepper, 0x02 = Servo
            float value;     // Wartość (kąt)
            uint8_t channel; // Kanał (dla serw)
        } data;
        uint8_t raw[10];
    } cmd;
} tcp_command_t;

typedef struct
{
    union
    {
        struct
        {
            uint8_t status;      // Status (0=OK, 1=ERROR)
            float current_angle; // Aktualny kąt
            uint8_t type;        // Typ (stepper/servo)
        } data;
        uint8_t raw[10];
    } response;
} tcp_response_t;

// --- FUNKCJE PUBLIC ---
esp_err_t wifi_init(void);
esp_err_t wifi_tcp_connect(void);
int tcp_send(const uint8_t *data, int len);
int tcp_receive(uint8_t *data, int max_len);
void tcp_disconnect(void);
void wifi_tcp_task(void *pvParameters);

#endif // WIFI_TCP_H
