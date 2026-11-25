# PSI-Microcontroladores2-Aula06
Atividade: Resolução de Race Condition com Semáforo ou Mutex

## 🎯 Objetivos da Atividade
Nesta atividade, os alunos deverão:
- Criar um código (linguagem livre) que apresenta **condições de corrida (race conditions)** (uso liberado de IA generativa).
- Trabalhar em **duplas**, com **avaliação cruzada interna** entre os integrantes do grupo (não deve usar IA generativa).
- Aplicar **testes estruturados** com pré-condição, etapas de teste e pós-condição (não deve usar IA generativa).
- Demonstrar como o problema de concorrência foi **identificado e resolvido** com uso de semáforo ou mutex (não deve usar IA generativa).

## 🧠 Etapas da Atividade

### **1️⃣ Criação do Código Anterior**
- Cada integrante do grupo deverá **criar um código**, e **executar o código do colega** que contém uma race condition.
- Documentar:
  - O comportamento incorreto observado.
  - O momento em que o erro ocorre (condição específica, sequência de eventos, etc.).

### **2️⃣ Planejamento de Testes**
Para cada cenário, descreva **três casos de teste** seguindo o formato abaixo:

| Caso de Teste | Pré-condição | Etapas de Teste | Pós-condição Esperada |
|----------------|---------------|------------------|------------------------|
| 1 | ... | ... | ... |
| 2 | ... | ... | ... |
| 3 | ... | ... | ... |

### **3️⃣ Correção e Reteste**
- Corrigir o código para **eliminar a race condition** usando semáforo ou mutex.
- Reexecutar **os mesmos casos de teste** e registrar:
  - As mudanças feitas, justificando sua escolha pelo mutex ou semáforo.
  - O resultado após a correção com evidências (capturas de tela por exemplo).

### **4️⃣ Avaliação Interna (entre colegas do mesmo grupo)**
Cada integrante deverá:
1. Criar o código de race condition.
2. Executar o código original do colega conforme os testes planejados.
3. Executar o código corrigido do colega conforme os testes planejados.  
4. Registrar uma **avaliação curta** (pode ser no final do README):
   - O que estava errado antes.  
   - O que mudou com a correção.
   - Se o comportamento agora é estável.  

## 📦 Entregáveis

No repositório da dupla, incluir:
1. `README.md` (este arquivo) contendo:
   - Nomes dos integrantes.
   - 2 Cenários escolhidos.
   - 6 Casos de teste.
   - Descrição das 2 race conditions e das soluções.
   - 2 Avaliações cruzadas.
2. Código-fonte organizado (considerando um código original e um corrigido por cada integrante, então 4 subpastas no total):
   - `codigo_original/`
   - `codigo_corrigido/`
3. Evidências (prints, logs, vídeos curtos, etc.) da execução dos testes.

---

**Entrega:** um PDF do markdown final (com o link do repositório aberto) no Moodle.


# Documentação Técnica: Simulação e Análise de Race Conditions no Zephyr RTOS (FRDM-KL25Z)

## 1. Visão Geral e Arquitetura do Projeto

Este projeto foi desenvolvido para a placa de desenvolvimento **NXP FRDM-KL25Z** (microcontrolador ARM Cortex-M0+) utilizando o **Zephyr RTOS** integrado ao ambiente **PlatformIO**. O objetivo principal é demonstrar, de forma visual e telemétrica, a ocorrência de **condições de corrida (*race conditions*)** em sistemas embarcados preemptivos de núcleo único (*single-core*).

### 1.1. Hardware e Periféricos Utilizados
A placa FRDM-KL25Z conta com um LED RGB integrado cujos catodos são conectados diretamente aos pinos de GPIO do microcontrolador, operando em lógica **ativo-baixo** (`GPIO_ACTIVE_LOW`):
* **Canal Vermelho (`led0`):** Pino `PTB18`
* **Canal Verde (`led1`):** Pino `PTB19`
* **Canal Azul (`led2`):** Pino `PTD1`

Na camada de abstração do Zephyr (*Devicetree*), o driver GPIO traduz automaticamente o estado lógico `1` para nível físico baixo (`0V`, acendendo o LED) e `0` para nível físico alto (`3.3V`, apagando o LED). Além disso, a interface serial UART via OpenSDA (`115200 bps`) é utilizada para registrar os diagnósticos de execução.

### 1.2. Concorrência e Escalonamento
O sistema instancia duas threads concorrentes (**Thread A** e **Thread B**) e uma **Thread Principal (`main`)** que atua como supervisora de testes:
* **Threads de Trabalho (`Thread A` e `Thread B`):** Ambas são criadas com a **mesma prioridade** (`PRIORITY_HIGH = 5`) e com o escalonamento preemptivo habilitado (`CONFIG_PREEMPT_ENABLED=y`). Isso permite que o *kernel* do Zephyr utilize *time-slicing* e interrupções de sistema (*system ticks*) para alternar o contexto entre as duas threads a qualquer momento, inclusive no meio de operações não atômicas.
* **Thread Supervisora (`main`):** Executa em segundo plano, alternando automaticamente entre os três cenários de teste a cada **10 segundos**, reiniciando as variáveis globais e imprimindo o relatório de inconsistências no terminal serial.

---

## 2. Metodologia de Indução das Race Conditions

Em sistemas reais, *race conditions* costumam ser falhas intermitentes e difíceis de reproduzir (*Heisenbugs*), pois dependem do alinhamento exato de interrupções de hardware. Para tornar esses defeitos determinísticos e observáveis durante os testes, o código aplica três técnicas de indução:

1. **Compartilhamento Desprotegido de Recursos:** Variáveis globais (`shared_toggle_counter` e `shared_rgb_mask`) e pinos físicos de GPIO são acessados simultaneamente pelas duas threads sem o uso de primitivas de sincronização (como `k_mutex`, `k_sem` ou operações atômicas `atomic_t`). O qualificador `volatile` é utilizado apenas para impedir que o compilador otimize leituras em registradores da CPU, mas não garante atomicidade no barramento de memória.
2. **Alargamento da Janela Crítica (`k_busy_wait`):** Entre a leitura de um estado compartilhado (*Read*) e a escrita do novo valor (*Write*), foram inseridos atrasos ativos de processamento (`k_busy_wait` de $50\ \mu\text{s}$ a $100\ \mu\text{s}$). Diferente de `k_msleep()`, que cede voluntariamente a CPU, o `k_busy_wait()` mantém a thread ocupando o processador, maximizando a probabilidade de o escalonador interrompê-la exatamente dentro da região crítica.
3. **Batimento de Frequências Assíncronas:** Nos testes de controle de LEDs, as threads operam com períodos de dormência distintos e primos entre si ou com múltiplos comuns periódicos (ex: $200\text{ ms}$ vs. $150\text{ ms}$, e $50\text{ ms}$ vs. $40\text{ ms}$). Isso garante que os momentos de leitura e escrita das threads se cruzem ciclicamente ao longo dos 10 segundos de cada teste.

---

## 3. Documentação dos Casos de Teste

Abaixo estão especificados os três casos de teste implementados no firmware, detalhando as condições de execução, os desvios observados e o exato momento lógico em que a falha ocorre.

### Caso de Teste 01 (CT-01): Perda de Incremento (*Lost Update*) em Contador de Acionamento

| Campo | Descrição |
| :--- | :--- |
| **Caso de Teste** | **CT-01:** Concorrência *Read-Modify-Write* em variável de controle de estado do LED Verde. |
| **Pré-condição** | 1. Placa FRDM-KL25Z energizada e conectada via OpenSDA (Serial `115200 bps`).<br>2. Variável de controle configurada para `active_test_case = 1`.<br>3. `shared_toggle_counter = 0` e todos os LEDs apagados.<br>4. Threads A e B configuradas com a mesma prioridade (`5`) e preempção ativa no Zephyr. |
| **Etapas de Teste** | 1. Iniciar a execução do CT-01.<br>2. Observar o padrão de piscada do LED Verde (`led1`) durante 10 segundos.<br>3. Coletar o valor impresso em `[Diagnostico CT-01]` no terminal serial ao final do ciclo. |
| **Pós-condição** | `shared_toggle_counter` apresenta um valor significativamente menor que a soma real das iterações executadas por ambas as threads (~2000 incrementos em 10 segundos). |
| **Comportamento Esperado vs. Não Esperado** | **Esperado (Sem Race Condition):** A cada ciclo de $10\text{ ms}$, duas threads incrementam o contador (total de $+2$ por ciclo, mantendo a paridade ou alternando determinísticamente). O contador final atingiria aproximadamente $2000$.<br>**Não Esperado (Observado):** O contador final registra valores entre **$1000$ e $1400$** (perda de 30% a 50% das escritas). Visualmente, o LED Verde apresenta **travamentos irregulares** (permanece aceso ou apagado por vários ciclos consecutivos em vez de piscar uniformemente). |

* **Momento Exato e Condição do Erro:** Ocorre durante a operação não atômica de incremento. A **Thread A** lê `shared_toggle_counter` (ex: valor $10$) para sua variável local `temp` e entra em `k_busy_wait(50)`. Antes de A gravar o resultado $11$, o escalonador do Zephyr alterna o contexto para a **Thread B**, que também lê o valor $10$ da memória. Ambas calculam $10 + 1$ e escrevem $11$, descartando um incremento e congelando a paridade que define o acionamento do pino `PTB19`.

---

### Caso de Teste 02 (CT-02): Corrupção de Máscara de Bits (*Dirty Overwrite*) no LED RGB

| Campo | Descrição |
| :--- | :--- |
| **Caso de Teste** | **CT-02:** Escrita concorrente não sincronizada em registrador virtual de máscara RGB (`shared_rgb_mask`). |
| **Pré-condição** | 1. Conclusão do CT-01 e transição automática para `active_test_case = 2`.<br>2. `shared_rgb_mask = 0x00` (LEDs apagados).<br>3. Thread A operando em períodos de $200\text{ ms}$ (Vermelho - bit `0x01`) e Thread B em períodos de $150\text{ ms}$ (Azul - bit `0x04`). |
| **Etapas de Teste** | 1. Monitorar visualmente o encapsulamento difusor do LED RGB onboard durante 10 segundos.<br>2. Observar a transição entre as cores Vermelho, Azul e Magenta (Vermelho + Azul simultâneos).<br>3. Verificar se algum LED apaga prematuramente antes de completar seu tempo de ciclo ($200\text{ ms}$ ou $150\text{ ms}$). |
| **Pós-condição** | Estado residual inconsistente em `shared_rgb_mask` ao final do intervalo de 10 segundos. |
| **Comportamento Esperado vs. Não Esperado** | **Esperado (Sem Race Condition):** Sobreposição limpa das ondas quadradas de $200\text{ ms}$ (Vermelho) e $150\text{ ms}$ (Azul), gerando transições previsíveis entre Vermelho puro, Azul puro e Magenta.<br>**Não Esperado (Observado):** Ocorre **extinção prematura de cor** (*glitches* visuais). O LED Azul acende e apaga abruptamente em menos de $50\text{ ms}$ (ou vice-versa com o Vermelho), falhando em manter a cor Magenta nos momentos de sobreposição teórica. |

* **Momento Exato e Condição do Erro:** Ocorre no cruzamento assíncrono dos ciclos de $200\text{ ms}$ e $150\text{ ms}$ (como em $t = 600\text{ ms}$ ou $t = 1200\text{ ms}$). A **Thread B** lê `shared_rgb_mask = 0x01` (Vermelho ligado), aplica a operação OR (`local_mask |= 0x04`, resultando em `0x05` - Magenta) e aguarda em `k_busy_wait(100)`. Nesse intervalo, a **Thread A** acorda do `k_msleep(200)` para desligar o Vermelho, lê a máscara ainda como `0x01`, limpa o bit 0 (`0x00`) e grava `0x00` no barramento. Quando B termina o *busy wait*, grava sua cópia desatualizada `0x05`, **religando indevidamente o LED Vermelho** que A havia acabado de desligar (ou o inverso, apagando o Azul recém-ligado).

---

### Caso de Teste 03 (CT-03): Violação de Exclusão Mútua (Colisão de Hardware / Estado Proibido)

| Campo | Descrição |
| :--- | :--- |
| **Caso de Teste** | **CT-03:** Violação de invariante de segurança onde dois canais de LED mutuamente exclusivos acionam simultaneamente. |
| **Pré-condição** | 1. Transição para `active_test_case = 3`.<br>2. Regra de negócio do sistema: **O LED Vermelho (`led0`) e o LED Azul (`led2`) jamais devem estar energizados ao mesmo tempo** (invariante de exclusão mútua).<br>3. Thread A executa ciclos de $50\text{ ms}$; Thread B executa ciclos de $40\text{ ms}$. |
| **Etapas de Teste** | 1. Executar o CT-03 por 10 segundos.<br>2. Inspecionar a emissão luminosa do LED RGB buscando a presença da cor **Magenta/Violeta** (indicativo físico de que `PTB18 == LOW` e `PTD1 == LOW` no mesmo instante). |
| **Pós-condição** | Ambos os pinos (`led_r` e `led_b`) permanecem em nível lógico ativo simultaneamente por até $40\text{ ms}$ a cada colisão. |
| **Comportamento Esperado vs. Não Esperado** | **Esperado (Sem Race Condition):** Apenas flashes alternados de Vermelho puro ou Azul puro, pois cada thread força o desligamento do LED oposto antes de acionar o seu.<br>**Não Esperado (Observado):** Em intervalos periódicos (a cada $\approx 200\text{ ms}$, mínimo múltiplo comum entre $40\text{ ms}$ e $50\text{ ms}$), o LED emite **flashes intensos de cor Magenta/Rosa**, comprovando a violação física da exclusão mútua no hardware. |

* **Momento Exato e Condição do Erro:** A falha ocorre na sequência **Check-Then-Act** dividida pelo `k_busy_wait(80)`:
  1. **Thread A** executa `gpio_pin_set_dt(&led_b, 0)` (garante que o Azul está desligado) e entra no atraso de $80\ \mu\text{s}$.
  2. Durante esses $80\ \mu\text{s}$, a **Thread B** executa `gpio_pin_set_dt(&led_r, 0)` (garante que o Vermelho está desligado) e também entra em atraso.
  3. Ambas as threads já forçaram a condição de segurança, mas ainda não acionaram seus próprios pinos.
  4. Na sequência imediata, **Thread A** liga o Vermelho (`led_r = 1`) e **Thread B** liga o Azul (`led_b = 1`). Como o bloqueio do recurso oposto e o acionamento próprio não foram executados de forma atômica (protegidos por um `k_mutex`), ambas entram na região crítica simultaneamente.