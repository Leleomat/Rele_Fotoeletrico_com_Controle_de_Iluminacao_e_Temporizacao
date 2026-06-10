#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/uart.h"

#define UART_ID uart0
#define BAUD_RATE 115200
#define UART_TX_PIN 0
#define UART_RX_PIN 1

// Variável de Controle (Configuradas via PC)
int threshold_acionamento = 50; // Inicial em 50%

// Variáveis de Status do Pico
bool lampada_acesa = false;
uint32_t horas_operacao_segundos = 0;
absolute_time_t tempo_ultimo_calculo;
absolute_time_t tempo_ultima_telemetria;

// Buffer para recepção de dados da UART
char rx_buffer[64];
int rx_index = 0;

void processar_comando(char *cmd) {
    int val1;
    if (sscanf(cmd, "SET_THR:%d", &val1) == 1) {
        threshold_acionamento = val1;
    }
}

void checar_uart() {
    while (uart_is_readable(UART_ID)) {
        char c = uart_getc(UART_ID);
        
        // Se encontrar fim de linha, processa o comando acumulado
        if (c == '\n' || c == '\r') {
            if (rx_index > 0) {
                rx_buffer[rx_index] = '\0';
                processar_comando(rx_buffer);
                rx_index = 0; // Reseta buffer
            }
        } else if (rx_index < sizeof(rx_buffer) - 1) {
            rx_buffer[rx_index++] = c;
        }
    }
}

int main() {
    // Inicializações
    stdio_init_all();
    sleep_ms(4000);

    uart_init(UART_ID, BAUD_RATE);
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);

    adc_init();
    adc_gpio_init(26);
    adc_select_input(0);

    uart_puts(UART_ID, "STATUS: Iniciando telemetria...\r\n");
    
    tempo_ultimo_calculo = get_absolute_time();
    tempo_ultima_telemetria = get_absolute_time();

    while (true) {
        // 1. Verifica comandos vindos do PC sem bloquear o loop
        checar_uart();

        // 2. Leitura física e lógica do sensor LDR
        uint16_t adc_bruto = adc_read();

        // ADC 4095 (3.3V) -> 0% de luminosidade ambiente
        // ADC 0 (0V) -> 100% de luminosidade ambiente
        float luminosidade_porcento = (1.0f - ((float)adc_bruto / 4095.0f)) * 100.0f;
        
        // Garante os limites por segurança matemática de ponto flutuante
        if (luminosidade_porcento < 0) luminosidade_porcento = 0;
        if (luminosidade_porcento > 100) luminosidade_porcento = 100;

        // Lógica de acionamento baseada no Threshold de luminosidade
        if (luminosidade_porcento <= threshold_acionamento) {
            lampada_acesa = true;
        } else {
            lampada_acesa = false;
        }

        // 3. Contabilização do tempo de operação em tempo real
        absolute_time_t agora = get_absolute_time();
        int64_t diff_ms = absolute_time_diff_us(tempo_ultimo_calculo, agora) / 1000;
        tempo_ultimo_calculo = agora;

        if (lampada_acesa) {
            // Acumula o tempo que ficou ligada continuamente
            static uint32_t acumulador_ms = 0;
            acumulador_ms += diff_ms;
            if (acumulador_ms >= 1000) {
                horas_operacao_segundos += (acumulador_ms / 1000);
                acumulador_ms %= 1000;
            }
        }

        // 4. Envio de Telemetria para o PC a cada 500ms
        if (absolute_time_diff_us(tempo_ultima_telemetria, agora) >= 500000) {
            tempo_ultima_telemetria = agora;
            char tx_buffer[80];

            // Define o brilho atual da lâmpada (100% se acesa, 0% se apagada)
            int brilho_lampada = lampada_acesa ? 100 : 0;

            snprintf(tx_buffer, sizeof(tx_buffer), "|LUM: %.1f|   |ST: %d|   |TIME: %lu|   |LUM_LAMP: %d|  |ADC Bruto: %d|\n",
                luminosidade_porcento,
                lampada_acesa ? 1 : 0,
                (unsigned long)horas_operacao_segundos,
                brilho_lampada,
                adc_bruto);
            uart_puts(UART_ID, tx_buffer);
        }

        // Pequena folga para estabilizar o consumo de processamento core, sem impactar UART
        sleep_us(1000);
    }
}