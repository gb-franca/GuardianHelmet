#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "lwip/tcp.h"
#include "hardware/i2c.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "ssd1306.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

// Definições de pinos e constantes
#define I2C_PORT i2c1
#define PINO_SCL 14
#define PINO_SDA 15
#define BUTTON_PIN 5  // Pino do botão (simulando o sensor de colisão)
#define BUZZER_PIN 21 // Pino do buzzer
#define LED_PIN 13    // Pino do LED
#define BUZZER_FREQUENCY 1000 // Frequência do buzzer em Hz
#define WIFI_SSID "b"  // Substitua pelo nome da sua rede Wi-Fi
#define WIFI_PASS "n" // Substitua pela senha da sua rede Wi-Fi

// Definições para ThingSpeak
#define THINGSPEAK_API_KEY "SOEFUER5JT1L2N0J"
#define THINGSPEAK_URL "api.thingspeak.com"
#define THINGSPEAK_FIELD "field1"  // Campo para registrar a colisão
#define THINGSPEAK_FIELD_LAT "field2"  // Campo para latitude
#define THINGSPEAK_FIELD_LON "field3"  // Campo para longitude

#define OPERATION_TIME_MS 5000 // Tempo de operação do buzzer e LED em milissegundos

ssd1306_t disp;
volatile bool button_pressed = false; // Flag para detectar pressionamento do botão

// Função de callback para interrupção do botão
void gpio_callback(uint gpio, uint32_t events) {
    if (gpio == BUTTON_PIN && (events & GPIO_IRQ_EDGE_FALL)) {
        button_pressed = true;
    }
}

// Função para inicializar o PWM no pino do buzzer
void pwm_init_buzzer(uint pin) {
    gpio_set_function(pin, GPIO_FUNC_PWM);
    uint slice_num = pwm_gpio_to_slice_num(pin);
    pwm_config config = pwm_get_default_config();
    pwm_config_set_clkdiv(&config, clock_get_hz(clk_sys) / (BUZZER_FREQUENCY * 4096)); // Divisor de clock
    pwm_init(slice_num, &config, true);
    pwm_set_gpio_level(pin, 0); // Iniciar o PWM no nível baixo
}

// Função para emitir um beep indefinidamente
void beep_indefinido(uint pin) {
    uint slice_num = pwm_gpio_to_slice_num(pin);
    pwm_set_gpio_level(pin, 2048); // Configurar o duty cycle para 50% (ativo)
}

// Função para parar o beep
void parar_beep(uint pin) {
    uint slice_num = pwm_gpio_to_slice_num(pin);
    pwm_set_gpio_level(pin, 0); // Configurar o duty cycle para 0% (inativo)
}

// Função para inicializar o display
void inicializa_display() {
    i2c_init(I2C_PORT, 400 * 1000);
    gpio_set_function(PINO_SCL, GPIO_FUNC_I2C);
    gpio_set_function(PINO_SDA, GPIO_FUNC_I2C);
    gpio_pull_up(PINO_SCL);
    gpio_pull_up(PINO_SDA);
    disp.external_vcc = false;
    ssd1306_init(&disp, 128, 64, 0x3C, I2C_PORT);
}

// Função para imprimir texto no display
void print_texto(const char *msg) {
    ssd1306_clear(&disp);
    char buffer[128];
    strncpy(buffer, msg, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    char *line = strtok(buffer, "\n");
    int y = 0;
    while (line != NULL) {
        ssd1306_draw_string(&disp, 0, y, 1, line);
        y += 10; // Ajuste a altura da linha conforme necessário
        line = strtok(NULL, "\n");
    }

    ssd1306_show(&disp);
}

// Função para enviar dados para o ThingSpeak
void send_data_to_thingspeak(int collision_detected, float latitude, float longitude) {
    char request[300];
    snprintf(request, sizeof(request),
             "GET /update?api_key=%s&%s=%d&%s=%.6f&%s=%.6f HTTP/1.1\r\n"
             "Host: %s\r\n"
             "Connection: close\r\n\r\n",
             THINGSPEAK_API_KEY, THINGSPEAK_FIELD, collision_detected, THINGSPEAK_FIELD_LAT, latitude, THINGSPEAK_FIELD_LON, longitude, THINGSPEAK_URL);

    printf("Enviando dados para ThingSpeak: %s\n", request); // Log para depuração

    struct tcp_pcb *pcb = tcp_new();
    struct ip4_addr server_ip;
    ip4addr_aton("184.106.153.149", &server_ip);

    if (tcp_connect(pcb, &server_ip, 80, NULL) == ERR_OK) {
        tcp_write(pcb, request, strlen(request), TCP_WRITE_FLAG_COPY);
    }
}

// Função para simular a obtenção de coordenadas GPS
void get_gps_coordinates(float *latitude, float *longitude) {
    *latitude = -23.550520 + ((float)(rand() % 1000) / 100000.0); // São Paulo, Brasil
    *longitude = -46.633308 + ((float)(rand() % 1000) / 100000.0);
    printf("Coordenadas simuladas: Latitude = %.6f, Longitude = %.6f\n", *latitude, *longitude); // Log para depuração
}

// Função para monitorar o botão
void monitor_button() {
    static bool last_state = false;
    bool current_state = !gpio_get(BUTTON_PIN);

    if (current_state != last_state) {
        last_state = current_state;
        if (current_state) {
            float latitude, longitude;
            get_gps_coordinates(&latitude, &longitude);
            print_texto("Colisao detectada!\nEnviando Resgate!");
            send_data_to_thingspeak(1, latitude, longitude);
            beep_indefinido(BUZZER_PIN); // Emite um beep indefinidamente

            // Acender o LED e iniciar o piscar
            gpio_put(LED_PIN, 1);
            uint32_t start_time = to_ms_since_boot(get_absolute_time());
            while (to_ms_since_boot(get_absolute_time()) - start_time < OPERATION_TIME_MS) {
                gpio_put(LED_PIN, 1);
                sleep_ms(500);
                gpio_put(LED_PIN, 0);
                sleep_ms(500);
            }

            // Parar o beep e apagar o LED após o tempo de operação
            parar_beep(BUZZER_PIN);
            gpio_put(LED_PIN, 0);
        } else {
            print_texto("Pressione o botao A.\nPara simular uma\ncolisao.");
            send_data_to_thingspeak(0, 0.0, 0.0);
            parar_beep(BUZZER_PIN); // Para o beep

            // Apagar o LED
            gpio_put(LED_PIN, 0);
        }
    }
}

int main() {
    stdio_init_all();
    sleep_ms(5000);
    printf("Inicializando Wi-Fi...\n");

    if (cyw43_arch_init()) {
        printf("Erro ao inicializar Wi-Fi\n");
        return 1;
    }

    cyw43_arch_enable_sta_mode();
    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASS, CYW43_AUTH_WPA2_AES_PSK, 10000)) {
        printf("Falha ao conectar no Wi-Fi\n");
        return 1;
    }

    printf("Wi-Fi conectado!\n");

    // Configuração do botão
    gpio_init(BUTTON_PIN);
    gpio_set_dir(BUTTON_PIN, GPIO_IN);
    gpio_pull_up(BUTTON_PIN);
    gpio_set_irq_enabled_with_callback(BUTTON_PIN, GPIO_IRQ_EDGE_FALL, true, &gpio_callback);

    // Inicializar o display
    inicializa_display();
    print_texto("Pressione o botao A.\nPara simular uma\ncolisao.");

    // Configuração do buzzer
    gpio_init(BUZZER_PIN);
    gpio_set_dir(BUZZER_PIN, GPIO_OUT);
    pwm_init_buzzer(BUZZER_PIN);

    // Configuração do LED
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    gpio_put(LED_PIN, 0); // Inicialmente desligado

    // Inicializar o gerador de números aleatórios
    srand(time(NULL));

    while (true) {
        monitor_button();
        sleep_ms(500);
    }

    cyw43_arch_deinit();
    return 0;
}
