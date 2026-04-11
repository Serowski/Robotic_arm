# Dokumentacja Modułu WiFi-TCP dla ESP32

## Spis treści
1. [Przegląd architektury](#przegląd-architektury)
2. [Procedura inicjalizacji WiFi](#procedura-inicjalizacji-wifi)
3. [Procedura połączenia TCP](#procedura-połączenia-tcp)
4. [Procedura odbierania danych](#procedura-odbierania-danych)
5. [Procedura parsowania JSON](#procedura-parsowania-json)
6. [Wysyłanie danych do silników](#wysyłanie-danych-do-silników)
7. [Używane konstrukcje](#używane-konstrukcje)
8. [Możliwe problemy i rozwiązania](#możliwe-problemy-i-rozwiązania)

---

## Przegląd architektury

### Diagram przepływu danych

```
┌─────────────────┐
│ Komputer (PC)   │
│  Python Server  │
│  Port 5000      │
└────────┬────────┘
         │
         │ JSON over TCP
         │
         ▼
┌─────────────────────────────────────────────────┐
│          ESP32 - Rdzeń 0 (Netif)                │
│                                                 │
│  ┌──────────────────────────────────────────┐  │
│  │ wifi_tcp_task() - główne zadanie TCP     │  │
│  │ - Odbiera dane z sieci                   │  │
│  │ - Parsuje JSON                           │  │
│  │ - Wysyła do kolejek                      │  │
│  └───────────────┬──────────────────────────┘  │
│                  │                             │
│      ┌───────────┴──────────┐                  │
│      ▼                      ▼                  │
│  [stepper_queue]      [servo_queue]           │
│                                                 │
└─────────────────────────────────────────────────┘
         │                    │
         │ FreeRTOS Queue     │ FreeRTOS Queue
         │                    │
         ▼                    ▼
┌─────────────────┐  ┌─────────────────┐
│  Rdzeń 1        │  │  Rdzeń 1        │
│ stepper_task()  │  │ servo_task()    │
│ - PCA9685 I2C   │  │ - PCA9685 I2C   │
│ - PWM           │  │ - Smooth easing │
└──────┬──────────┘  └──────┬──────────┘
       │                    │
       ▼                    ▼
   [Stepper]           [Servo Motors]
   (Silnik)            (Serwomechanizmy)
```

---

## Procedura inicjalizacji WiFi

### Funkcja: `esp_err_t wifi_init(void)`

**Lokalizacja:** `wifi_TCP.c:61-131`

### Kroki inicjalizacji:

#### 1. Inicjalizacja NVS (Non-Volatile Storage)
```c
esp_err_t ret = nvs_flash_init();
if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
{
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
}
```
- **Cel:** Przechowywanie konfiguracji WiFi w pamięci Flash
- **Czyszczenie:** Jeśli pamięć jest pełna lub nowa wersja, wymazywane są dane
- **Rezultat:** Gotowa pamięć NVS do zapisywania danych WiFi

#### 2. Tworzenie Event Group
```c
wifi_event_group = xEventGroupCreate();
```
- **FreeRTOS Event Group:** Synchronizacja między zadaniami
- **Bit WIFI_CONNECTED_BIT:** Flaga że WiFi jest podłączony
- **Użycie:** `wifi_tcp_task()` czeka aż ten bit się ustawić

#### 3. Inicjalizacja stosu sieciowego (LwIP)
```c
ESP_ERROR_CHECK(esp_netif_init());
ESP_ERROR_CHECK(esp_event_loop_create_default());
esp_netif_create_default_wifi_sta();
```
- **esp_netif_init():** Inicjalizacja interfejsu sieciowego
- **esp_event_loop:** Tworzenie pętli zdarzeń dla systemowych callback'ów
- **wifi_sta:** Postawienie karty WiFi w trybie stacji (STA) - łączy się z routerem

#### 4. Konfiguracja WiFi
```c
wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
ESP_ERROR_CHECK(esp_wifi_init(&cfg));
```
- **Domyślna konfiguracja:** Ustawienia rekomendowane przez ESP-IDF
- **Inicjalizacja sterownika WiFi:** Przygotowanie mikrokodu WiFi 802.11

#### 5. Rejestracja Event Handlera
```c
ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, 
                                                    &wifi_event_handler, NULL, NULL));
```
- **Obsługuje zdarzenia:**
  - `WIFI_EVENT_STA_START` - WiFi się rusza
  - `WIFI_EVENT_STA_DISCONNECTED` - Utraciło połączenie
  - `IP_EVENT_STA_GOT_IP` - Otrzymało IP od routera

#### 6. Ustawianie SSID i hasła
```c
wifi_config_t wifi_config = {
    .sta = {
        .ssid = WIFI_SSID,           // "MASZT_2.4G_TEST_RADOM"
        .password = WIFI_PASSWORD,   // "@5JzunB3Q8A8"
        .threshold.authmode = WIFI_AUTH_WPA2_PSK,
    },
};
```
- **Dane z `wifi_TCP.h`** - tym się różni każdy projekt
- **Autentykacja WPA2:** Bezpieczne szyfrowanie

#### 7. Uruchomienie WiFi
```c
ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
ESP_ERROR_CHECK(esp_wifi_start());
```
- **Ustawienie trybu:** STA (Station) = klient
- **Wgranie konfiguracji:** SSID i hasło
- **Start:** Sterownik zaczyna szukać sieci

#### 8. Czekanie na IP
```c
xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, false, true, 
                    10000 / portTICK_PERIOD_MS);
```
- **Czeka do 10 sekund** aż ESP otrzyma IP
- **Event Group:** Blokuje zadanie do momentu zdarzenia `IP_EVENT_STA_GOT_IP`
- **Timeout:** Jeśli nie ma IP przez 10s, zwraca `ESP_ERR_TIMEOUT`

### Event Handler - Obsługa zdarzeń WiFi

```c
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
```

**Obsługuje 3 zdarzenia:**

1. **WIFI_EVENT_STA_START** (linia 38-41)
   - Wywoływane gdy sterownik WiFi się inicjalizuje
   ```c
   esp_wifi_connect();  // Polecenie: połącz się z siecią
   ```

2. **WIFI_EVENT_STA_DISCONNECTED** (linia 43-50)
   - Sieć się przecięła (utrata sygnału, zmiana hasła, itp.)
   ```c
   tcp_connected = false;  // Resetuj flagi
   xEventGroupClearBits(...);  // Wyczyść flagi event group
   esp_wifi_connect();  // Próbuj ponownie
   ```

3. **IP_EVENT_STA_GOT_IP** (linia 51-57)
   - Otrzymano IP od routera - gotowe do komunikacji!
   ```c
   printf("[WiFi] Uzyskano IP: " IPSTR "\n", IP2STR(&event->ip_info.ip));
   wifi_connected = true;
   xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
   ```

---

## Procedura połączenia TCP

### Funkcja: `esp_err_t wifi_tcp_connect(void)`

**Lokalizacja:** `wifi_TCP.c:134-176`

### Krok po kroku:

#### 1. Sprawdzenie czy socket już otwarty
```c
if (sock != -1)
{
    printf("[TCP] Socket już otwarty\n");
    return ESP_OK;
}
```
- **Socket zmienna:** `static int sock = -1`
- **-1 oznacza:** socket zamknięty/neutworzony
- **Zapobieganie:** Unikamy duplikowania socketów

#### 2. Przygotowanie adresu serwera
```c
struct sockaddr_in dest_addr;
dest_addr.sin_addr.s_addr = inet_addr(SERVER_HOST);    // "172.16.69.255"
dest_addr.sin_family = AF_INET;                        // IPv4
dest_addr.sin_port = htons(SERVER_PORT);               // 5000 z `wifi_TCP.h`
```

**Struktura sockaddr_in:**
- sin_addr.s_addr: IP serwera (konwersja string → liczba)
- sin_family: AF_INET = IPv4 (nie IPv6)
- sin_port: **htons()** =Host To Network Short (big-endian konwersja)

#### 3. Tworzenie socketu
```c
sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
if (sock < 0)
{
    printf("[TCP] BŁĄD: Nie można utworzyć socketu\n");
    return ESP_FAIL;
}
```

**Parametry socket():**
- `AF_INET`: Adres Family IPv4
- `SOCK_STREAM`: TCP (niezawodność, kolejność)
- `IPPROTO_IP`: Domyślny protokół

**Zwracane wartości:**
- `> 0`: Deskryptor socketu (liczba)
- `< 0`: Błąd

#### 4. Ustawienie timeoutu
```c
struct timeval timeout;
timeout.tv_sec = 5;      // 5 sekund
timeout.tv_usec = 0;     // + 0 mikrosekund
setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
```

**Cel timeoutu:** 
- Jeśli dane nie przychodzą przez 5 sekund, `recv()` zwraca błąd
- Bez timeoutu: zadanie by się zawiesiło na każdy `recv()`

#### 5. Połączenie TCP
```c
int err = connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
if (err != 0)
{
    printf("[TCP] BŁĄD: Nie można się połączyć - %d\n", err);
    close(sock);
    sock = -1;
    return ESP_FAIL;
}
```

**Co się dzieje:**
- **Three-Way Handshake:** SYN → SYN-ACK → ACK
- Jeśli serwer nie czeka na porcie 5000: zwraca błąd
- Po błędzie: czyszczenie zasobów (`close()` i `sock = -1`)

#### 6. Ustawienie flagi połączenia
```c
tcp_connected = true;
printf("[TCP] ✓ Połączono z serwerem\n");
return ESP_OK;
```

---

## Procedura odbierania danych

### Funkcja: `int tcp_receive(uint8_t *data, int max_len)`

**Lokalizacja:** `wifi_TCP.c:199-227`

### Realizacja BLOCKING receive z timeoutem:

```c
int len = recv(sock, data, max_len - 1, 0);
```

**recv() parametry:**
- `sock`: Numer socketu
- `data`: Bufor do umieszczenia danych
- `max_len - 1`: Rezerwowanie miejsca na '\0'
- `0`: Flagi (brak specjalnych opcji)

### Obsługa warunków zwrotu:

#### Błąd z kodem EAGAIN/EWOULDBLOCK
```c
if (errno == EAGAIN || errno == EWOULDBLOCK)
{
    return 0;  // Timeout - brak danych; nie jest to błąd
}
```
- **Znaczenie:** Timeout (5 sekund bez danych)
- **Powracamy:** 0 = brak danych, ale połączenie OK

#### Błąd rzeczywisty
```c
printf("[TCP] BŁĄD odbierania\n");
tcp_connected = false;
return -1;
```
- Sieć popsuta, socket nieprawidłowy, itp.

#### Serwer zamknął połączenie
```c
if (len == 0)
{
    printf("[TCP] Serwer zamknął połączenie\n");
    tcp_connected = false;
    return -1;
}
```
- **len == 0:** Serwer wysłał EOF (End Of File)
- Normalny koniec - rekonekcja w głównym tasku

#### Dane odebrane
```c
return len;  // Liczba odebranych bajtów
```

---

## Procedura wysyłania danych

### Funkcja: `int tcp_send(const uint8_t *data, int len)`

**Lokalizacja:** `wifi_TCP.c:179-196`

### Implementacja:

```c
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
```

**Zwracane wartości:**
- `> 0`: Liczba wysłanych bajtów
- `-1`: Błąd

**Użycie:**
```c
const char *ack = "{\"status\":\"ok\"}\n";
tcp_send((const uint8_t *)ack, strlen(ack));
```

---

## Procedura odbierania danych - Główne zadanie

### Funkcja: `void wifi_tcp_task(void *pvParameters)`

**Lokalizacja:** `wifi_TCP.c:339-409`

### Architektura pętli:

```
START
  ↓
Czekaj na WiFi (Event Group)
  ↓
┌─────────────────────────────────┐
│ GŁÓWNA PĘTLA (nieskończona)     │
│                                 │
│ ┌─ TCP NIEPODŁĄCZONY ─┐         │
│ │ Próbuj się połączyć │         │
│ │ Delay 3 sekundy    │         │
│ │ Retry count++      │         │
│ └────────────────────┘         │
│           │                     │
│ ┌─ TCP PODŁĄCZONY ──┐           │
│ │ len = recv()      │           │
│ │ Jeśli len > 0:    │           │
│ │  - Parsuj JSON    │           │
│ │  - Wyślij ACK     │           │
│ └────────────────────┘          │
│           │                     │
│ Delay 100ms                     │
│           │                     │
└────────────────────────┘        │
           ↑                      │
           └──────────────────────┘
```

### Szczegółowo:

#### 1. Inicjalizacja
```c
uint8_t rx_buffer[TCP_RX_BUFFER];  // 512 bajtów
int retry_count = 0;
const int max_retries = 5;
```

#### 2. Czekanie na WiFi
```c
xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, false, true, portMAX_DELAY);
```
- Blokuje zadanie do momentu gdy `WIFI_CONNECTED_BIT` jest ustawiony
- `false` = nie czyszcz bitu po czekaniu
- `true` = czekaj aż BIT będzie set (nie clear)

#### 3. Główna pętla - Brak połączenia
```c
if (!tcp_connected)
{
    if (retry_count < max_retries)  // max_retries = 5
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
            retry_count = 0;  // Reset licznika na sukces
        }
    }
    else
    {
        printf("[TCP] Zbyt wiele nieudanych prób. Czekanie 10 sekund...\n");
        vTaskDelay(pdMS_TO_TICKS(10000));  // Większy delay
        retry_count = 0;
    }
```

**Logika:**
- Prób 1-5 co 3 sekundy
- Po 5 nieudanych próbach czeka 10 sekund
- Zapobiega spamowaniu serwera

#### 4. Główna pętla - Połączenie OK
```c
int len = tcp_receive(rx_buffer, TCP_RX_BUFFER);

if (len > 0)
{
    rx_buffer[len] = '\0';  // Null-terminate string
    printf("[TCP] Odebrano %d bajtów: %s\n", len, (const char *)rx_buffer);
    
    parse_json_command((const char *)rx_buffer);
    
    const char *ack = "{\"status\":\"ok\"}\n";
    tcp_send((const uint8_t *)ack, strlen(ack));
}
```

**Przebieg:**
1. Odbierz dane
2. Zamknij string null-terminator
3. Wydrukuj na konsolę
4. Parsuj JSON
5. Wyślij potwierdzenie

#### 5. Błąd połączenia
```c
else if (len < 0)
{
    tcp_disconnect();
    retry_count = 0;
}
```

#### 6. Główny delay
```c
vTaskDelay(pdMS_TO_TICKS(100));  // 100ms
```
- Zapobieganie busy-wait
- Pozwala innym zadaniom biec

---

## Procedura parsowania JSON

### Funkcja: `static void parse_json_command(const char *json_str)`

**Lokalizacja:** `wifi_TCP.c:245-336`

### Format JSON:

**Stepper:**
```json
{"type":"stepper","angle":45.5}
{"type": "stepper", "angle": 45.5}  // Z spacjami - też ok
```

**Servo:**
```json
{"type":"servo","channel":0,"angle":90}
{"type": "servo", "channel": 0, "angle": 90.0}
```

**Ping:**
```json
{"type":"ping"}
```

### Parsowanie - Krok po kroku:

#### 1. Inicjalizacja zmiennych
```c
char type[20] = {0};       // Rodzaj komendy: "servo" lub "stepper"
float angle = 0.0f;        // Kąt: 0.0 - 180.0 dla servo, -180 do 180 dla stepper
uint8_t channel = 0;       // Numer kanału: 0-15

printf("[TCP] Parsowanie: %s\n", json_str);
```

#### 2. Ekstraktowanie "type"
```c
char *ptr = strstr(json_str, "\"type\"");  // Szukaj ciągu "type"
if (ptr != NULL)
{
    // Próba 1: Z spacjami: "type" : "servo"
    sscanf(ptr, "\"type\" : \"%19[^\"]\"", type);
    
    // Jeśli puste, próba 2: Bez spacji: "type":"servo"
    if (strlen(type) == 0)
    {
        sscanf(ptr, "\"type\":\"%19[^\"]\"", type);
    }
    printf("[TCP] DEBUG: type='%s'\n", type);
}
```

**Wyjaśnienie sscanf:**
- `pointer` - gdzie czytać
- `"\"type\" : \"%19[^\"]\""`
  - `\"` - literalny cudzysłów
  - `type` - szukany string
  - ` : ` - literals ze spacjami
  - `%19[^\"]` - czytaj do 19 znaków aż do cudzysłowu (nie zawiera go)
  - `19 = rozmiar bufora - 1` (one for null terminator)

#### 3. Ekstraktowanie "angle"
```c
ptr = strstr(json_str, "\"angle\"");
if (ptr != NULL)
{
    if (sscanf(ptr, "\"angle\" : %f", &angle) != 1)
    {
        sscanf(ptr, "\"angle\":%f", &angle);
    }
    printf("[TCP] DEBUG: angle=%.2f\n", angle);
}
```

**sscanf zwraca liczbę pomyślnie sparsowanych pól:**
- `== 1`: Udało się parsować jedną liczbę
- `!= 1`: Nie ma zmiennej lub zły format

#### 4. Ekstraktowanie "channel"
```c
ptr = strstr(json_str, "\"channel\"");
if (ptr != NULL)
{
    if (sscanf(ptr, "\"channel\" : %hhu", &channel) != 1)
    {
        sscanf(ptr, "\"channel\":%hhu", &channel);
    }
    printf("[TCP] DEBUG: channel=%d\n", channel);
}
```

**%hhu** = unsigned char (0-255)

---

## Wysyłanie danych do silników

### Obsługa komendy "stepper"

```c
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
```

**Procedura:**
1. Sprawdzenie czy to "stepper"
2. Debug: Wydrukowanie kąta
3. Walidacja: `stepper_queue != NULL`
4. Wysłanie do kolejki: `xQueueSend()`
   - 3. parametr = 0 = nie czekaj jeśli pełna
   - Zwraca `pdPASS` jeśli OK
5. Debug output

**Przekazane dane:**
- Typ: `float angle`
- Wartość: z JSON-a (np. 45.5)

### Obsługa komendy "servo"

```c
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
```

**Procedura:**
1. Sprawdzenie czy to "servo"
2. Debug: Wydrukowanie kanału i kąta
3. Tworzenie struktury `servo_command_t`
4. Wysłanie do kolejki
5. Debug output

**Struktura servo_command_t:**
```c
typedef struct {
    uint8_t channel;  // 0-15
    float angle;      // 0-180
} servo_command_t;
```

### Obsługa komendy "ping"

```c
else if (strcmp(type, "ping") == 0)
{
    printf("[TCP] Ping otrzymany\n");
}
```

Tylko debug output - brak wysyłania do kolejek.

---

## Używane konstrukcje

### FreeRTOS Components

#### 1. **QueueHandle_t** - Kolejka

```c
extern QueueHandle_t stepper_queue;
extern QueueHandle_t servo_queue;
```

**Inicjalizacja (w main.c):**
```c
stepper_queue = xQueueCreate(10, sizeof(float));
servo_queue = xQueueCreate(10, sizeof(servo_command_t));
```

**Parametry:**
- 10 = pojemność kolejki (10 elementów)
- sizeof(...) = rozmiar każdego elementu

**Operacje:**
```c
xQueueSend(stepper_queue, &angle, 0);  // Wyślij do kolejki
xQueueReceive(stepper_queue, &received_degree, portMAX_DELAY);  // Odbierz
```

#### 2. **EventGroupHandle_t** - Synchronizacja

```c
static EventGroupHandle_t wifi_event_group = NULL;
static const int WIFI_CONNECTED_BIT = BIT0;
```

**Operacje:**
```c
xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);    // Ustaw bit
xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);  // Wyczyść bit
xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, false, true, timeout);
```

**Sens:** Synchronizacja między zadaniami bez busy-wait

#### 3. **vTaskDelay()** - Opóźnienie

```c
vTaskDelay(pdMS_TO_TICKS(100));  // 100 ms
vTaskDelay(pdMS_TO_TICKS(3000)); // 3 sekundy
```

**Konwersja:** FreeRTOS wewnętrznie używa ticks - konwersjez `pdMS_TO_TICKS()`

**Ważne:** Inne zadania mogą działać w czasie opóźnienia

### Sockets (BSD API - lwip)

#### Socket creation
```c
sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
```

#### Connection
```c
connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
```

#### Send/Receive
```c
send(sock, data, len, 0);      // Wysłanie
recv(sock, data, max_len, 0);  // Odbieranie
```

#### Socket options
```c
setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
```

#### Closing
```c
close(sock);
sock = -1;
```

### String Functions

#### strlen() - Długość stringa
```c
strlen("hello");  // 5
```

#### strcmp() - Porównanie stringów
```c
if (strcmp(type, "servo") == 0)  // Równe
```

#### strstr() - Wyszukiwanie podciągu
```c
char *ptr = strstr(json_str, "\"type\"");
if (ptr != NULL)  // Znaleziono
```

#### sscanf() - Parser stringów

```c
sscanf(ptr, "\"angle\":%f", &angle);           // Parsuj liczbę
sscanf(ptr, "\"type\":\"%19[^\"]\"", type);    // Parsuj string
```

**Format specifiers:**
- `%f` = float
- `%hhu` = unsigned char (0-255)
- `%19[^\"]` = do 19 znaków, aż do `"`

---

## Możliwe problemy i rozwiązania

### Problem 1: ESP się nie łączy z WiFi

**Symptomy:**
```
[WiFi] Startowanie stacji WiFi...
[WiFi] Timeout połączenia WiFi
```

**Przyczyny:**
1. ❌ Złe SSID/hasło w `wifi_TCP.h`
2. ❌ Router emituje tylko 5 GHz (ESP32 obsługuje tylko 2.4 GHz)
3. ❌ MAC adres ESP na blackliście routera
4. ❌ Zbyt daleko od routera

**Rozwiązanie:**
```c
// wifi_TCP.h
#define WIFI_SSID       "NAZWA_SIECI"     // Dokładnie ta sama
#define WIFI_PASSWORD   "HASLO"           // Dokładnie to samo
```
- Sprawdzić w ustawieniach routera jaki kanał
- Sprawdzić czy 2.4 GHz jest włączony

---

### Problem 2: WiFi OK, ale TCP się nie łączy

**Symptomy:**
```
[WiFi] ✓ Podłączony do sieci WiFi
[TCP] Łączenie do serwera: 192.168.1.100:5000
[TCP] Próba połączenia 1/5 za 3 sekundy...
```

**Przyczyny:**
1. ❌ Serwer Python nie biegnie
2. ❌ Złe IP serwera w `wifi_TCP.h`
3. ❌ Firewall blokuje port 5000
4. ❌ ESP i komputer nie w tej samej sieci

**Rozwiązanie:**
```bash
# Sprawdzić IP komputera
ipconfig | findstr "IPv4"

# Sprawdzić czy serwer słucha
netstat -an | findstr ":5000"

# Uruchomić serwer Python
python server.py
```

**Zmiana IP w `wifi_TCP.h`:**
```c
#define SERVER_HOST     "192.168.1.105"  // Dokładnie IP komputera
#define SERVER_PORT     5000
```

---

### Problem 3: TCP połączony, ale dane nie przechodzą do silników

**Symptomy:**
```
[TCP] ✓ Połączono z serwerem
[TCP] Odebrano 47 bajtów: {"type": "servo", "channel": 0, "angle": 90.0}
[TCP] DEBUG: type=''  ← PUSTY!
```

**Przyczyny:**
1. ❌ Parser JSON nie działa (format inny niż oczekiwany)
2. ❌ Kolejka = NULL (nie została utworzona)
3. ❌ Kolejka pełna (silniki nie przetwarzają)

**Debugowanie:**
```
Sprawdzić monitor seriowy:
[TCP] DEBUG: type='servo'      ← OK
[TCP] DEBUG: channel=0         ← OK
[TCP] DEBUG: angle=90.00       ← OK
[TCP] ✓ Servo zadanie wysłane  ← OK
```

Jeśli `type=''` - format JSON zły. Sprawdzić spacje i cudzysłowy.

---

### Problem 4: Dane parsowane, ale silniki się nie ruszają

**Symptomy:**
```
[TCP] ✓ Servo zadanie wysłane
```
Ale servo nie się rusza fizycznie.

**Przyczyny:**
1. ❌ Servo nie zasilane (brak 5V)
2. ❌ servo_task() nie wysyła do PCA9685
3. ❌ Przewody I2C przerwane
4. ❌ PCA9685 nie zainicjalizowany na starcie

**Debugowanie:**
```
Monitor seriowy na starcie powinien pokazać:
PCA9685 zainicjalizowany pomyslnie!
Testuje kanal 0 na 90°...
✓ SUKCES - Kanał 0 powinien się ruszać na 90.00°
```

---

### Problem 5: Timeout połączenia (recv timeout)

**Symptomy:**
```
[TCP] Łączenie do serwera: 192.168.1.100:5000
(czeka 5 sekund)
[TCP] BŁĄD: Nie można się połączyć - -1
```

**Przyczyna:**
- Serwer nie odpowiada w ciągu 5 sekund

**Timeout jest ustawiony na:**
```c
struct timeval timeout;
timeout.tv_sec = 5;      // 5 sekund
timeout.tv_usec = 0;
setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
```

---

### Problem 6: Bufor w JSON nie obsługuje spacji

**Symptomy:**
```
Serwer wysyła: {"type": "servo", "channel": 0}  ← Z spacjami
Monitor: [TCP] DEBUG: type=''  ← Nie parsuje!
```

**Rozwiązanie:**
Parser ma dwa warianty:
```c
// Wariant 1: Z spacjami
sscanf(ptr, "\"type\" : \"%19[^\"]\"", type);

// Wariant 2: Bez spacji
if (strlen(type) == 0)
{
    sscanf(ptr, "\"type\":\"%19[^\"]\"", type);
}
```

Obie wersje powinny działać.

---

### Problem 7: Memory leak na requestach

**Symptomy:**
```
Po kilku godzinach system się zawieszył
```

**Przyczyna:**
- Nie zamykanie socketów prawidłowo
- Memory w kolejkach nie odczyszczane

**Zapobieganie:**
- Przywracanie `sock = -1` po `close()`
- FreeRTOS queue automatycznie czyszczą

---

### Problem 8: Kolizja numerów portów

**Symptomy:**
```
[TCP] BŁĄD: Nie można utworzyć socketu
```

**Przyczyna:**
- Port 5000 już zajęty (inny proces)

**Rozwiązanie:**
```bash
# Sprawdzić co słucha na porcie 5000
netstat -ano | findstr ":5000"

# Zmienić port w wifi_TCP.h
#define SERVER_PORT     5001  // Inny port
```

---

## Tablica stanów połączenia

| Stan | Flagi | Możliwe akcje | Co się dzieje |
|------|-------|---------------|--------------|
| **Boot** | wifi=F, tcp=F | Czekanie | Inicjalizacja |
| **WiFi Connecting** | wifi=F, tcp=F | Czekanie | SSID/hasło problemy |
| **WiFi Connected** | wifi=T, tcp=F | TCP Connect attempt | TCP timeout/connection |
| **TCP Connecting** | wifi=T, tcp=F | Retry | Backoff exponential |
| **TCP Connected** | wifi=T, tcp=T | Send/Receive | Normal operation |
| **WiFi Lost** | wifi=F, tcp=F | Reconnect WiFi | Ponowna inicjalizacja |
| **TCP Lost** | wifi=T, tcp=F | TCP Reconnect | Backoff retry |

---

## Podsumowanie przepływu całkowitego

```
1. START ESP32
   ↓
2. wifi_init()
   └─ Inicjalizacja NVS, event handlers, WiFi
   └─ Czekanie na IP
   ↓
3. wifi_tcp_task() startuje (Rdzeń 0)
   ├─ Czekanie na WIFI_CONNECTED_BIT
   ├─ wifi_tcp_connect() - połączenie do serwera
   └─ Główna pętla:
       ├─ recv() → JSON string
       ├─ parse_json_command()
       │  ├─ sscanf() → type, angle, channel
       │  └─ xQueueSend() → stepper/servo_queue
       ├─ send() potwierdzenie ACK
       └─ Delay 100ms
   ↓
4. stepper_task() (Rdzeń 1)
   └─ xQueueReceive() ← angle z WiFi
   └─ Ruch silnika stepper
   ↓
5. servo_task() (Rdzeń 1)
   └─ xQueueReceive() ← servo_command_t z WiFi
   └─ PCA9685 PWM na serwomechanizmach
   ↓
6. Urządzenia się ruszają! ✓
```

---

## Referencje do kodu

| Funkcja | Plik | Linia |
|---------|------|-------|
| wifi_init | wifi_TCP.c | 61-131 |
| wifi_event_handler | wifi_TCP.c | 35-58 |
| wifi_tcp_connect | wifi_TCP.c | 134-176 |
| tcp_send | wifi_TCP.c | 179-196 |
| tcp_receive | wifi_TCP.c | 199-227 |
| tcp_disconnect | wifi_TCP.c | 230-239 |
| parse_json_command | wifi_TCP.c | 245-336 |
| wifi_tcp_task | wifi_TCP.c | 339-409 |

---

**Koniec dokumentacji**
