# Validação do firmware v1.5.0-smalltv-scene1

Binário distribuído: 543.504 bytes, SHA256 `8f0a270972885aab13942fa31f8d6b375d5b5f2e7eebf676c470f8c5c8ce0964`.

Revisão independente verificou eboot em 0, aplicação em 0x1000, cabeçalhos 4 MB/DIO/40 MHz, segmentos, XOR e CRC completo. As palavras de tamanho/CRC em 0x1010..0x1017 foram zeradas conforme elf2bin para os cálculos. LittleFS 0x200000..0x3FA000 e EEPROM 0x3FB000 preservados. As oito tabelas YCbCr, total de 6.804 bytes, foram encontradas em endereços de flash 0x402..., não DRAM.

RAM estática: 37.464 bytes. Estado dinâmico da cena: 7.528 bytes. Fonte/core revisados para recorte, offsets, ordenação dos glifos, texto e limites de buffer. Validação/alocação da cena candidata antecedem a substituição; GIF e OTA liberam o estado quando precisam da memória.

O núcleo C++ real foi executado com MSVC AddressSanitizer: 1.000 quadros, 128 cenas determinísticas de 32 nós, coordenadas -480..480, UTF-8, canários, limites do texto, margens, pulso e viradas de minuto/meia-noite. Nenhum acesso inválido detectado. Perfil host final: média 466 µs, p95 687 µs; não são tempos do ESP8266. O harness original dependia do fixture do aplicativo e não faz parte deste pacote exclusivo de firmware.

No aparelho foram testados os desenhos individuais, batch e cena, autenticação rejeitada, seis payloads inválidos preservando a cena, web, NTP e disponibilidade OTA. Dez substituições da cena e 30 consultas em aproximadamente 35 segundos mantiveram heap de 22.056 bytes e maior bloco de 16.448 bytes; fragmentação de 24%, sem mudança. Consulta posterior, com mais de cinco minutos de uptime, mostrou aproximadamente 22 KB livres e nenhum novo reset.

Primeira composição: cerca de 111 ms. Quadros animados amostrados: até 16,8 ms. Ausência de tela preta e pulso suave foram confirmados visualmente. O contraste inicial do relógio foi aumentado via JSON para 64..255, sem regravar firmware.

Limites: teste curto, sem garantia de funcionamento indefinido; não foi instrumentado o pico de heap durante parsing. O teste host não emula ESP8266, watchdog, Wi-Fi, SPI ou OTA. Recuperação foi preservada e revisada, mas não foi provocado boot loop nem perda de energia durante a atualização.
