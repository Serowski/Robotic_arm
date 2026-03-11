#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h" // Biblioteka do precyzyjnych opóźnień mikrosekundowych
#include <string.h>
#include "driver/uart.h"

#include "freertos/queue.h"
QueueHandle_t stepper_queue;

// --- KONFIGURACJA UART ---
#define UART_PORT_NUM UART_NUM_0 // Domyślny port podłączony do USB
#define UART_BAUD_RATE 115200    // Prędkość komunikacji (standard dla ESP-IDF)
#define BUF_SIZE 1024            // Rozmiar bufora pamięci
// --- KONFIGURACJA PINÓW ---
#define STEP_PIN GPIO_NUM_25
#define DIR_PIN GPIO_NUM_26
// --- PARAMETRY ---
// Maksymalna liczba kroków w jedną stronę (200 kroków na 360 deg silnika * 20 obroków wału)
#define MAX_STEPS 4000
// Maksymalne prędkości i przyspieszenia (w us)
#define MAX_DELAY_US 2500
#define MIN_DELAY_US 1300
#define DELAY_CHANGE 4

void move_stepper(int steps, int dir, int max_delay_us, int min_delay_us, int delay_change)
{
    // Ustawienie kierunku
    gpio_set_level(DIR_PIN, dir);

    // Obliczenia: ile kroków zajmie nam pełne rozpędzenie się do min_delay_us?
    int steps_to_full_speed = (max_delay_us - min_delay_us) / delay_change;

    // Zabezpieczenie krótkiego ruchu
    int accel_steps = steps_to_full_speed;
    if (steps / 2 < steps_to_full_speed)
    {
        accel_steps = steps / 2;
    }

    int current_delay = max_delay_us;

    // Główna pętla ruchu
    for (int i = 0; i < steps; i++)
    {

        // --- Rozpędzanie (Zmniejszamy delay) ---
        if (i < accel_steps)
        {
            current_delay -= delay_change;
        }
        // --- Hamowanie (Zwiększamy delay) ---
        else if (i >= steps - accel_steps)
        {
            current_delay += delay_change;
        }

        // Zabezpieczenie przed błędami matematycznymi
        if (current_delay < min_delay_us)
            current_delay = min_delay_us;

        // --- Impuls na silnik ---
        gpio_set_level(STEP_PIN, 1);
        esp_rom_delay_us(10);
        gpio_set_level(STEP_PIN, 0);

        esp_rom_delay_us(current_delay - 10);

        // --- Ochrona Watchdoga ---
        if (i % 1000 == 0)
        {
            vTaskDelay(pdMS_TO_TICKS(1)); // Dajemy systemowi 1 ms oddechu co 1000 kroków
        }
    }
}

void stepper_task(void *pvParameters)
{

    // Inicjalizacja pinów
    gpio_reset_pin(STEP_PIN);
    gpio_reset_pin(DIR_PIN);
    gpio_set_direction(STEP_PIN, GPIO_MODE_OUTPUT);
    gpio_set_direction(DIR_PIN, GPIO_MODE_OUTPUT);

    int current_abs_pos = 0; // Aktualna pozycja w krokach (0 do MAX_STEPS/2)
    bool direction = true;   // true = w prawo, false = w lewo
    int deg_to_steps = 0;
    int move_steps = 0;
    float received_degree = 0.0f;

    // --- SYMULACJA ODBIERANIA DANYCH ---
    bool test = true;

    //  Główna pętla ruchu
    while (1)
    {
        // Przełożenie przekładni 1:20 => 1 obrót silnika = 1/20 obrotu całości
        // 4000 kroków na obrót 360 => 1 krok = 0.09 stopniad
        // Od -180 do +180 stopni => -2000 do +2000 kroków
        // received_degree = 180.0f * (test ? 1 : -1);
        if (xQueueReceive(stepper_queue, &received_degree, portMAX_DELAY) == pdPASS)
        {
            if (received_degree > 180.0f || received_degree < -180.0f)
            {
                printf("Otrzymany kąt jest poza zakresem! Oczekiwany zakres: -180 do 180 stopni.\n");
                received_degree = 0.0f; // Reset do bezpiecznej pozycji
                continue;
            }

            deg_to_steps = received_degree / 0.09f;
            move_steps = (int)(deg_to_steps - current_abs_pos);

            if (move_steps > 0)
            {
                direction = true; // Kierunek w prawo
                gpio_set_level(DIR_PIN, 1);
            }
            else
            {
                direction = false; // Kierunek w lewo
                move_steps = -move_steps;
                gpio_set_level(DIR_PIN, 0);
            }

            move_stepper(move_steps, direction, MAX_DELAY_US, MIN_DELAY_US, DELAY_CHANGE);
            vTaskDelay(pdMS_TO_TICKS(1000));

            current_abs_pos += move_steps * (direction ? 1 : -1);

            // test = !test;
        }
    }
}

// --- ZADANIE KOMUNIKACYJNE UART (Core 0) ---
void uart_task(void *pvParameters)
{
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    uart_driver_install(UART_NUM_0, 1024, 0, 0, NULL, 0);
    uart_param_config(UART_NUM_0, &uart_config);
    uart_set_pin(UART_NUM_0, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    char rx_buf[64]; // "Poczekalnia" na układanie liter
    int rx_pos = 0;  // Licznik, na którym miejscu w poczekalni jesteśmy

    printf("\nRdzen 0: UART gotowy. Wpisz np. 'S 1000' i wcisnij Enter.\n> ");

    while (1)
    {
        uint8_t c;
        // Czekamy (portMAX_DELAY) na DOKŁADNIE 1 znak z klawiatury
        if (uart_read_bytes(UART_NUM_0, &c, 1, portMAX_DELAY) > 0)
        {

            // 1. Sprawdzamy czy to klawisz ENTER (\r lub \n)
            if (c == '\r' || c == '\n')
            {
                if (rx_pos > 0)
                {
                    rx_buf[rx_pos] = '\0'; // Zamykamy zdanie (tworzymy pełnego stringa)
                    printf("\n");          // Robimy ładne przejście do nowej linii

                    char komenda = 0;
                    float wartosc = 0.0f;

                    // Próbujemy zrozumieć pełne zdanie, np. "S 1000"
                    if (sscanf(rx_buf, "%c %f", &komenda, &wartosc) == 2)
                    {
                        xQueueSend(stepper_queue, &wartosc, 0); // Wysyłamy na Core 1
                        printf("Wysłano do silnika: %.2f\n> ", wartosc);
                    }
                    else
                    {
                        printf("Blad skladni. Uzyj formatu: Litera Spacja Liczba (np. S 1000)\n> ");
                    }
                    rx_pos = 0; // CZYŚCIMY BUFOR dla kolejnej komendy!
                }
            }
            // 2. Obsługa klawisza BACKSPACE (kasowanie błędów)
            else if (c == '\b' || c == 127)
            {
                if (rx_pos > 0)
                {
                    rx_pos--; // Cofamy licznik
                    // Kasujemy znak wizualnie w terminalu
                    const char backspace_str[] = "\b \b";
                    uart_write_bytes(UART_NUM_0, backspace_str, 3);
                }
            }
            // 3. To zwykła litera/cyfra - dodajemy do bufora i wyświetlamy (Echo)
            else if (rx_pos < sizeof(rx_buf) - 1)
            {
                rx_buf[rx_pos++] = c;
                uart_write_bytes(UART_NUM_0, (const char *)&c, 1);
            }
        }
    }
}

void app_main(void)
{
    // printf("System uruchomiony.\n");
    printf("\n--- START SYSTEMU ---\n");

    // 1. KROK KRYTYCZNY: NAJPIERW tworzymy kolejkę
    stepper_queue = xQueueCreate(10, sizeof(float));

    // Dodajemy małe zabezpieczenie inżynierskie
    if (stepper_queue == NULL)
    {
        printf("BLAD KRYTYCZNY: Nie udalo sie utworzyc kolejki w pamieci!\n");
        return; // Zatrzymujemy działanie, żeby uniknąć crasha
    }

    xTaskCreatePinnedToCore(
        stepper_task,
        "stepper_task",
        4096,
        NULL,
        5,
        NULL,
        1 // Nr rdzenia do zadania
    );

    xTaskCreatePinnedToCore(
        uart_task,
        "uart_task",
        4096,
        NULL,
        5,
        NULL,
        0 // Rdzeń 0!
    );
}