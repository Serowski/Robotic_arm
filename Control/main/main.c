#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h" // Biblioteka do precyzyjnych opóźnień mikrosekundowych

// --- KONFIGURACJA PINÓW ---
#define STEP_PIN GPIO_NUM_25
#define DIR_PIN  GPIO_NUM_26

// --- PARAMETRY ---
// Maksymalna liczba kroków w jedną stronę (200 kroków na 360 deg silnika * 20 obroków wału)
#define MAX_STEPS 4000
// Maksymalne prędkości i przyspieszenia (w us)
#define MAX_DELAY_US 2500
#define MIN_DELAY_US 1300
#define DELAY_CHANGE 4


long map(long x, long in_min, long in_max, long out_min, long out_max) {
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

void move_stepper(int steps, int dir, int max_delay_us, int min_delay_us, int delay_change) {
    // Ustawienie kierunku
    gpio_set_level(DIR_PIN, dir);

    // Obliczenia: ile kroków zajmie nam pełne rozpędzenie się do min_delay_us?
    int steps_to_full_speed = (max_delay_us - min_delay_us) / delay_change;

    // Zabezpieczenie krótkiego ruchu
    int accel_steps = steps_to_full_speed;
    if (steps / 2 < steps_to_full_speed) {
        accel_steps = steps / 2;
    }

    int current_delay = max_delay_us;

    // Główna pętla ruchu
    for (int i = 0; i < steps; i++) {
        
        // --- Rozpędzanie (Zmniejszamy delay) ---
        if (i < accel_steps) {
            current_delay -= delay_change;
        }
        // --- Hamowanie (Zwiększamy delay) ---
        else if (i >= steps - accel_steps) {
            current_delay += delay_change;
        }
        
        // Zabezpieczenie przed błędami matematycznymi
        if (current_delay < min_delay_us) current_delay = min_delay_us;

        // --- Impuls na silnik ---
        gpio_set_level(STEP_PIN, 1);
        esp_rom_delay_us(10); 
        gpio_set_level(STEP_PIN, 0);

        esp_rom_delay_us(current_delay - 10);

        // --- Ochrona Watchdoga ---
        if (i % 1000 == 0) {
            vTaskDelay(pdMS_TO_TICKS(1)); // Dajemy systemowi 1 ms oddechu co 1000 kroków
        }
    }
}


void stepper_task(void *pvParameters) {

    // Inicjalizacja pinów
    gpio_reset_pin(STEP_PIN);
    gpio_reset_pin(DIR_PIN);
    gpio_set_direction(STEP_PIN, GPIO_MODE_OUTPUT);
    gpio_set_direction(DIR_PIN, GPIO_MODE_OUTPUT);

    int current_abs_pos = 0; // Aktualna pozycja w krokach (0 do MAX_STEPS/2)
    bool direction = true; // true = w prawo, false = w lewo
    int deg_to_steps = 0; 
    int move_steps = 0;
    float received_degree = 0.0f; 

    // --- SYMULACJA ODBIERANIA DANYCH ---
    bool test = true; 

    //  Główna pętla ruchu
    while (1) {
        // Przełożenie przekładni 1:20 => 1 obrót silnika = 1/20 obrotu całości
        // 4000 kroków na obrót 360 => 1 krok = 0.09 stopniad
        // Od -180 do +180 stopni => -2000 do +2000 kroków
        received_degree = 180.0f * (test ? 1 : -1); 
        
        if (received_degree > 180.0f || received_degree < -180.0f) {
            printf("Otrzymany kąt %.2f jest poza zakresem! Oczekiwany zakres: -180 do 180 stopni.\n", received_degree);
            received_degree = 0.0f; // Reset do bezpiecznej pozycji
            continue; 
        }

        deg_to_steps = received_degree / 0.09f;
        move_steps = (int)(deg_to_steps - current_abs_pos); 

        if (move_steps > 0) {
            direction = true; // Kierunek w prawo
            gpio_set_level(DIR_PIN, 1);
        } else {
            direction = false; // Kierunek w lewo
            move_steps = -move_steps; 
            gpio_set_level(DIR_PIN, 0);
        }
        
        move_stepper(move_steps, direction, MAX_DELAY_US, MIN_DELAY_US, DELAY_CHANGE);
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        current_abs_pos += move_steps * (direction ? 1 : -1); 

        test = !test; 

        
    }
}

void app_main(void) {
    printf("System uruchomiony.\n");

    xTaskCreatePinnedToCore(
        stepper_task, 
        "stepper_task", 
        4096, 
        NULL, 
        5, 
        NULL, 
        1  // Nr rdzenia do zadania
    );

    while (1) {
        // Rdzeń 0 co sekundę potwierdza, że układ się nie zawiesił
        printf("Rdzen 0 dziala... silnik kreci sie na Rdzeniu 1.\n");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}