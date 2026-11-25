#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>

/* Definição dos LEDs da FRDM-KL25Z via Devicetree */
#define LED_RED_NODE   DT_ALIAS(led0)
#define LED_GREEN_NODE DT_ALIAS(led1)
#define LED_BLUE_NODE  DT_ALIAS(led2)

static const struct gpio_dt_spec led_r = GPIO_DT_SPEC_GET(LED_RED_NODE, gpios);
static const struct gpio_dt_spec led_g = GPIO_DT_SPEC_GET(LED_GREEN_NODE, gpios);
static const struct gpio_dt_spec led_b = GPIO_DT_SPEC_GET(LED_BLUE_NODE, gpios);

/* Configurações das Threads */
#define STACK_SIZE 1024
#define PRIORITY_HIGH 5
#define PRIORITY_LOW  7

K_THREAD_STACK_DEFINE(thread_a_stack, STACK_SIZE);
K_THREAD_STACK_DEFINE(thread_b_stack, STACK_SIZE);
struct k_thread thread_a_data;
struct k_thread thread_b_data;

/* * RECURSOS COMPARTILHADOS SEM PROTEÇÃO (Proposital para Race Condition)
 * 'volatile' evita otimização do compilador, mas NÃO garante atomicidade.
 */
volatile uint32_t shared_toggle_counter = 0;
volatile uint8_t shared_rgb_mask = 0; // Bit 0: R, Bit 1: G, Bit 2: B
volatile uint8_t active_test_case = 1;

/* Função auxiliar para aplicar a máscara aos pinos físicos */
void apply_led_mask(uint8_t mask) {
    gpio_pin_set_dt(&led_r, (mask & 0x01) ? 1 : 0);
    gpio_pin_set_dt(&led_g, (mask & 0x02) ? 1 : 0);
    gpio_pin_set_dt(&led_b, (mask & 0x04) ? 1 : 0);
}

/* =========================================================================
 * THREAD A: Produtora / Modificadora de Estado (Prioridade Mais Alta)
 * ========================================================================= */
void thread_a_entry(void *p1, void *p2, void *p3) {
    while (1) {
        if (active_test_case == 1) {
            /* CASO 1: Operação Read-Modify-Write não atômica no contador */
            uint32_t temp = shared_toggle_counter;
            k_busy_wait(50); // Janela crítica artificial para forçar preempção/intercalação
            shared_toggle_counter = temp + 1;

            if (shared_toggle_counter % 2 == 0) {
                gpio_pin_set_dt(&led_g, 1); // Deveria acender apenas em pares
            } else {
                gpio_pin_set_dt(&led_g, 0);
            }
            k_msleep(10);
        } 
        else if (active_test_case == 2) {
            /* CASO 2: Corrupção de Máscara de Bits (LED Vermelho) */
            uint8_t local_mask = shared_rgb_mask;
            local_mask |= 0x01; // Liga bit do Vermelho
            k_busy_wait(100);   // Simula processamento antes da escrita
            shared_rgb_mask = local_mask; // Sobrescreve alterações da Thread B!
            apply_led_mask(shared_rgb_mask);
            k_msleep(200);

            local_mask = shared_rgb_mask;
            local_mask &= ~0x01; // Desliga bit do Vermelho
            shared_rgb_mask = local_mask;
            apply_led_mask(shared_rgb_mask);
            k_msleep(200);
        }
        else if (active_test_case == 3) {
            /* CASO 3: Violação de Exclusão Mútua (R e B nunca deveriam acender juntos) */
            gpio_pin_set_dt(&led_b, 0); // Garante Azul apagado
            k_busy_wait(80);             // Ponto de vulnerabilidade
            gpio_pin_set_dt(&led_r, 1); // Acende Vermelho
            k_msleep(50);
            gpio_pin_set_dt(&led_r, 0);
            k_msleep(50);
        }
    }
}

/* =========================================================================
 * THREAD B: Consumidora / Concorrente (Prioridade Mais Baixa ou Igual)
 * ========================================================================= */
void thread_b_entry(void *p1, void *p2, void *p3) {
    while (1) {
        if (active_test_case == 1) {
            /* CASO 1: Incremento concorrente no mesmo contador */
            uint32_t temp = shared_toggle_counter;
            k_busy_wait(50);
            shared_toggle_counter = temp + 1;
            k_msleep(10);
        } 
        else if (active_test_case == 2) {
            /* CASO 2: Corrupção de Máscara de Bits (LED Azul) */
            uint8_t local_mask = shared_rgb_mask;
            local_mask |= 0x04; // Liga bit do Azul
            k_busy_wait(100);
            shared_rgb_mask = local_mask; // Sobrescreve alterações da Thread A!
            apply_led_mask(shared_rgb_mask);
            k_msleep(150);

            local_mask = shared_rgb_mask;
            local_mask &= ~0x04; // Desliga bit do Azul
            shared_rgb_mask = local_mask;
            apply_led_mask(shared_rgb_mask);
            k_msleep(150);
        }
        else if (active_test_case == 3) {
            /* CASO 3: Tentativa de acionar Azul exclusivamente */
            gpio_pin_set_dt(&led_r, 0); // Garante Vermelho apagado
            k_busy_wait(80);
            gpio_pin_set_dt(&led_b, 1); // Acende Azul
            k_msleep(40);
            gpio_pin_set_dt(&led_b, 0);
            k_msleep(40);
        }
    }
}

int main(void) {
    if (!gpio_is_ready_dt(&led_r) || !gpio_is_ready_dt(&led_g) || !gpio_is_ready_dt(&led_b)) {
        printk("Erro: Dispositivos GPIO dos LEDs nao prontos.\n");
        return -1;
    }

    gpio_pin_configure_dt(&led_r, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&led_g, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&led_b, GPIO_OUTPUT_INACTIVE);

    printk("=== Iniciando Simulacao de Race Conditions na FRDM-KL25Z ===\n");

    /* Inicializa as threads com a MESMA prioridade (5) para permitir Time-Slicing / Intercalação */
    k_thread_create(&thread_a_data, thread_a_stack, K_THREAD_STACK_SIZEOF(thread_a_stack),
                    thread_a_entry, NULL, NULL, NULL,
                    PRIORITY_HIGH, 0, K_NO_WAIT);

    k_thread_create(&thread_b_data, thread_b_stack, K_THREAD_STACK_SIZEOF(thread_b_stack),
                    thread_b_entry, NULL, NULL, NULL,
                    PRIORITY_HIGH, 0, K_NO_WAIT);

    /* Loop supervisor que alterna os casos de teste a cada 10 segundos e imprime diagnósticos */
    while (1) {
        printk("\n--- Executando Caso de Teste %d ---\n", active_test_case);
        shared_toggle_counter = 0;
        shared_rgb_mask = 0;
        apply_led_mask(0);

        k_sleep(K_SECONDS(10));

        if (active_test_case == 1) {
            printk("[Diagnostico CT-01] Valor final contador: %u (Esperado ~2000 sem perdas)\n", 
                   shared_toggle_counter);
        } else if (active_test_case == 2) {
            printk("[Diagnostico CT-02] Mascara final: 0x%02X (Verifique flashes perdidos no R/B)\n", 
                   shared_rgb_mask);
        }

        active_test_case = (active_test_case % 3) + 1;
    }
    return 0;
}