#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

/* Registra o módulo de log do Zephyr */
LOG_MODULE_REGISTER(race_report, LOG_LEVEL_INF);

/* Definição dos LEDs da FRDM-KL25Z via Devicetree */
#define LED_RED_NODE   DT_ALIAS(led0)
#define LED_GREEN_NODE DT_ALIAS(led1)
#define LED_BLUE_NODE  DT_ALIAS(led2)

static const struct gpio_dt_spec led_r = GPIO_DT_SPEC_GET(LED_RED_NODE, gpios);
static const struct gpio_dt_spec led_g = GPIO_DT_SPEC_GET(LED_GREEN_NODE, gpios);
static const struct gpio_dt_spec led_b = GPIO_DT_SPEC_GET(LED_BLUE_NODE, gpios);

#define STACK_SIZE 1024
#define PRIORITY_HIGH 5

K_THREAD_STACK_DEFINE(thread_a_stack, STACK_SIZE);
K_THREAD_STACK_DEFINE(thread_b_stack, STACK_SIZE);
struct k_thread thread_a_data;
struct k_thread thread_b_data;

/* Recursos compartilhados sem proteção (alvo das race conditions) */
volatile uint32_t shared_toggle_counter = 0;
volatile uint8_t shared_rgb_mask = 0;
volatile uint8_t active_test_case = 1;

/* Variáveis de instrumentação para geração do relatório */
volatile uint32_t expected_inc_a = 0;      // Contador privado da Thread A (CT-01)
volatile uint32_t expected_inc_b = 0;      // Contador privado da Thread B (CT-01)
volatile uint32_t ct02_overwrites = 0;     // Total de sobrescritas detectadas (CT-02)
volatile uint32_t ct03_collisions = 0;     // Total de colisões R+B detectadas (CT-03)
volatile uint8_t red_hw_state = 0;         // Espelho de estado do LED Vermelho (CT-03)
volatile uint8_t blue_hw_state = 0;        // Espelho de estado do LED Azul (CT-03)

void apply_led_mask(uint8_t mask) {
    gpio_pin_set_dt(&led_r, (mask & 0x01) ? 1 : 0);
    gpio_pin_set_dt(&led_g, (mask & 0x02) ? 1 : 0);
    gpio_pin_set_dt(&led_b, (mask & 0x04) ? 1 : 0);
}

/* =========================================================================
 * THREAD A
 * ========================================================================= */
void thread_a_entry(void *p1, void *p2, void *p3) {
    while (1) {
        if (active_test_case == 1) {
            uint32_t temp = shared_toggle_counter;
            k_busy_wait(50);
            shared_toggle_counter = temp + 1;
            expected_inc_a++;

            gpio_pin_set_dt(&led_g, (shared_toggle_counter % 2 == 0) ? 1 : 0);
            k_msleep(10);
        } 
        else if (active_test_case == 2) {
            uint8_t orig_mask = shared_rgb_mask;
            uint8_t local_mask = orig_mask | 0x01; // Liga Vermelho
            k_busy_wait(100);

            /* Verifica se a Thread B alterou a máscara durante a janela crítica */
            if (shared_rgb_mask != orig_mask) {
                ct02_overwrites++;
                if (ct02_overwrites <= 5) { // Limita spam no log, mas contabiliza todos
                    LOG_WRN("CT-02,EVENTO,OVERWRITE,t=%ums,thread=A,lido=0x%02X,atual=0x%02X,gravado=0x%02X",
                            (uint32_t)k_uptime_get(), orig_mask, shared_rgb_mask, local_mask);
                }
            }
            shared_rgb_mask = local_mask;
            apply_led_mask(shared_rgb_mask);
            k_msleep(200);

            orig_mask = shared_rgb_mask;
            local_mask = orig_mask & ~0x01; // Desliga Vermelho
            shared_rgb_mask = local_mask;
            apply_led_mask(shared_rgb_mask);
            k_msleep(200);
        }
        else if (active_test_case == 3) {
            gpio_pin_set_dt(&led_b, 0);
            blue_hw_state = 0;
            k_busy_wait(80);

            gpio_pin_set_dt(&led_r, 1);
            red_hw_state = 1;

            /* Detector de violação de exclusão mútua */
            if (red_hw_state && blue_hw_state) {
                ct03_collisions++;
                if (ct03_collisions <= 5) {
                    LOG_ERR("CT-03,EVENTO,COLISAO_RGB,t=%ums,thread=A,estado=MAGENTA_PROIBIDO",
                            (uint32_t)k_uptime_get());
                }
            }
            k_msleep(50);

            gpio_pin_set_dt(&led_r, 0);
            red_hw_state = 0;
            k_msleep(50);
        }
    }
}

/* =========================================================================
 * THREAD B
 * ========================================================================= */
void thread_b_entry(void *p1, void *p2, void *p3) {
    while (1) {
        if (active_test_case == 1) {
            uint32_t temp = shared_toggle_counter;
            k_busy_wait(50);
            shared_toggle_counter = temp + 1;
            expected_inc_b++;
            k_msleep(10);
        } 
        else if (active_test_case == 2) {
            uint8_t orig_mask = shared_rgb_mask;
            uint8_t local_mask = orig_mask | 0x04; // Liga Azul
            k_busy_wait(100);

            if (shared_rgb_mask != orig_mask) {
                ct02_overwrites++;
                if (ct02_overwrites <= 5) {
                    LOG_WRN("CT-02,EVENTO,OVERWRITE,t=%ums,thread=B,lido=0x%02X,atual=0x%02X,gravado=0x%02X",
                            (uint32_t)k_uptime_get(), orig_mask, shared_rgb_mask, local_mask);
                }
            }
            shared_rgb_mask = local_mask;
            apply_led_mask(shared_rgb_mask);
            k_msleep(150);

            orig_mask = shared_rgb_mask;
            local_mask = orig_mask & ~0x04; // Desliga Azul
            shared_rgb_mask = local_mask;
            apply_led_mask(shared_rgb_mask);
            k_msleep(150);
        }
        else if (active_test_case == 3) {
            gpio_pin_set_dt(&led_r, 0);
            red_hw_state = 0;
            k_busy_wait(80);

            gpio_pin_set_dt(&led_b, 1);
            blue_hw_state = 1;

            if (red_hw_state && blue_hw_state) {
                ct03_collisions++;
                if (ct03_collisions <= 5) {
                    LOG_ERR("CT-03,EVENTO,COLISAO_RGB,t=%ums,thread=B,estado=MAGENTA_PROIBIDO",
                            (uint32_t)k_uptime_get());
                }
            }
            k_msleep(40);

            gpio_pin_set_dt(&led_b, 0);
            blue_hw_state = 0;
            k_msleep(40);
        }
    }
}

/* =========================================================================
 * MAIN: Supervisor e Gerador de Relatório
 * ========================================================================= */
int main(void) {
    if (!gpio_is_ready_dt(&led_r) || !gpio_is_ready_dt(&led_g) || !gpio_is_ready_dt(&led_b)) {
        LOG_ERR("Falha critica: GPIOs dos LEDs indisponiveis.");
        return -1;
    }

    gpio_pin_configure_dt(&led_r, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&led_g, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&led_b, GPIO_OUTPUT_INACTIVE);

    LOG_INF("==========================================================");
    LOG_INF("RELATORIO DE EXECUCAO - FRDM-KL25Z (Zephyr Race Conditions)");
    LOG_INF("==========================================================");

    k_thread_create(&thread_a_data, thread_a_stack, K_THREAD_STACK_SIZEOF(thread_a_stack),
                    thread_a_entry, NULL, NULL, NULL,
                    PRIORITY_HIGH, 0, K_NO_WAIT);

    k_thread_create(&thread_b_data, thread_b_stack, K_THREAD_STACK_SIZEOF(thread_b_stack),
                    thread_b_entry, NULL, NULL, NULL,
                    PRIORITY_HIGH, 0, K_NO_WAIT);

    uint32_t ciclo = 1;

    while (1) {
        /* Reinicia métricas antes de iniciar o teste */
        shared_toggle_counter = 0;
        expected_inc_a = 0;
        expected_inc_b = 0;
        ct02_overwrites = 0;
        ct03_collisions = 0;
        shared_rgb_mask = 0;
        red_hw_state = 0;
        blue_hw_state = 0;
        apply_led_mask(0);

        LOG_INF("--- [CICLO %u] INICIO CT-0%d (t=%ums) ---",
                ciclo, active_test_case, (uint32_t)k_uptime_get());

        k_sleep(K_SECONDS(10));

        /* Emite resumo estruturado do teste recém-concluído */
        if (active_test_case == 1) {
            uint32_t esperado = expected_inc_a + expected_inc_b;
            uint32_t observado = shared_toggle_counter;
            uint32_t perdidos = (esperado > observado) ? (esperado - observado) : 0;

            LOG_INF("RESUMO,CT-01,esperado=%u,observado=%u,perdidos=%u,status=%s",
                    esperado, observado, perdidos,
                    (perdidos > 0) ? "FALHA_DETECTADA" : "OK");
        } 
        else if (active_test_case == 2) {
            LOG_INF("RESUMO,CT-02,sobrescritas=%u,mascara_final=0x%02X,status=%s",
                    ct02_overwrites, shared_rgb_mask,
                    (ct02_overwrites > 0) ? "FALHA_DETECTADA" : "OK");
        } 
        else if (active_test_case == 3) {
            LOG_INF("RESUMO,CT-03,colisoes_magenta=%u,status=%s",
                    ct03_collisions,
                    (ct03_collisions > 0) ? "FALHA_DETECTADA" : "OK");
        }

        active_test_case = (active_test_case % 3) + 1;
        if (active_test_case == 1) {
            ciclo++;
        }
    }
    return 0;
}