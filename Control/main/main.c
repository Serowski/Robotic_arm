#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h" // Biblioteka do precyzyjnych opóźnień mikrosekundowych

// --- KONFIGURACJA PINÓW ---
#define STEP_PIN GPIO_NUM_25
#define DIR_PIN  GPIO_NUM_26

// --- PARAMETRY ---
// Czast trwania jednego kroku w us (max. speed)
#define STEP_DELAY_US 1200 
// Maksymalna liczba kroków w jedną stronę (200 kroków na 360 deg silnika * 20 obroków wału)
#define MAX_STEPS 4000



void stepper_task(void *pvParameters) {

    // 1. Inicjalizacja pinów
    gpio_reset_pin(STEP_PIN);
    gpio_reset_pin(DIR_PIN);
    gpio_set_direction(STEP_PIN, GPIO_MODE_OUTPUT);
    gpio_set_direction(DIR_PIN, GPIO_MODE_OUTPUT);

    // 2. Ustalenie kierunku
    gpio_set_level(DIR_PIN, 1); 

    printf("Stepper Task: Start na Core 1. Predkosc: %d us\n", STEP_DELAY_US);

    // Licznik kroków do ochrony przed watchdogiem
    int step_counter = 0;

    // 3. Główna pętla ruchu
    while (1) {
        // Przełożenie przekładni 1:20 => 1 obrót silnika = 1/20 obrotu całości
        // 4000 kroków na obrót 360 => 1 krok = 0.09 stopnia
        float received_degree = 90.0f; 
        int deg_to_steps = received_degree / 0.09f;

        for (int i = 0; i < deg_to_steps; i++) { 
            // --- Generowanie impulsu ---
            

            gpio_set_level(STEP_PIN, 1);
            // Delay aby step był widoczny (minimalny czas trwania impulsu)
            esp_rom_delay_us(10);
            gpio_set_level(STEP_PIN, 0);
            // Odczekujemy resztę czasu trwania kroku
            esp_rom_delay_us(STEP_DELAY_US - 10);
        }

        // --- OCHRONA PRZED WATCHDOGIEM ---
        // Co 1000 kroków dajemy systemowi milisekundę na oddech (aby nie wywołać watchdog timer)
        step_counter++;
        if (step_counter >= 1000) { 
            vTaskDelay(pdMS_TO_TICKS(1)); 
            step_counter = 0;
        }
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