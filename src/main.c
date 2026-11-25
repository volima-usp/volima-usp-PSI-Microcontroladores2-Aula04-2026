#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(race_report, LOG_LEVEL_INF);

/* Mapeamento conforme o DTS da sua FRDM-KL25Z */
#define LED_GREEN_NODE DT_ALIAS(led0)
#define LED_BLUE_NODE  DT_ALIAS(led1)
#define LED_RED_NODE   DT_ALIAS(led2)

static const struct gpio_dt_spec led_g = GPIO_DT_SPEC_GET(LED_GREEN_NODE, gpios);
static const struct gpio_dt_spec led_b = GPIO_DT_SPEC_GET(LED_BLUE_NODE, gpios);
static const struct gpio_dt_spec led_r = GPIO_DT_SPEC_GET(LED_RED_NODE, gpios);

#define STACK_SIZE 1024
#define PRIORITY_HIGH 5

K_THREAD_STACK_DEFINE(thread_a_stack, STACK_SIZE);
K_THREAD_STACK_DEFINE(thread_b_stack, STACK_SIZE);
struct k_thread thread_a_data;
struct k_thread thread_b_data;

/* Mutexes dedicados para cada recurso compartilhado */
K_MUTEX_DEFINE(counter_mutex);   /* Protege CT-01: shared_toggle_counter */
K_MUTEX_DEFINE(mask_mutex);      /* Protege CT-02: shared_rgb_mask + GPIO */
K_MUTEX_DEFINE(rgb_excl_mutex);  /* Protege CT-03: exclusão mútua R/B */

/* Recursos compartilhados (agora protegidos por mutex) */
volatile uint32_t shared_toggle_counter = 0;
volatile uint8_t shared_rgb_mask = 0; // Bit 0: R, Bit 1: G, Bit 2: B
volatile uint8_t active_test_case = 0; // 0 = pausa/transição, 1..3 = testes

/* Métricas de auditoria */
volatile uint32_t expected_inc_a = 0;
volatile uint32_t expected_inc_b = 0;
volatile uint32_t ct01_led_anomalies = 0;
volatile uint32_t ct02_overwrites = 0;
volatile uint32_t ct02_simultaneous_rb = 0;
volatile uint32_t ct03_collisions = 0;

/* Espelhos de estado físico dos pinos */
volatile uint8_t red_hw_state = 0;
volatile uint8_t green_hw_state = 0;
volatile uint8_t blue_hw_state = 0;

static inline void set_red(int val) {
    gpio_pin_set_dt(&led_r, val);
    red_hw_state = val ? 1 : 0;
}

static inline void set_green(int val) {
    gpio_pin_set_dt(&led_g, val);
    green_hw_state = val ? 1 : 0;
}

static inline void set_blue(int val) {
    gpio_pin_set_dt(&led_b, val);
    blue_hw_state = val ? 1 : 0;
}

void apply_led_mask(uint8_t mask) {
    set_red((mask & 0x01) ? 1 : 0);
    set_green((mask & 0x02) ? 1 : 0);
    set_blue((mask & 0x04) ? 1 : 0);
}

/* =========================================================================
 * THREAD A
 * ========================================================================= */
void thread_a_entry(void *p1, void *p2, void *p3) {
    while (1) {
        if (active_test_case == 0) {
            k_msleep(20);
        }
        else if (active_test_case == 1) {
            /* Região crítica protegida: mantém k_busy_wait(50) dentro do lock */
            k_mutex_lock(&counter_mutex, K_FOREVER);
            uint32_t temp = shared_toggle_counter;
            k_busy_wait(50);
            shared_toggle_counter = temp + 1;
            expected_inc_a++;

			/* Pisca a cada 20 incrementos (~100 ms aceso / ~100 ms apagado = 5 Hz) */
            set_green(((shared_toggle_counter/20) % 2 == 0) ? 1 : 0);

            if (red_hw_state || blue_hw_state) {
                ct01_led_anomalies++;
                if (ct01_led_anomalies <= 10) {
                    LOG_WRN("CT-01,ANOMALIA_LED,t=%ums,R=%u,G=%u,B=%u",
                            (uint32_t)k_uptime_get(), red_hw_state, green_hw_state, blue_hw_state);
                }
            }
            k_mutex_unlock(&counter_mutex);

            k_msleep(10);
        } 
        else if (active_test_case == 2) {
            /* Liga Vermelho: Read-Modify-Write + k_busy_wait(100) atômicos via mutex */
            k_mutex_lock(&mask_mutex, K_FOREVER);
            uint8_t orig_mask = shared_rgb_mask;
            uint8_t local_mask = orig_mask | 0x01;
            k_busy_wait(100);

            if (shared_rgb_mask != orig_mask) {
                ct02_overwrites++;
                if (ct02_overwrites <= 10) {
                    LOG_WRN("CT-02,OVERWRITE,t=%ums,thread=A,lido=0x%02X,atual=0x%02X,gravado=0x%02X",
                            (uint32_t)k_uptime_get(), orig_mask, shared_rgb_mask, local_mask);
                }
            }
            shared_rgb_mask = local_mask;
            apply_led_mask(shared_rgb_mask);

            if (red_hw_state && blue_hw_state) {
                ct02_simultaneous_rb++;
            }
            k_mutex_unlock(&mask_mutex);

            /* Permanece fora do mutex durante o sleep para permitir sobreposição limpa */
            k_msleep(200);

            if (active_test_case != 2) continue;

            /* Desliga Vermelho */
            k_mutex_lock(&mask_mutex, K_FOREVER);
            orig_mask = shared_rgb_mask;
            local_mask = orig_mask & ~0x01;
            shared_rgb_mask = local_mask;
            apply_led_mask(shared_rgb_mask);
            k_mutex_unlock(&mask_mutex);

            k_msleep(200);
        }
        else if (active_test_case == 3) {
            /* Mantém o lock durante todo o pulso aceso para garantir exclusão mútua R/B */
            k_mutex_lock(&rgb_excl_mutex, K_FOREVER);
            if (active_test_case == 3) {
                set_blue(0);
                k_busy_wait(80);
                set_red(1);

                if (red_hw_state && blue_hw_state) {
                    ct03_collisions++;
                    if (ct03_collisions <= 10) {
                        LOG_ERR("CT-03,COLISAO_RB,t=%ums,thread=A,estado=MAGENTA_PROIBIDO",
                                (uint32_t)k_uptime_get());
                    }
                }
                k_msleep(50);
                set_red(0);
            }
            k_mutex_unlock(&rgb_excl_mutex);

            k_msleep(50);
        }
    }
}

/* =========================================================================
 * THREAD B
 * ========================================================================= */
void thread_b_entry(void *p1, void *p2, void *p3) {
    while (1) {
        if (active_test_case == 0) {
            k_msleep(20);
        }
        else if (active_test_case == 1) {
            k_mutex_lock(&counter_mutex, K_FOREVER);
            uint32_t temp = shared_toggle_counter;
            k_busy_wait(50);
            shared_toggle_counter = temp + 1;
            expected_inc_b++;
            k_mutex_unlock(&counter_mutex);

            k_msleep(10);
        } 
        else if (active_test_case == 2) {
            /* Liga Azul */
            k_mutex_lock(&mask_mutex, K_FOREVER);
            uint8_t orig_mask = shared_rgb_mask;
            uint8_t local_mask = orig_mask | 0x04;
            k_busy_wait(100);

            if (shared_rgb_mask != orig_mask) {
                ct02_overwrites++;
                if (ct02_overwrites <= 10) {
                    LOG_WRN("CT-02,OVERWRITE,t=%ums,thread=B,lido=0x%02X,atual=0x%02X,gravado=0x%02X",
                            (uint32_t)k_uptime_get(), orig_mask, shared_rgb_mask, local_mask);
                }
            }
            shared_rgb_mask = local_mask;
            apply_led_mask(shared_rgb_mask);

            if (red_hw_state && blue_hw_state) {
                ct02_simultaneous_rb++;
            }
            k_mutex_unlock(&mask_mutex);

            k_msleep(150);

            if (active_test_case != 2) continue;

            /* Desliga Azul */
            k_mutex_lock(&mask_mutex, K_FOREVER);
            orig_mask = shared_rgb_mask;
            local_mask = orig_mask & ~0x04;
            shared_rgb_mask = local_mask;
            apply_led_mask(shared_rgb_mask);
            k_mutex_unlock(&mask_mutex);

            k_msleep(150);
        }
        else if (active_test_case == 3) {
            k_mutex_lock(&rgb_excl_mutex, K_FOREVER);
            if (active_test_case == 3) {
                set_red(0);
                k_busy_wait(80);
                set_blue(1);

                if (red_hw_state && blue_hw_state) {
                    ct03_collisions++;
                    if (ct03_collisions <= 10) {
                        LOG_ERR("CT-03,COLISAO_RB,t=%ums,thread=B,estado=MAGENTA_PROIBIDO",
                                (uint32_t)k_uptime_get());
                    }
                }
                k_msleep(40);
                set_blue(0);
            }
            k_mutex_unlock(&rgb_excl_mutex);

            k_msleep(40);
        }
    }
}

/* =========================================================================
 * MAIN: Supervisor com barreira limpa entre testes
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
    LOG_INF("RELATORIO DE EXECUCAO - FRDM-KL25Z (COM PROTECAO K_MUTEX)");
    LOG_INF("==========================================================");

    k_thread_create(&thread_a_data, thread_a_stack, K_THREAD_STACK_SIZEOF(thread_a_stack),
                    thread_a_entry, NULL, NULL, NULL,
                    PRIORITY_HIGH, 0, K_NO_WAIT);

    k_thread_create(&thread_b_data, thread_b_stack, K_THREAD_STACK_SIZEOF(thread_b_stack),
                    thread_b_entry, NULL, NULL, NULL,
                    PRIORITY_HIGH, 0, K_NO_WAIT);

    uint32_t ciclo = 1;
    uint8_t next_test = 1;

    while (1) {
        active_test_case = 0;
        k_msleep(300);

        shared_toggle_counter = 0;
        expected_inc_a = 0;
        expected_inc_b = 0;
        ct01_led_anomalies = 0;
        ct02_overwrites = 0;
        ct02_simultaneous_rb = 0;
        ct03_collisions = 0;
        shared_rgb_mask = 0;
        apply_led_mask(0);

        LOG_INF("--- [CICLO %u] INICIO CT-0%d (t=%ums) ---",
                ciclo, next_test, (uint32_t)k_uptime_get());

        active_test_case = next_test;
        k_sleep(K_SECONDS(10));

        active_test_case = 0;
        k_msleep(250);

        if (next_test == 1) {
            uint32_t esperado = expected_inc_a + expected_inc_b;
            uint32_t observado = shared_toggle_counter;
            uint32_t perdidos = (esperado > observado) ? (esperado - observado) : 0;

            LOG_INF("RESUMO,CT-01,esperado=%u,observado=%u,perdidos=%u,anomalias_led=%u,status=%s",
                    esperado, observado, perdidos, ct01_led_anomalies,
                    (perdidos > 0 || ct01_led_anomalies > 0) ? "FALHA_DETECTADA" : "OK");
        } 
        else if (next_test == 2) {
            LOG_INF("RESUMO,CT-02,sobrescritas=%u,sobreposicoes_magenta=%u,mascara_final=0x%02X,status=%s",
                    ct02_overwrites, ct02_simultaneous_rb, shared_rgb_mask,
                    (ct02_overwrites > 0) ? "FALHA_DETECTADA" : "OK");
        } 
        else if (next_test == 3) {
            LOG_INF("RESUMO,CT-03,colisoes_magenta=%u,status=%s",
                    ct03_collisions,
                    (ct03_collisions > 0) ? "FALHA_DETECTADA" : "OK");
        }

        next_test = (next_test % 3) + 1;
        if (next_test == 1) {
            ciclo++;
        }
    }
    return 0;
}