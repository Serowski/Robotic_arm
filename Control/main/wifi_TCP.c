#include "wifi_TCP.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"

// --- ZMIENNE GLOBALNE ---
static int sock = -1; // Socket TCP
static bool wifi_connected = false;
static bool tcp_connected = false;
static EventGroupHandle_t wifi_event_group = NULL;
static const int WIFI_CONNECTED_BIT = BIT0;

// --- FORWARD DECLARATIONS (z main.c) ---
extern QueueHandle_t stepper_queue;
extern QueueHandle_t servo_queue;

typedef struct
{
    uint8_t channel;
    float angle;
} servo_command_t;

// --- OBSŁUGI ZDARZEŃ WiFi ---
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        printf("[WiFi] Startowanie stacji WiFi...\n");
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        printf("[WiFi] Rozłączenie z siecią WiFi\n");
        tcp_connected = false;
        wifi_connected = false;
        xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
        esp_wifi_connect();
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        printf("[WiFi] Uzyskano IP: " IPSTR "\n", IP2STR(&event->ip_info.ip));
        wifi_connected = true;
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

// --- INICJALIZACJA WiFi ---
esp_err_t wifi_init(void)
{
    // Inicjalizacja NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Tworzymy event group
    wifi_event_group = xEventGroupCreate();
    if (wifi_event_group == NULL)
    {
        printf("[WiFi] BŁĄD: Nie można utworzyć event group\n");
        return ESP_FAIL;
    }

    // Inicjalizacja netif
    ESP_ERROR_CHECK(esp_netif_init());

    // Tworzenie event loop
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Tworzenie WiFi netif
    esp_netif_create_default_wifi_sta();

    // Konfiguracja WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Rejestracja event handlerów
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    // Konfiguracja SSID i hasła
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    printf("[WiFi] WiFi zainicjalizowany. Łączenie z: %s\n", WIFI_SSID);

    // Czekamy na połączenie
    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, false, true, 10000 / portTICK_PERIOD_MS);

    if (!wifi_connected)
    {
        printf("[WiFi] OSTRZEŻENIE: Timeout połączenia WiFi\n");
        return ESP_ERR_TIMEOUT;
    }

    printf("[WiFi] ✓ Podłączony do sieci WiFi\n");
    return ESP_OK;
}

// --- POŁĄCZENIE TCP ---
esp_err_t wifi_tcp_connect(void)
{
    if (sock != -1)
    {
        printf("[TCP] Socket już otwarty\n");
        return ESP_OK;
    }

    printf("[TCP] Łączenie do serwera: %s:%d\n", SERVER_HOST, SERVER_PORT);

    struct sockaddr_in dest_addr;
    dest_addr.sin_addr.s_addr = inet_addr(SERVER_HOST);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(SERVER_PORT);

    // Tworzenie socketu
    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (sock < 0)
    {
        printf("[TCP] BŁĄD: Nie można utworzyć socketu\n");
        return ESP_FAIL;
    }

    // Ustawianie timeout
    struct timeval timeout;
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    // Połączenie
    int err = connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err != 0)
    {
        printf("[TCP] BŁĄD: Nie można się połączyć - %d\n", err);
        close(sock);
        sock = -1;
        return ESP_FAIL;
    }

    tcp_connected = true;
    printf("[TCP] ✓ Połączono z serwerem\n");
    return ESP_OK;
}

// --- WYSYŁANIE DANYCH ---
int tcp_send(const uint8_t *data, int len)
{
    if (!tcp_connected || sock < 0)
    {
        printf("[TCP] BŁĄD: Socket nie podłączony\n");
        return -1;
    }

    int sent = send(sock, data, len, 0);
    if (sent < 0)
    {
        printf("[TCP] BŁĄD wysyłania\n");
        tcp_connected = false;
        return -1;
    }

    return sent;
}

// --- ODBIERANIE DANYCH ---
int tcp_receive(uint8_t *data, int max_len)
{
    if (!tcp_connected || sock < 0)
    {
        return -1;
    }

    int len = recv(sock, data, max_len - 1, 0);

    if (len < 0)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            return 0; // Timeout - brak danych
        }
        printf("[TCP] BŁĄD odbierania\n");
        tcp_connected = false;
        return -1;
    }

    if (len == 0)
    {
        printf("[TCP] Serwer zamknął połączenie\n");
        tcp_connected = false;
        return -1;
    }

    return len;
}

// --- ROZŁĄCZENIE TCP ---
void tcp_disconnect(void)
{
    if (sock != -1)
    {
        close(sock);
        sock = -1;
    }
    tcp_connected = false;
    printf("[TCP] Rozłączeno z serwera\n");
}

// --- PARSING KOMEND Z SERWERA ---
// Format JSON: {"type":"stepper","angle":45.5}
// lub: {"type":"servo","channel":0,"angle":90}
// Obsługuje również spacje: {"type": "servo", "channel": 0, "angle": 90}
static void parse_json_command(const char *json_str)
{
    char type[20] = {0};
    float angle = 0.0f;
    uint8_t channel = 0;

    printf("[TCP] Parsowanie: %s\n", json_str);

    // Prosty parser JSON - obsługuje spacje
    char *ptr = strstr(json_str, "\"type\"");
    if (ptr != NULL)
    {
        // Format: "type" : "value" lub "type":"value"
        sscanf(ptr, "\"type\" : \"%19[^\"]\"", type);
        if (strlen(type) == 0)
        {
            sscanf(ptr, "\"type\":\"%19[^\"]\"", type);
        }
        printf("[TCP] DEBUG: type='%s'\n", type);
    }

    ptr = strstr(json_str, "\"angle\"");
    if (ptr != NULL)
    {
        // Format: "angle" : 90.0 lub "angle":90.0
        if (sscanf(ptr, "\"angle\" : %f", &angle) != 1)
        {
            sscanf(ptr, "\"angle\":%f", &angle);
        }
        printf("[TCP] DEBUG: angle=%.2f\n", angle);
    }

    ptr = strstr(json_str, "\"channel\"");
    if (ptr != NULL)
    {
        // Format: "channel" : 0 lub "channel":0
        if (sscanf(ptr, "\"channel\" : %hhu", &channel) != 1)
        {
            sscanf(ptr, "\"channel\":%hhu", &channel);
        }
        printf("[TCP] DEBUG: channel=%d\n", channel);
    }

    // Przetwarzanie komendy
    if (strcmp(type, "stepper") == 0)
    {
        printf("[TCP] ✓ Wysyłam do steppera: kąt=%.2f°\n", angle);
        if (stepper_queue != NULL)
        {
            if (xQueueSend(stepper_queue, &angle, 0) == pdPASS)
            {
                printf("[TCP] ✓ Stepper zadanie wysłane\n");
            }
            else
            {
                printf("[TCP] ✗ BŁĄD: Nie mogę wysłać do stepper_queue\n");
            }
        }
        else
        {
            printf("[TCP] ✗ BŁĄD: stepper_queue = NULL!\n");
        }
    }
    else if (strcmp(type, "servo") == 0)
    {
        printf("[TCP] ✓ Wysyłam do servo: kanał=%d, kąt=%.2f°\n", channel, angle);
        if (servo_queue != NULL)
        {
            servo_command_t servo_cmd = {.channel = channel, .angle = angle};
            if (xQueueSend(servo_queue, &servo_cmd, 0) == pdPASS)
            {
                printf("[TCP] ✓ Servo zadanie wysłane\n");
            }
            else
            {
                printf("[TCP] ✗ BŁĄD: Nie mogę wysłać do servo_queue\n");
            }
        }
        else
        {
            printf("[TCP] ✗ BŁĄD: servo_queue = NULL!\n");
        }
    }
    else if (strcmp(type, "ping") == 0)
    {
        printf("[TCP] Ping otrzymany\n");
    }
    else
    {
        printf("[TCP] ✗ BŁĄD: Nieznany typ: '%s'\n", type);
    }
}

// --- GŁÓWNE ZADANIE TCP ---
void wifi_tcp_task(void *pvParameters)
{
    printf("\n[TCP] Uruchamianie zadania WiFi/TCP...\n");

    uint8_t rx_buffer[TCP_RX_BUFFER];
    int retry_count = 0;
    const int max_retries = 5;

    // Czekamy na połączenie WiFi
    printf("[TCP] Czekanie na połączenie WiFi...\n");
    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, false, true, portMAX_DELAY);
    printf("[TCP] WiFi podłączony!\n");

    // Główna pętla
    while (1)
    {
        // Jeśli nie połączeni - spróbuj połączyć
        if (!tcp_connected)
        {
            if (retry_count < max_retries)
            {
                esp_err_t ret = wifi_tcp_connect();
                if (ret != ESP_OK)
                {
                    retry_count++;
                    printf("[TCP] Próba połączenia %d/%d za 3 sekundy...\n", retry_count, max_retries);
                    vTaskDelay(pdMS_TO_TICKS(3000));
                }
                else
                {
                    retry_count = 0;
                }
            }
            else
            {
                // Zbyt wiele nieudanych prób - czekamy dłużej
                printf("[TCP] Zbyt wiele nieudanych prób. Czekanie 10 sekund...\n");
                vTaskDelay(pdMS_TO_TICKS(10000));
                retry_count = 0;
            }

            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        // Odbieramy dane
        int len = tcp_receive(rx_buffer, TCP_RX_BUFFER);

        if (len > 0)
        {
            // Zamykamy bufor stringiem
            rx_buffer[len] = '\0';
            printf("[TCP] Odebrano %d bajtów: %s\n", len, (const char *)rx_buffer);

            // Parsujemy JSON
            parse_json_command((const char *)rx_buffer);

            // Opcjonalnie: wysyłamy potwierdzenie
            const char *ack = "{\"status\":\"ok\"}\n";
            tcp_send((const uint8_t *)ack, strlen(ack));
        }
        else if (len < 0)
        {
            // Błąd połączenia
            tcp_disconnect();
            retry_count = 0;
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
