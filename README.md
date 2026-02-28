# 🤖 Robotic Arm (ESP32 & FreeRTOS)

Projekt zdalnie sterowanego ramienia robotycznego opartego na mikrokontrolerze **ESP32**, napisanego w czystym języku **C** przy użyciu środowiska **ESP-IDF (v5.3.1)** oraz systemu czasu rzeczywistego **FreeRTOS**. 

Projekt łączy precyzyjne sterowanie silnikiem krokowym, obsługę serwomechanizmów przez szynę I2C oraz architekturę wielordzeniową zapewniającą maksymalną wydajność i brak opóźnień (jittera) przy generowaniu sygnałów.

## ✨ Główne cechy projektu

* **Wielordzeniowość (Dual-Core):**
  * **Core 1:** Dedykowany wyłącznie do krytycznego czasowo sterowania silnikiem krokowym (zapewnia idealnie gładki ruch bez blokowania głównego programu, wyciągając prędkości do 700 µs/krok).
  * **Core 0:** Odpowiada za logikę, komunikację Wi-Fi (planowane UDP) oraz wysyłanie asynchronicznych komend I2C do sterownika serw.
* **Wydrukowana w 3D Przekładnia Cykloidalna:** Zastosowanie autorskiej przekładni o przełożeniu 20:1 (wykonanej z PLA, smarowanej smarem PTFE) znacząco zwiększającej moment obrotowy silnika krokowego.
* **Niskopoziomowa obsługa sprzętu:** Bezpośrednie sterowanie rejestrami i pinami (brak "ciężkich" bibliotek z Arduino), co gwarantuje mikrosekundową precyzję.

## 🛠️ Wykorzystany Sprzęt (Hardware)

* **Mikrokontroler:** ESP32 (WROOM-32)
* **Silnik główny (Baza):** Silnik krokowy NEMA 17 (42HS34-0404, 0.4A)
* **Sterownik silnika krokowego:** A4988 (VREF skalibrowane na ~0.28V)
* **Przeguby ramienia:** 4x Serwomechanizm MG996R
* **Sterownik serw:** Adafruit PCA9685 (16-kanałowy generator PWM na szynie I2C)
* **Zasilanie:** Dedykowany zasilacz 12V dla silnika krokowego oraz przetwornice step-down do zasilania serw (5-6V) i logiki.

## 💻 Środowisko Programistyczne (Software)

* **Framework:** ESP-IDF v5.3.1
* **Język:** C
* **OS:** FreeRTOS
* **Narzędzia:** Visual Studio Code + ESP-IDF Extension
