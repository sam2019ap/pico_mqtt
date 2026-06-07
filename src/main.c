/**
 * @file main.c
 * @brief Firmware MQTT para Raspberry Pi Pico W
 *
 * Funcionalidades:
 *  - Conecta ao Wi-Fi e a um broker MQTT
 *  - Ao pressionar o botão BOOTSEL, publica a temperatura do núcleo da CPU
 *    no tópico /samuel_araujo/210025640/temperatura
 *  - Assina o tópico /samuel_araujo/210025640/led e controla o piscar do LED integrado
 *    conforme o intervalo (em segundos) recebido via MQTT
 *
 * Hardware: Raspberry Pi Pico W
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"       // Driver Wi-Fi do Pico W
#include "hardware/adc.h"          // Leitura do ADC (temperatura interna)

#include "lwip/apps/mqtt.h"        // Stack MQTT via lwIP
#include "lwip/ip_addr.h"
#include "lwip/dns.h"

#include "config.h"                // Credenciais e configurações (ver config.h)

/* ─── Constantes de pinos ──────────────────────────────────────────────── */
// No Pico W o LED integrado é controlado pelo chip CYW43 (não via GPIO direto)
// O botão BOOTSEL é lido via gpio_get no pino 0 no modo de usuário
// (Nota: BOOTSEL compartilha o pino de flash; usamos a técnica com ADC desabilitado)

/* ─── Estado global ────────────────────────────────────────────────────── */
static mqtt_client_t *mqtt_client = NULL;

// Controle do temporizador do LED
static volatile int32_t led_interval_ms = 0;  // 0 = LED desligado
static absolute_time_t last_led_toggle;
static bool led_state = false;

// Debounce do botão
static absolute_time_t last_button_press;
#define DEBOUNCE_MS 300

/* ─── Protótipos ───────────────────────────────────────────────────────── */
static float read_cpu_temperature(void);
static void publish_temperature(void);
static void mqtt_connect(void);
static void mqtt_connection_cb(mqtt_client_t *client, void *arg, mqtt_connection_status_t status);
static void mqtt_subscribe_cb(void *arg, err_t err);
static void mqtt_incoming_data_cb(void *arg, const u8_t *data, u16_t len, u8_t flags);
static void mqtt_incoming_publish_cb(void *arg, const char *topic, u32_t tot_len);
static void process_led_command(const char *payload, uint16_t len);
static bool read_bootsel_button(void);

/* ═══════════════════════════════════════════════════════════════════════════
 * Leitura da temperatura interna do RP2040
 * O sensor interno está no canal ADC4. A fórmula de conversão é fornecida
 * pelo datasheet: T = 27 - (Vbe - 0.706) / 0.001721
 * ═══════════════════════════════════════════════════════════════════════════ */
static float read_cpu_temperature(void) {
    // Seleciona o canal do sensor de temperatura (canal 4)
    adc_select_input(4);

    // Lê o valor bruto de 12 bits e converte para tensão (referência de 3.3 V)
    uint16_t raw = adc_read();
    float voltage = raw * 3.3f / (1 << 12);

    // Fórmula do datasheet do RP2040
    float temperature = 27.0f - (voltage - 0.706f) / 0.001721f;
    return temperature;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Publicação da temperatura no tópico MQTT
 * ═══════════════════════════════════════════════════════════════════════════ */
static void publish_temperature(void) {
    if (mqtt_client == NULL || !mqtt_client_is_connected(mqtt_client)) {
        printf("[MQTT] Cliente não conectado, ignorando publicação.\n");
        return;
    }

    float temp = read_cpu_temperature();

    // Formata a mensagem com 2 casas decimais
    char payload[32];
    snprintf(payload, sizeof(payload), "%.2f", temp);

    err_t err = mqtt_publish(
        mqtt_client,
        MQTT_TOPIC_TEMP,   // tópico definido em config.h
        payload,
        strlen(payload),
        0,                 // QoS 0 (at most once)
        0,                 // retain = false
        NULL,              // callback de confirmação (não usado aqui)
        NULL
    );

    if (err == ERR_OK) {
        printf("[MQTT] Temperatura publicada: %s °C no tópico %s\n", payload, MQTT_TOPIC_TEMP);
    } else {
        printf("[MQTT] Erro ao publicar temperatura: %d\n", err);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Callback chamado ao receber dados de uma mensagem subscrita
 * Os dados podem chegar em múltiplos fragmentos; aqui tratamos em buffer.
 * ═══════════════════════════════════════════════════════════════════════════ */
static char incoming_payload[128];
static uint16_t incoming_len = 0;

static void mqtt_incoming_publish_cb(void *arg, const char *topic, u32_t tot_len) {
    // Início de uma nova mensagem recebida
    printf("[MQTT] Mensagem recebida no tópico: %s (tamanho total: %lu)\n", topic, tot_len);
    incoming_len = 0;  // Reinicia o buffer
    memset(incoming_payload, 0, sizeof(incoming_payload));
}

static void mqtt_incoming_data_cb(void *arg, const u8_t *data, u16_t len, u8_t flags) {
    // Acumula os dados recebidos (pode vir em fragmentos)
    if (incoming_len + len < sizeof(incoming_payload) - 1) {
        memcpy(incoming_payload + incoming_len, data, len);
        incoming_len += len;
    }

    // MQTT_DATA_FLAG_LAST indica que este é o último (ou único) fragmento
    if (flags & MQTT_DATA_FLAG_LAST) {
        incoming_payload[incoming_len] = '\0';  // Garante terminação da string
        printf("[MQTT] Payload completo recebido: \"%s\"\n", incoming_payload);
        process_led_command(incoming_payload, incoming_len);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Processa o comando recebido no tópico do LED
 *
 * Regras:
 *  - Número inteiro positivo → define o intervalo de piscar em segundos
 *  - "0" ou valor inválido   → desliga o LED e para o temporizador
 * ═══════════════════════════════════════════════════════════════════════════ */
static void process_led_command(const char *payload, uint16_t len) {
    if (len == 0) {
        printf("[LED] Payload vazio, desligando LED.\n");
        led_interval_ms = 0;
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);  // Garante LED apagado
        led_state = false;
        return;
    }

    // Verifica se todos os caracteres são dígitos (aceita inteiro não-negativo)
    bool all_digits = true;
    for (uint16_t i = 0; i < len; i++) {
        if (!isdigit((unsigned char)payload[i])) {
            all_digits = false;
            break;
        }
    }

    if (!all_digits) {
        printf("[LED] Payload inválido (\"%s\"), desligando LED.\n", payload);
        led_interval_ms = 0;
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
        led_state = false;
        return;
    }

    // Converte para inteiro
    int32_t value = atoi(payload);

    if (value <= 0) {
        // Valor 0 (ou negativo por overflow): desliga LED
        printf("[LED] Valor 0 recebido, desligando LED.\n");
        led_interval_ms = 0;
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
        led_state = false;
    } else {
        // Converte segundos para milissegundos e inicia temporizador
        led_interval_ms = value * 1000;
        last_led_toggle = get_absolute_time();
        printf("[LED] Intervalo definido para %d segundo(s) (%d ms).\n", value, led_interval_ms);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Callback de subscribe — chamado após a confirmação de inscrição no tópico
 * ═══════════════════════════════════════════════════════════════════════════ */
static void mqtt_subscribe_cb(void *arg, err_t err) {
    if (err == ERR_OK) {
        printf("[MQTT] Inscrito com sucesso no tópico %s\n", MQTT_TOPIC_LED);
    } else {
        printf("[MQTT] Erro ao inscrever no tópico: %d\n", err);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Callback de conexão MQTT
 * Chamado quando a conexão com o broker é estabelecida ou falha
 * ═══════════════════════════════════════════════════════════════════════════ */
static void mqtt_connection_cb(mqtt_client_t *client, void *arg, mqtt_connection_status_t status) {
    if (status == MQTT_CONNECT_ACCEPTED) {
        printf("[MQTT] Conectado ao broker!\n");

        // Registra os callbacks para mensagens recebidas
        mqtt_set_inpub_callback(client, mqtt_incoming_publish_cb, mqtt_incoming_data_cb, NULL);

        // Assina o tópico de controle do LED (QoS 0)
        err_t err = mqtt_subscribe(client, MQTT_TOPIC_LED, 0, mqtt_subscribe_cb, NULL);
        if (err != ERR_OK) {
            printf("[MQTT] Falha ao enviar pedido de inscrição: %d\n", err);
        }
    } else {
        printf("[MQTT] Falha na conexão MQTT, status: %d\n", status);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Inicia a conexão com o broker MQTT
 * ═══════════════════════════════════════════════════════════════════════════ */
static void mqtt_connect(void) {
    mqtt_client = mqtt_client_new();
    if (mqtt_client == NULL) {
        printf("[MQTT] Falha ao criar cliente MQTT.\n");
        return;
    }

    struct mqtt_connect_client_info_t ci = {
        .client_id   = MQTT_CLIENT_ID,
        .client_user = MQTT_USER,      // NULL se não usar autenticação
        .client_pass = MQTT_PASS,      // NULL se não usar autenticação
        .keep_alive  = 60,             // Keep-alive em segundos
        .will_topic  = NULL,           // Sem Last Will
        .will_msg    = NULL,
        .will_qos    = 0,
        .will_retain = 0,
    };

    ip_addr_t broker_ip;
    // Converte o endereço IP do broker de string para struct ip_addr_t
    if (!ip4addr_aton(MQTT_BROKER_IP, &broker_ip)) {
        printf("[MQTT] Endereço IP do broker inválido: %s\n", MQTT_BROKER_IP);
        return;
    }

    printf("[MQTT] Conectando ao broker %s:%d...\n", MQTT_BROKER_IP, MQTT_BROKER_PORT);

    err_t err = mqtt_client_connect(
        mqtt_client,
        &broker_ip,
        MQTT_BROKER_PORT,
        mqtt_connection_cb,
        NULL,
        &ci
    );

    if (err != ERR_OK) {
        printf("[MQTT] mqtt_client_connect retornou erro: %d\n", err);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Leitura do botão BOOTSEL (pino GP0 compartilhado)
 *
 * No Pico W em modo de usuário, o BOOTSEL pode ser lido como ADC ou GPIO,
 * mas a forma mais simples é usar a função pico_bootsel do SDK.
 * Aqui usamos um GPIO externo como alternativa mais confiável,
 * configurado em config.h como BUTTON_PIN (padrão: 15).
 * ═══════════════════════════════════════════════════════════════════════════ */
static bool read_bootsel_button(void) {
    // Botão ativo em nível baixo (pull-up interno habilitado)
    return !gpio_get(BUTTON_PIN);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * MAIN
 * ═══════════════════════════════════════════════════════════════════════════ */
int main(void) {
    // Inicializa stdio (USB/UART para debug)
    stdio_init_all();
    sleep_ms(2000);  // Aguarda estabilização e abertura do terminal serial

    printf("\n=== Firmware MQTT - Raspberry Pi Pico W ===\n");
    printf("Tópico pub : %s\n", MQTT_TOPIC_TEMP);
    printf("Tópico sub : %s\n", MQTT_TOPIC_LED);
    printf("Broker     : %s:%d\n\n", MQTT_BROKER_IP, MQTT_BROKER_PORT);

    /* ── Inicializa ADC para leitura de temperatura ── */
    adc_init();
    adc_set_temp_sensor_enabled(true);  // Habilita sensor interno de temperatura

    /* ── Configura botão com pull-up interno ── */
    gpio_init(BUTTON_PIN);
    gpio_set_dir(BUTTON_PIN, GPIO_IN);
    gpio_pull_up(BUTTON_PIN);

    /* ── Inicializa o chip Wi-Fi CYW43 ── */
    if (cyw43_arch_init()) {
        printf("[Wi-Fi] Falha ao inicializar o chip CYW43.\n");
        return 1;
    }

    // Habilita modo estação (STA = conectar a um AP existente)
    cyw43_arch_enable_sta_mode();

    /* ── Conecta ao Wi-Fi ── */
    printf("[Wi-Fi] Conectando à rede \"%s\"...\n", WIFI_SSID);
    int wifi_result = cyw43_arch_wifi_connect_timeout_ms(
        WIFI_SSID,
        WIFI_PASSWORD,
        CYW43_AUTH_WPA2_AES_PSK,
        30000  // Timeout de 30 segundos
    );

    if (wifi_result != 0) {
        printf("[Wi-Fi] Falha na conexão (código %d). Verifique SSID e senha.\n", wifi_result);
        cyw43_arch_deinit();
        return 1;
    }
    printf("[Wi-Fi] Conectado! IP: %s\n",
           ip4addr_ntoa(netif_ip4_addr(netif_list)));

    /* ── Inicia conexão MQTT ── */
    mqtt_connect();

    /* ── Inicializa controle de tempo ── */
    last_button_press = get_absolute_time();
    last_led_toggle   = get_absolute_time();

    printf("[Sistema] Entrando no loop principal...\n");

    /* ═══ Loop principal ═══════════════════════════════════════════════════ */
    while (true) {
        // Necessário para manter a stack lwIP e Wi-Fi funcionando
        cyw43_arch_poll();

        /* ── Verifica botão com debounce ── */
        if (read_bootsel_button()) {
            absolute_time_t now = get_absolute_time();
            int64_t elapsed = absolute_time_diff_us(last_button_press, now) / 1000;

            if (elapsed > DEBOUNCE_MS) {
                last_button_press = now;
                printf("[Botão] Pressionado! Publicando temperatura...\n");
                publish_temperature();

                // Aguarda soltar o botão para evitar múltiplos disparos
                while (read_bootsel_button()) {
                    cyw43_arch_poll();
                    sleep_ms(10);
                }
            }
        }

        /* ── Controla piscar do LED ── */
        if (led_interval_ms > 0) {
            absolute_time_t now = get_absolute_time();
            int64_t elapsed_led = absolute_time_diff_us(last_led_toggle, now) / 1000;

            if (elapsed_led >= led_interval_ms) {
                led_state = !led_state;
                cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led_state ? 1 : 0);
                last_led_toggle = now;
            }
        }

        sleep_ms(10);  // Cede CPU brevemente para evitar busy-wait excessivo
    }

    // Nunca chega aqui em operação normal
    cyw43_arch_deinit();
    return 0;
}
