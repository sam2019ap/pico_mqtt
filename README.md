# Firmware MQTT — Raspberry Pi Pico W

Firmware em C para a **Raspberry Pi Pico W** que utiliza o protocolo MQTT para publicar a temperatura interna da CPU e controlar o LED integrado a partir de mensagens recebidas via broker.

\---

## Funcionalidades

|Funcionalidade|Descrição|
|-|-|
|**Publicação de temperatura**|Ao pressionar o botão (GP15), publica a temperatura do núcleo da CPU (°C) no tópico `/seu\\\_nome/seu\\\_ra/temperatura`|
|**Controle do LED**|Assina `/seu\\\_nome/seu\\\_ra/led`; um número inteiro positivo define o intervalo de piscar em segundos; `0` ou valor inválido desliga o LED|

\---

## Estrutura do Projeto

```
pico\\\_mqtt/
├── CMakeLists.txt          # Sistema de build (CMake)
├── README.md               # Este arquivo
├── include/
│   └── config.h            # ⚠️  Credenciais e configurações
└── src/
    └── main.c              # Código-fonte principal
```

\---

## Pré-requisitos

* **Hardware:** Raspberry Pi Pico **W** (com chip Wi-Fi CYW43)
* **SDK:** [pico-sdk](https://github.com/raspberrypi/pico-sdk) v1.5+
* **Ferramentas:** `cmake`, `ninja` (ou `make`), `arm-none-eabi-gcc`
* **Broker MQTT:** Mosquitto, HiveMQ ou qualquer broker acessível na rede local

\---

## Configuração

Antes de compilar, edite o arquivo **`include/config.h`** com seus dados:

```c
// Wi-Fi
#define WIFI\\\_SSID       "SUA\\\_REDE\\\_WIFI"
#define WIFI\\\_PASSWORD   "SUA\\\_SENHA\\\_WIFI"

// Broker MQTT
#define MQTT\\\_BROKER\\\_IP  "192.168.1.100"   // IP do broker na sua rede

// Tópicos
#define MQTT\\\_TOPIC\\\_TEMP  "/samuel\\\_araujo/210025640/temperatura"
#define MQTT\\\_TOPIC\\\_LED   "/samuel\\\_araujo/210025640/led"
```

\---

## Compilação

```bash
# 1. Clone o repositório
git clone https://github.com/seu\\\_usuario/pico\\\_mqtt.git
cd pico\\\_mqtt

# 2. Configure a variável do SDK (ajuste o caminho)
export PICO\\\_SDK\\\_PATH=/caminho/para/pico-sdk

# 3. Crie o diretório de build e compile
mkdir build \\\&\\\& cd build
cmake ..
cmake --build . -j4
```

O arquivo `pico\\\_mqtt.uf2` será gerado dentro de `build/`.

\---

## Gravação no Pico W

1. Segure o botão **BOOTSEL** no Pico W e conecte o cabo USB ao computador
2. O Pico W aparecerá como um drive USB chamado `RPI-RP2`
3. Copie o arquivo `pico\\\_mqtt.uf2` para esse drive
4. O Pico W reiniciará automaticamente e executará o firmware

\---

## Uso

### Monitoramento serial (debug)

```bash
# Linux/macOS — substitua /dev/ttyACM0 pelo seu dispositivo
minicom -b 115200 -D /dev/ttyACM0

# Ou com screen:
screen /dev/ttyACM0 115200
```

### Publicar temperatura

Pressione o **botão** conectado ao **GP15** (ativo em GND).  
A mensagem publicada terá o formato: `26.43` (graus Celsius, 2 casas decimais).

### Controlar o LED via MQTT

```bash
# Piscar a cada 2 segundos
mosquitto\\\_pub -h 192.168.1.100 -t "/samuel\\\_araujo/210025640/led" -m "2"

# Piscar a cada 5 segundos
mosquitto\\\_pub -h 192.168.1.100 -t "/samuel\\\_araujo/210025640/led" -m "5"

# Desligar o LED
mosquitto\\\_pub -h 192.168.1.100 -t "/samuel\\\_araujo/210025640/led" -m "0"

# Desligar com valor inválido
mosquitto\\\_pub -h 192.168.1.100 -t "/samuel\\\_araujo/210025640/led" -m "abc"
```

### Assinar o tópico de temperatura

```bash
mosquitto\\\_sub -h 192.168.1.100 -t "/samuel\\\_araujo/210025640/temperatura"
```

\---

## Esquema de Hardware

```
Raspberry Pi Pico W
┌──────────────────┐
│          GP15 ───┼─── Botão ─── GND
│                  │
│  LED integrado   │   (controlado via CYW43, sem GPIO externo)
│  (CYW43)         │
│                  │
│  ADC4 (interno)  │   Sensor de temperatura do RP2040
└──────────────────┘
```

> O LED integrado do Pico W é controlado pelo chip CYW43 (Wi-Fi), não diretamente por um pino GPIO. Por isso, usa-se `cyw43\\\_arch\\\_gpio\\\_put()` em vez de `gpio\\\_put()`.

\---

## Diagrama de Fluxo

```
Inicialização
    │
    ├─ Configura ADC (sensor de temperatura)
    ├─ Configura GPIO do botão (GP15, pull-up)
    ├─ Conecta ao Wi-Fi
    └─ Conecta ao broker MQTT
           │
           └─ Subscribe em /samuel\\\_araujo/210025640/led
                    │
               Loop Principal
              ┌─────┴──────┐
              │             │
         Botão             Mensagem MQTT
         pressionado?      no tópico /led?
              │             │
         Sim ─┤        Analisa payload
              │        ┌────┴────┐
         Lê temp.      Número    Inválido/0
         Publica       positivo      │
         em /samuel\\\_   │          LED OFF
         araujo/...    Define     Timer OFF
                       intervalo
                       Timer ON
```

\---

## Dependências do SDK

|Biblioteca|Função|
|-|-|
|`pico\\\_stdlib`|GPIO, timers, stdio USB|
|`hardware\\\_adc`|Leitura do sensor de temperatura interno|
|`pico\\\_cyw43\\\_arch\\\_lwip\\\_poll`|Driver Wi-Fi + stack TCP/IP (modo polling)|
|`pico\\\_lwip\\\_mqtt`|Implementação do protocolo MQTT|

\---

## Observações Técnicas

* **Modo polling vs RTOS:** O firmware usa `cyw43\\\_arch\\\_lwip\\\_poll`, que requer chamadas periódicas a `cyw43\\\_arch\\\_poll()` no loop principal em vez de usar um RTOS. Isso simplifica o código para projetos embarcados sem sistema operacional.
* **Temperatura interna:** O sensor do RP2040 tem precisão de ±2°C. Para aplicações críticas, use um sensor externo (DS18B20, DHT22, etc.).
* **QoS 0:** As mensagens são enviadas com QoS 0 (at most once), sem confirmação de entrega. Para garantia de entrega, use QoS 1.
* **Reconexão:** O firmware não implementa reconexão automática ao broker. Em caso de queda, reinicie o dispositivo ou implemente um watchdog.

\---

## Licença

MIT License — livre para uso educacional e modificação.

