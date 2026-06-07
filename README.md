# Atividade MQTT - Raspberry Pi Pico W

**Aluno:** Samuel Araujo  
**RA:** 210025640

## O que esse projeto faz

Firmware desenvolvido para a Raspberry Pi Pico W como atividade da disciplina.
O programa conecta no Wi-Fi e em um broker MQTT para:

- Quando o botão é pressionado, lê a temperatura interna da placa e publica no tópico MQTT
- Fica escutando o tópico do LED e controla o piscar conforme o valor recebido

## Tópicos MQTT utilizados

- Publicação: `/samuel_araujo/210025640/temperatura`
- Assinatura: `/samuel_araujo/210025640/led`

## Como funciona

**Botão:** Ao pressionar o botão no pino GP15, o firmware lê o sensor de temperatura
interno do RP2040 e publica o valor em graus Celsius no broker.

**LED:** Quando chega uma mensagem no tópico do LED:
- Número positivo (ex: 3) = LED pisca a cada 3 segundos
- Zero ou texto inválido = LED apaga

## Estrutura do projeto
pico_mqtt/
├── CMakeLists.txt
├── README.md
├── include/
│   └── config.h
└── src/
└── main.c