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

Este projeto foi desenvolvido para a placa de desenvolvimento **NXP FRDM-KL25Z** (microcontrolador ARM Cortex-M0+) utilizando o **Zephyr RTOS** integrado ao ambiente **PlatformIO**. O objetivo principal é demonstrar, de forma visual e por meio de relatórios de log estruturados, a ocorrência de **condições de corrida (*race conditions*)** em sistemas embarcados preemptivos de núcleo único (*single-core*).

### 1.1. Hardware e Mapeamento do Devicetree (DTS)

A placa FRDM-KL25Z conta com um LED RGB integrado cujos catodos são conectados diretamente aos pinos de GPIO do microcontrolador, operando em lógica **ativo-baixo** (`GPIO_ACTIVE_LOW`). Conforme o *Devicetree* (DTS) utilizado neste projeto, os *aliases* estão mapeados da seguinte forma:

* **Canal Verde (`led0`):** Pino `PTB19`
* **Canal Azul (`led1`):** Pino `PTD1`
* **Canal Vermelho (`led2`):** Pino `PTB18`

Na camada de abstração do Zephyr, o driver GPIO traduz automaticamente o estado lógico `1` para nível físico baixo ($0\text{ V}$, acendendo o LED) e `0` para nível físico alto ($3{,}3\text{ V}$, apagando o LED).

### 1.2. Concorrência, Escalonamento e Isolamento de Testes

O sistema instancia duas threads concorrentes (**Thread A** e **Thread B**) e uma **Thread Principal (`main`)** que atua como supervisora e geradora de relatórios:

* **Threads de Trabalho (`Thread A` e `Thread B`):** Ambas são criadas com a **mesma prioridade** (`PRIORITY_HIGH = 5`), com preempção (`CONFIG_PREEMPT_ENABLED=y`) e *time-slicing* (`CONFIG_TIMESLICING=y`) habilitados. Isso permite que o *kernel* do Zephyr alterne o contexto entre as duas threads no meio de operações não atômicas.
* **Thread Supervisora (`main`) e Barreira de Transição:** A thread principal alterna automaticamente entre os três casos de teste a cada **10 segundos**. Entre cada teste, o sistema entra em um estado de pausa (`active_test_case = 0`) por $300\text{ ms}$ antes de iniciar o próximo cenário e $250\text{ ms}$ ao final dele. Essa barreira garante que as threads concluam seus atrasos (`k_msleep`) e não provoquem vazamento de estado ou acionamento indevido de LEDs de um teste para o outro.

### 1.3. Geração de Relatórios de Execução (Logging)

Para permitir a auditoria precisa sem interferir no *timing* das *race conditions*, o projeto combina dois mecanismos:

1. **Subsistema de Log Diferido no Zephyr (`CONFIG_LOG_MODE_DEFERRED=y`):** Os eventos de falha são gravados rapidamente em um buffer em RAM (`1024 bytes`) com *timestamp* em milissegundos (`k_uptime_get()`) e transmitidos pela interface UART (`115200 bps`) em segundo plano.
2. **Captura Automática em Arquivo (PlatformIO):** O filtro `monitor_filters = log2file` no `platformio.ini` grava toda a telemetria em arquivos `.log` na pasta `logs/` do projeto, utilizando formato estruturado separado por vírgulas (`EVENTO` e `RESUMO`).

---

## 2. Metodologia de Indução das Race Conditions

Para tornar as falhas de concorrência determinísticas e reproduzíveis durante os testes, o firmware aplica três técnicas:

1. **Compartilhamento Desprotegido de Recursos:** Variáveis globais (`shared_toggle_counter` e `shared_rgb_mask`) e pinos físicos de GPIO são acessados simultaneamente pelas duas threads sem sincronização (`k_mutex`, `k_sem` ou `atomic_t`). O qualificador `volatile` impede apenas otimizações do compilador, sem garantir atomicidade.
2. **Alargamento da Janela Crítica (`k_busy_wait`):** Entre a leitura (*Read*) e a escrita (*Write*), são inseridos atrasos ativos de processamento ($50\ \mu\text{s}$ a $100\ \mu\text{s}$), mantendo a CPU ocupada e maximizando a chance de preempção dentro da região crítica.
3. **Batimento de Frequências Assíncronas:** As threads operam com períodos distintos ($200\text{ ms}$ vs. $150\text{ ms}$ no CT-02; $50\text{ ms}$ vs. $40\text{ ms}$ no CT-03), garantindo cruzamentos periódicos de execução ao longo dos 10 segundos de teste.

---

## 3. Documentação dos Casos de Teste

Abaixo estão especificados os três casos de teste implementados no firmware, com o mapeamento corrigido do DTS e os respectivos indicadores registrados no arquivo de log.

### Caso de Teste 01 (CT-01): Perda de Incremento (*Lost Update*) em Contador de Acionamento

| Campo | Descrição |
| :--- | :--- |
| **Caso de Teste** | **CT-01:** Concorrência *Read-Modify-Write* em variável de controle de estado do LED Verde (`led0`). |
| **Pré-condição** | 1. Placa FRDM-KL25Z conectada via porta **SDA USB** (`115200 bps`) com captura `log2file` ativa.<br>2. Barreira de transição concluída (`active_test_case` passa de `0` para `1`).<br>3. `shared_toggle_counter = 0`, contadores privados `expected_inc_a = 0`, `expected_inc_b = 0` e todos os LEDs apagados.<br>4. Threads A e B com mesma prioridade (`5`) e *time-slicing* ativo. |
| **Etapas de Teste** | 1. Executar o CT-01 durante 10 segundos.<br>2. Observar o padrão de piscada do LED Verde (`led0`) e confirmar que os canais Vermelho (`led2`) e Azul (`led1`) permanecem apagados.<br>3. Inspecionar a linha `RESUMO,CT-01` no arquivo de log gerado. |
| **Pós-condição** | `shared_toggle_counter` registra um valor inferior à soma real de iterações (`expected_inc_a + expected_inc_b`), resultando em `perdidos > 0` e `status=FALHA_DETECTADA` no log. |
| **Comportamento Esperado vs. Não Esperado** | **Esperado (Sem Race Condition):** A cada ciclo de $10\text{ ms}$, ambas as threads incrementam o contador sem perdas (`perdidos=0`), com piscada uniforme apenas no LED Verde e `anomalias_led=0`.<br>**Não Esperado (Observado):** Perda de 30% a 50% dos incrementos no log (`observado` muito menor que `esperado`) e **travamentos irregulares** no LED Verde (permanecendo aceso ou apagado por ciclos consecutivos). Caso qualquer outro LED acenda indevidamente, o log registra `CT-01,ANOMALIA_LED`. |

* **Momento Exato e Condição do Erro:** Ocorre durante a operação não atômica de incremento. A **Thread A** lê `shared_toggle_counter` (ex.: $10$) para `temp` e entra em `k_busy_wait(50)`. Antes de A gravar $11$, o escalonador alterna para a **Thread B**, que também lê $10$. Ambas calculam $10 + 1$ e escrevem $11$ na memória, descartando um incremento e congelando a paridade que controla o pino `PTB19` (`led0`).

---

### Caso de Teste 02 (CT-02): Corrupção de Máscara de Bits (*Dirty Overwrite*) no LED RGB

| Campo | Descrição |
| :--- | :--- |
| **Caso de Teste** | **CT-02:** Escrita concorrente não sincronizada em registrador virtual de máscara RGB (`shared_rgb_mask`) controlando Vermelho (`led2`) e Azul (`led1`). |
| **Pré-condição** | 1. Conclusão do CT-01 e pausa de isolamento de $300\text{ ms}$.<br>2. Transição para `active_test_case = 2` com `shared_rgb_mask = 0x00` e LEDs apagados.<br>3. Thread A operando em períodos de $200\text{ ms}$ (Vermelho - bit `0x01` / `led2`) e Thread B em períodos de $150\text{ ms}$ (Azul - bit `0x04` / `led1`). |
| **Etapas de Teste** | 1. Monitorar visualmente o LED RGB durante 10 segundos, observando as transições entre Vermelho, Azul e Magenta (sobreposição normal de Vermelho + Azul).<br>2. Verificar a ocorrência de extinção prematura de cor antes do fim do ciclo de $200\text{ ms}$ ou $150\text{ ms}$.<br>3. Analisar no arquivo de log os eventos `CT-02,OVERWRITE` e o total consolidado em `RESUMO,CT-02` (`sobrescritas` e `sobreposicoes_magenta`). |
| **Pós-condição** | Registro de `sobrescritas > 0` e `status=FALHA_DETECTADA` no relatório, além de possível estado residual inconsistente em `mascara_final`. |
| **Comportamento Esperado vs. Não Esperado** | **Esperado (Sem Race Condition):** Sobreposição limpa das ondas de $200\text{ ms}$ (Vermelho) e $150\text{ ms}$ (Azul), gerando transições previsíveis entre Vermelho puro, Azul puro e Magenta, com `sobrescritas=0`.<br>**Não Esperado (Observado):** Ocorrem *glitches* visuais (extinção prematura ou religamento indevido de uma das cores) e o log registra eventos `CT-02,OVERWRITE` apontando divergência entre o valor `lido` antes do `k_busy_wait(100)` e o valor `atual` na memória no momento da gravação. |

* **Momento Exato e Condição do Erro:** Ocorre no cruzamento assíncrono dos ciclos de $200\text{ ms}$ e $150\text{ ms}$ (ex.: $t = 600\text{ ms}$, $1200\text{ ms}$). A **Thread B** lê `shared_rgb_mask = 0x01` (Vermelho ligado), calcula `local_mask = 0x05` (Magenta) e aguarda em `k_busy_wait(100)`. Nesse intervalo, a **Thread A** acorda para desligar o Vermelho, lê `0x01`, limpa o bit 0 e grava `0x00`. Ao sair do *busy wait*, B sobrescreve a memória com `0x05`, **religando indevidamente o LED Vermelho** que A havia acabado de apagar (ou o inverso, apagando o Azul recém-ligado).

---

### Caso de Teste 03 (CT-03): Violação de Exclusão Mútua (Colisão de Hardware / Estado Proibido)

| Campo | Descrição |
| :--- | :--- |
| **Caso de Teste** | **CT-03:** Violação de invariante de segurança onde os canais Vermelho (`led2`) e Azul (`led1`), mutuamente exclusivos, são acionados simultaneamente. |
| **Pré-condição** | 1. Conclusão do CT-02 e pausa de isolamento de $300\text{ ms}$.<br>2. Transição para `active_test_case = 3` com todos os LEDs apagados.<br>3. Regra de negócio: **O LED Vermelho (`led2` / `PTB18`) e o LED Azul (`led1` / `PTD1`) jamais podem estar ligados simultaneamente**.<br>4. Thread A executa ciclos de $50\text{ ms}$; Thread B executa ciclos de $40\text{ ms}$. |
| **Etapas de Teste** | 1. Executar o CT-03 por 10 segundos.<br>2. Inspecionar visualmente o LED RGB buscando flashes na cor **Magenta/Violeta** (indicativo físico de que `led2` e `led1` estão ativos no mesmo instante).<br>3. Conferir no arquivo de log os eventos `CT-03,COLISAO_RB` (`estado=MAGENTA_PROIBIDO`) e o contador `colisoes_magenta` em `RESUMO,CT-03`. |
| **Pós-condição** | Detecção de `colisoes_magenta > 0` e `status=FALHA_DETECTADA` no relatório, comprovando que ambos os pinos permaneceram ativos simultaneamente por até $40\text{ ms}$ por colisão. |
| **Comportamento Esperado vs. Não Esperado** | **Esperado (Sem Race Condition):** Apenas flashes alternados de Vermelho puro ou Azul puro, pois cada thread desliga o LED oposto antes de acionar o seu (`colisoes_magenta=0`).<br>**Não Esperado (Observado):** Em intervalos periódicos (a cada $\approx 200\text{ ms}$, mínimo múltiplo comum entre $40\text{ ms}$ e $50\text{ ms}$), o LED emite **flashes de cor Magenta** e o log registra os instantes exatos (`t=...ms`) em que a exclusão mútua foi violada. |

* **Momento Exato e Condição do Erro:** A falha ocorre na sequência **Check-Then-Act** dividida pelo `k_busy_wait(80)`:
  1. **Thread A** executa `set_blue(0)` (garante que o Azul está desligado) e entra no atraso de $80\ \mu\text{s}$.
  2. Durante esses $80\ \mu\text{s}$, a **Thread B** executa `set_red(0)` (garante que o Vermelho está desligado) e também entra em atraso.
  3. Ambas as threads já forçaram a condição de segurança, mas ainda não acionaram seus próprios pinos.
  4. Na sequência imediata, **Thread A** liga o Vermelho (`set_red(1)`) e **Thread B** liga o Azul (`set_blue(1)`). Sem proteção atômica (`k_mutex`), ambas entram na região crítica simultaneamente, gerando o estado proibido **Magenta**.