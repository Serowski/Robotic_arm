#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h" // Biblioteka do precyzyjnych opóźnień mikrosekundowych
#include <string.h>
#include "driver/uart.h"
#include "driver/i2c.h"
#include "math.h"
#include "freertos/queue.h"
#include "pca9685.h"

QueueHandle_t stepper_queue;
QueueHandle_t servo_queue;
pca9685_t pca9685;
float current_servo_angle[16] = {0.0f};
// --- KONFIGURACJA UART ---
#define UART_PORT_NUM UART_NUM_0 // Domyślny port podłączony do USB
#define UART_BAUD_RATE 115200    // Prędkość komunikacji (standard dla ESP-IDF)
#define BUF_SIZE 1024            // Rozmiar bufora pamięci
// --- KONFIGURACJA PINÓW ---
#define STEP_PIN GPIO_NUM_25
#define DIR_PIN GPIO_NUM_26
// --- KONFIGURACJA I2C ---
#define I2C_PORT I2C_NUM_0
#define SDA_PIN GPIO_NUM_21
#define SCL_PIN GPIO_NUM_22
// --- KONFIGURACJA PARAMETRY ---
// Maksymalna liczba kroków w jedną stronę (200 kroków na 360 deg silnika * 20 obroków wału)
#define MAX_STEPS 4000
// Maksymalne prędkości i przyspieszenia (w us)
#define MAX_DELAY_US 2500
#define MIN_DELAY_US 1300
#define DELAY_CHANGE 4

#define PI 3.141592
// --- FUNKCJE DIAGNOSTYCZNE ---

// Skanowanie I2C
void scan_i2c_devices(i2c_port_t i2c_port)
{
    printf("\n=== SKANOWANIE MAGISTRALI I2C ===\n");
    printf("Szukam urządzeń na pinach GPIO21 (SDA) i GPIO22 (SCL)...\n\n");

    int found_count = 0;
    for (uint8_t addr = 1; addr < 127; addr++)
    {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
        i2c_master_stop(cmd);

        esp_err_t ret = i2c_master_cmd_begin(i2c_port, cmd, pdMS_TO_TICKS(50));
        i2c_cmd_link_delete(cmd);

        if (ret == ESP_OK)
        {
            printf("  ✓ Znaleziono URZADZENIE na adresie 0x%02X\n", addr);
            found_count++;
        }
    }

    if (found_count == 0)
    {
        printf("  ✗ BRAK urzadzen na magistrali I2C!\n");
        printf("    PROBLEM: Sprawdz przewody SDA/SCL, zasilanie, pull-up rezystory\n");
    }
    else
    {
        printf("\nRazem znalezione urz=devices: %d\n", found_count);
    }
    printf("\n");
}

// Odczyt statusu PCA9685
void debug_pca9685_status(pca9685_t *pca)
{
    printf("\n=== STATUS PCA9685 (ADRES 0x%02X) ===\n", pca->address);

    uint8_t mode1, mode2, prescale;
    esp_err_t err1 = pca9685_read_register(pca, PCA9685_MODE1, &mode1, 1);
    esp_err_t err2 = pca9685_read_register(pca, PCA9685_MODE2, &mode2, 1);
    esp_err_t err3 = pca9685_read_register(pca, PCA9685_PRESCALE, &prescale, 1);

    if (err1 != ESP_OK || err2 != ESP_OK || err3 != ESP_OK)
    {
        printf("  ✗ BLAD przy czytaniu rejestrów!\n");
        printf("    MODE1: %s\n", esp_err_to_name(err1));
        printf("    MODE2: %s\n", esp_err_to_name(err2));
        printf("    PRESCALE: %s\n", esp_err_to_name(err3));
        return;
    }

    printf("  MODE1 (0x00): 0x%02X\n", mode1);
    printf("    - Sleep bit: %s\n", (mode1 & 0x10) ? "SLEEP (zimna)" : "AKTYWNY");
    printf("    - Auto increment: %s\n", (mode1 & 0x20) ? "TAK" : "NIE");

    printf("  MODE2 (0x01): 0x%02X\n", mode2);
    printf("  PRESCALE (0xFE): %d (częstotliwość ~%d Hz)\n", prescale,
           (int)(25000000 / (4096 * (prescale + 1))));

    printf("  Stan pamięci: Częstotliwość = %d Hz\n", pca->frequency);
    printf("\n");
}

// Test pojedynczego kanału
void test_servo_channel(pca9685_t *pca, uint8_t channel, float angle)
{
    printf("\n=== TEST KANAŁU %d ===\n", channel);
    printf("Ustawianie kąta: %.2f°\n", angle);

    esp_err_t err = pca9685_set_servo_angle(pca, channel, angle);

    if (err == ESP_OK)
    {
        printf("✓ SUKCES - Kanał %d powinien się ruszać na %.2f°\n", channel, angle);

        // Czytaj wartości tego kanału
        uint8_t reg_off_l = PCA9685_CH0_OFF_L + (channel * 4);
        uint8_t off_data[2] = {0, 0};
        pca9685_read_register(pca, reg_off_l, off_data, 2);
        uint16_t pwm_val = off_data[0] | ((off_data[1] & 0x0F) << 8);
        printf("  PWM wartość: %d (z zakresu 0-4095)\n", pwm_val);
    }
    else
    {
        printf("✗ BLAD: %s\n", esp_err_to_name(err));
    }
    printf("\n");
}

// Funkcja do testowania wszystkich kanałów
void test_all_servo_channels(pca9685_t *pca)
{
    printf("\n=== TEST WSZYSTKICH KANAŁÓW (0-15) ===\n");
    printf("Każdy kanał będzie ustawiony na 90°...\n\n");

    for (uint8_t ch = 0; ch < 16; ch++)
    {
        esp_err_t err = pca9685_set_servo_angle(pca, ch, 90.0f);
        printf("Kanał %2d: %s\n", ch, (err == ESP_OK) ? "✓ OK" : "✗ BLAD");
    }
    printf("\nJeśli widzisz wszystkie ✓ OK - problem jest gdzie indziej (zasilanie serw?).\n");
    printf("Jeśli widzisz ✗ BLAD - problem z I2C lub inicjalizacją PCA9685.\n\n");
}

void move_stepper(int steps, bool dir, int max_delay_us, int min_delay_us, int delay_change)
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

// --- STRUKTURY DANYCH DO KOMUNIKACJI ---
typedef struct
{
    uint8_t channel;
    float angle;
} servo_command_t;

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

// --- ZADANIE SERWOMECHANIZMÓW (Core 1) ---
void servo_task(void *pvParameters)
{
    servo_command_t servo_cmd_0;

    printf("\nRdzen 1: Serwomechanizmy zainicjalizowane na kanałach 0-15.\n");
    printf("Czasowe opóźnienie na starcie...\n");
    vTaskDelay(pdMS_TO_TICKS(500)); // Czekaj na inicjalizację PCA9685

    while (1)
    {
        // Czekaj na komendę serwomechanizmu
        if (xQueueReceive(servo_queue, &servo_cmd_0, portMAX_DELAY) == pdPASS)
        {
            // Waliduj kanał
            if (servo_cmd_0.channel >= 16)
            {
                printf("BLAD: Kanał spoza zakresu (0-15): %d\n", servo_cmd_0.channel);
                continue;
            }

            // Waliduj kąt
            if (servo_cmd_0.angle < 0.0f || servo_cmd_0.angle > 180.0f)
            {
                printf("BLAD: Kąt spoza zakresu (0-180): %.2f\n", servo_cmd_0.angle);
                continue;
            }

            // Ustaw kąt serwomechanizmu
            // esp_err_t err = pca9685_set_servo_angle(&pca9685, servo_cmd.channel, servo_cmd.angle);
            // esp_err_t err = soft_move_servo(&pca9685, servo_cmd.channel, servo_cmd.angle, 1500);
            /*
            if (err == ESP_OK)
            {
                printf("Kanał %d: Serwomechanizm ustawiony na %.2f°\n", servo_cmd.channel, servo_cmd.angle);
            }
            else
            {
                printf("BLAD przy ustawianiu serwomechanizmu na kanale %d: %s\n",
                       servo_cmd.channel, esp_err_to_name(err));
            }
            */

            float start_angle = current_servo_angle[servo_cmd_0.channel];
            int duration_ms = 1000 * (3 - 2 * cos(PI * (servo_cmd_0.angle - start_angle) / 180));
            int refresh_time_ms = 20;
            int steps = duration_ms / refresh_time_ms;

            for (int i = 0; i <= steps; i++)
            {
                float p = (float)i / (float)steps; // progres od 0 do 1
                // float move = (1.0f - cos(progress * PI)) / 2.0f;
                float move = 6.0f * p * p * p * p * p - 15.0f * p * p * p * p + 10.0f * p * p * p;
                float current_step_angle = start_angle + (servo_cmd_0.angle - start_angle) * move;
                pca9685_set_servo_angle(&pca9685, servo_cmd_0.channel, current_step_angle);
                vTaskDelay(pdMS_TO_TICKS(refresh_time_ms));
            }
            current_servo_angle[servo_cmd_0.channel] = servo_cmd_0.angle;
        }
    }
}
/*
esp_err_t soft_move_servo(pca9685_t *pca, uint8_t channel, float angle, int duration_ms)
{
    float start_angle = current_servo_angle[channel];
    int refresh_time_ms = 20;
    int steps = duration_ms / refresh_time_ms;
    if (angle > 180.0f)
        return false;
    for (int i = 0; i <= steps; i++)
    {
        float progress = (float)i / (float)steps; // progres od 0 do 1
        float move = (1.0f - cos(progress * PI)) / 2.0f;
        float current_step_angle = start_angle + (angle - start_angle) * move;
        pca9685_set_servo_angle(&pca, channel, current_step_angle);
        vTaskDelay(pdMS_TO_TICKS(refresh_time_ms));
    }
    current_servo_angle[channel] = angle;
}
*/
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

    printf("\nRdzen 0: UART gotowy. Dostepne komendy:\n");
    printf("  S <kat>       - Ustaw stepper (np. S 45)\n");
    printf("  SV <ch> <°>   - Ustaw serwomechanizm (np. SV 0 90)\n");
    printf("  SCAN          - Skanuj magistrale I2C\n");
    printf("  DEBUG         - Wyswietl status PCA9685\n");
    printf("  TEST <ch> <°> - Testuj konkretny kanal (np. TEST 0 90)\n");
    printf("  TESTALL       - Testuj wszystkie kanaly (0-15)\n");
    printf("> ");

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

                    // Próbujemy zrozumieć pełne zdanie
                    if (strncmp(rx_buf, "TESTALL", 7) == 0)
                    {
                        // Test wszystkich kanałów
                        test_all_servo_channels(&pca9685);
                        printf("> ");
                    }
                    else if (strncmp(rx_buf, "TEST", 4) == 0)
                    {
                        // Test konkretnego kanału: TEST <channel> <angle>
                        uint8_t channel = 0;
                        float angle = 0.0f;

                        if (sscanf(rx_buf, "TEST %hhu %f", &channel, &angle) == 2)
                        {
                            test_servo_channel(&pca9685, channel, angle);
                            printf("> ");
                        }
                        else
                        {
                            printf("Blad skladni. Uzyj: TEST <0-15> <0-180> (np. TEST 0 90)\n> ");
                        }
                    }
                    else if (strncmp(rx_buf, "DEBUG", 5) == 0)
                    {
                        // Debug status PCA9685
                        debug_pca9685_status(&pca9685);
                        printf("> ");
                    }
                    else if (strncmp(rx_buf, "SCAN", 4) == 0)
                    {
                        // Skanuj I2C
                        scan_i2c_devices(I2C_PORT);
                        printf("> ");
                    }
                    else if (strncmp(rx_buf, "SV", 2) == 0)
                    {
                        // Komenda Servo: SV <channel> <angle>
                        uint8_t channel = 0;
                        float angle = 0.0f;

                        if (sscanf(rx_buf, "SV %hhu %f", &channel, &angle) == 2)
                        {
                            servo_command_t servo_cmd = {.channel = channel, .angle = angle};
                            xQueueSend(servo_queue, &servo_cmd, 0);
                            printf("Wysłano do serwomechanizmu: kanał %d, kąt %.2f°\n> ", channel, angle);
                        }
                        else
                        {
                            printf("Blad skladni. Uzyj formatu: SV <0-15> <0-180> (np. SV 0 90)\n> ");
                        }
                    }
                    else if (strncmp(rx_buf, "SS", 2) == 0)
                    {
                        // Komenda Servo: SV <channel> <angle>
                        uint8_t channel[3] = {0};
                        float angle[3] = {0.0f};

                        if (sscanf(rx_buf, "SS %hhu %f %hhu %f %hhu %f", &channel[0], &angle[0], &channel[1], &angle[1], &channel[2], &angle[2]) == 6)
                        {
                            servo_command_t servo_cmd_0 = {.channel = channel[0], .angle = angle[0]};
                            servo_command_t servo_cmd_1 = {.channel = channel[1], .angle = angle[1]};
                            servo_command_t servo_cmd_2 = {.channel = channel[2], .angle = angle[2]};
                            xQueueSend(servo_queue, &servo_cmd_0, 0);
                            xQueueSend(servo_queue, &servo_cmd_1, 0);
                            xQueueSend(servo_queue, &servo_cmd_2, 0);

                            printf("Wysłano do wszystkich: kanał %d, kąt %.2f°\n> ", channel[0], angle[0]);
                            printf("->                     kanał %d, kąt %.2f°\n> ", channel[1], angle[1]);
                            printf("->                     kanał %d, kąt %.2f°\n> ", channel[2], angle[2]);
                        }
                        else
                        {
                            printf("Blad skladni. Uzyj formatu: SS <0-15> <0-180> <0-15> <0-180> <0-15> <0-180>\n> ");
                        }
                    }
                    else if (rx_buf[0] == 'S')
                    {
                        // Komenda Stepper: S <angle>
                        float wartosc = 0.0f;

                        if (sscanf(rx_buf, "S %f", &wartosc) == 1)
                        {
                            xQueueSend(stepper_queue, &wartosc, 0);
                            printf("Wysłano do silnika: %.2f\n> ", wartosc);
                        }
                        else
                        {
                            printf("Blad skladni. Uzyj formatu: S <kat> (np. S 45)\n> ");
                        }
                    }
                    else
                    {
                        printf("Nieznana komenda. Dostepne: S, SV, SCAN, DEBUG, TEST, TESTALL\n> ");
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

    // 1. Tworzymy kolejkę dla steppera
    stepper_queue = xQueueCreate(10, sizeof(float));

    // 2. Tworzymy kolejkę dla serwomechanizmów
    servo_queue = xQueueCreate(10, sizeof(servo_command_t));

    // Dodajemy małe zabezpieczenie inżynierskie
    if (stepper_queue == NULL || servo_queue == NULL)
    {
        printf("BLAD KRYTYCZNY: Nie udalo sie utworzyc kolejek w pamieci!\n");
        return; // Zatrzymujemy działanie, żeby uniknąć crasha
    }

    // 3. Inicjalizacja PCA9685
    printf("Inicjalizacja PCA9685...\n");
    esp_err_t err = pca9685_init(&pca9685, I2C_PORT, SDA_PIN, SCL_PIN);
    if (err != ESP_OK)
    {
        printf("BLAD KRYTYCZNY: Nie udalo sie zainicjalizowac PCA9685: %s\n", esp_err_to_name(err));
        printf("\nZACZYNAM DIAGNOSTYKE...\n");

        // Konfiguracja I2C dla skanowania
        i2c_config_t conf = {
            .mode = I2C_MODE_MASTER,
            .sda_io_num = SDA_PIN,
            .scl_io_num = SCL_PIN,
            .sda_pullup_en = GPIO_PULLUP_ENABLE,
            .scl_pullup_en = GPIO_PULLUP_ENABLE,
            .master.clk_speed = 400000,
        };
        i2c_param_config(I2C_PORT, &conf);
        i2c_driver_install(I2C_PORT, I2C_MODE_MASTER, 0, 0, 0);

        // Skanuj magistralę I2C
        scan_i2c_devices(I2C_PORT);

        printf("SPRAWDZ:\n");
        printf("  1. Czy PCA9685 jest zasilany (5V)?\n");
        printf("  2. Czy piny I2C (GPIO21/22) sa poprawnie polaczone?\n");
        printf("  3. Czy adres to 0x40 (sprawdz skocznikami)?\n");
        printf("\nWyprobuj komende SCAN w UART aby sprawdzic magistrale.\n");

        // Kontynuuj bez serw
        printf("\nKontynuujem bez serwomechanizmów...\n");
    }
    else
    {
        printf("PCA9685 zainicjalizowany pomyslnie!\n");

        // Diagnostyka przy starcie
        printf("\n--- DIAGNOSTYKA NA STARCIE ---\n");
        debug_pca9685_status(&pca9685);

        printf("Testuje kanal 0 na 90° (powinna byc srodkowa pozycja)...\n");
        test_servo_channel(&pca9685, 0, 90.0f);
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
        servo_task,
        "servo_task",
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