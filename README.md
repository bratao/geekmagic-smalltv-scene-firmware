# GeekMagic SmallTV Scene Firmware

Firmware para **SmallTV-Ultra / ESP8266**, com **Drawing API**, cenas retidas, relógio e animação local, atualizações sem limpeza preta intermediária e otimização de RAM.

Derivado de [Times-Z/GeekMagic-Open-Firmware](https://github.com/Times-Z/GeekMagic-Open-Firmware), v1.5.0, commit `9d31738bd653ca69b2fae979fec31bce9d20664f`. Os desenhos tradicionais foram adaptados de [HoloClawd-Open-Firmware](https://github.com/andrewjiang/HoloClawd-Open-Firmware).

Este repositório contém **somente firmware, ferramentas de build, documentação e binário**. Não contém cliente de agenda, integração Google, dados pessoais ou configuração de rede de uma instalação.

## O que mudou

- **Cenas nativas:** um JSON compacto descreve texto, relógio e formas. O dispositivo mantém a cena e anima sem novas requisições por quadro.
- **Sem apagão entre atualizações:** composição fora da tela em faixas de 240×8; somente linhas alteradas são transmitidas. Não há `clearScreen()` antes de substituir a cena.
- **Relógio autônomo:** usa NTP, com epoch recebido como alternativa. A mudança de minuto é avaliada antes do limitador da animação.
- **Pulso suave:** 40 níveis de intensidade, período configurável e passo de 100 ms. Exemplo com ciclo de quatro segundos, sem piscar abruptamente.
- **Texto legível:** Noto Sans antialias 4 bpp, cinco tamanhos, UTF-8 com caracteres portugueses, corte por largura e reticências.
- **HoloClawd Drawing API:** nove primitivas individuais e batch tradicional, além do modo de cena otimizado. Autenticação Bearer nas rotas de desenho.
- **GIF sem limpeza obrigatória:** `keep_screen:true` em stop/play. Corrigido também o tempo do último quadro do GIF.
- **Menos RAM:** 6.804 bytes de tabelas YCbCr movidos de DRAM para flash, preservando sinal e conversão de cores.
- **Diagnóstico:** heap, maior bloco livre, fragmentação, uptime, motivo de reset, revisões e tempo/pixels de renderização.

Mantidos: interface web, Wi-Fi/AP, NTP, LittleFS, configuração, autenticação, GIF, OTA e RescueMode. Desativados neste build: painel de métricas CPU/GPU do PC e log periódico de heap; o diagnóstico pela API permanece.

## Hardware e limites

| Item | Configuração |
| --- | --- |
| Dispositivo validado | SmallTV-Ultra |
| MCU / ambiente | ESP8266 ESP-12E / `esp12e` |
| LCD | ST7789, **240×240 nativos** |
| Flash | **4 MB, DIO, 40 MHz** |
| Linker | `eagle.flash.4m2m.ld` |
| RAM estática | **37.464 / 81.920 bytes** |
| Flash de aplicação | **539.351 / 1.044.464 bytes** |
| Imagem OTA | **543.504 bytes** |
| Estado da cena no heap | **7.528 bytes**, somente quando ativo |
| Buffer de composição | **3.840 bytes** |

RAM estática caiu de 43.664 para 37.464 bytes comparando com o build intermediário anterior à otimização: redução líquida de **6.200 bytes** mesmo com a cena nativa. O heap disponível em execução é outra métrica: no teste físico ficou em aproximadamente **22 KB**.

Um framebuffer RGB565 completo precisaria de 115.200 bytes. Por isso usamos faixas, hashes por linha e transferências agrupadas. A substituição não é uma troca atômica de framebuffer: uma mudança grande pode aparecer progressivamente durante a transferência SPI. O fundo recomendado é **preto puro `#000000`**; a resolução nunca é reduzida. No exemplo, ficam 5 px livres no topo e 10 px na base.

## Compilar

Requisitos: Python 3.11+ e acesso à internet na primeira instalação das ferramentas. Não é necessário Poetry nem o aplicativo de agenda.

```sh
git clone https://github.com/bratao/geekmagic-smalltv-scene-firmware.git
cd geekmagic-smalltv-scene-firmware
python -m venv .venv
```

Ative o ambiente:

```powershell
# Windows PowerShell
.\.venv\Scripts\Activate.ps1
```

```sh
# Linux / macOS
source .venv/bin/activate
```

Instale as versões fixadas e compile:

```sh
python -m pip install -r requirements-build.txt
python tools/build.py
```

Saída: `.pio/build/esp12e/firmware.bin` e `firmware.elf`. O script **não grava o dispositivo**. Resolve as dependências, aplica o patch GFX com verificação SHA256, gera as fontes e faz build limpo. Não ignore o patch executando apenas `pio run` em uma instalação nova: isso perderia a economia de RAM.

Versões: PlatformIO 6.2.0; plataforma espressif8266 4.2.1; Arduino Core 3.1.2; ArduinoJson 7.4.3; Arduino_GFX 1.6.7; AnimatedGIF 2.2.3; Pillow 12.3.0. A versão embarcada é fixa em `firmware_version.txt`: `v1.5.0-smalltv-scene1`.

As fontes geradas estão incluídas; o TTF e sua licença estão em `assets/`. Para reativar métricas do PC, altere `SMALLTV_ENABLE_METRICS` e retire `-<dashboard/>` de `build_src_filter`; para o log periódico, altere `SMALLTV_HEAP_LOG`.

## Binário e atualização

[**Baixar o firmware validado**](artifacts/firmware.bin) · [SHA256](artifacts/SHA256SUMS) · [Revisão estrutural](docs/binary-review.json)

SHA256 do binário testado:

```text
8f0a270972885aab13942fa31f8d6b375d5b5f2e7eebf676c470f8c5c8ce0964
```

O binário foi instalado e validado em um SmallTV-Ultra que já executava GeekMagic Open Firmware. **Não é um pacote de migração universal do firmware de fábrica**. Verifique modelo/flash e use o procedimento de migração do upstream quando aplicável.

Para atualizar uma instalação compatível, pare os clientes que enviam conteúdo e envie **somente firmware** por `/api/v1/ota/fw`, mantendo alimentação e rede estáveis:

```sh
curl -H "Authorization: Bearer SEU_TOKEN" \
  -F "file=@artifacts/firmware.bin" \
  http://IP_DA_TV/api/v1/ota/fw
```

Não envie imagem LittleFS nem apague a flash para instalar esta atualização: isso preserva web, configuração e arquivos. Examine o JSON, pois o updater pode retornar HTTP 200 também em falhas. Sucesso deve conter `"status":"Upload successful"` e `"message":"Update OK (...)"`; o dispositivo reinicia depois.

Após o boot, verifique `/api/v1/display/capabilities`, `/api/v1/draw/status`, web e `/api/v1/ota/status`. Guarde seu firmware anterior e um backup adequado antes de modificar outro aparelho. RescueMode/OTA foram mantidos; um dispositivo que não inicializa pode exigir recuperação serial física.

## Usar a API

Documentação completa: **[docs/API.md](docs/API.md)**. Exemplo: **[examples/scene.json](examples/scene.json)**.

```sh
curl -H "Authorization: Bearer SEU_TOKEN" \
  http://IP_DA_TV/api/v1/display/capabilities

curl -H "Authorization: Bearer SEU_TOKEN" \
  -H "Content-Type: application/json" \
  --data-binary @examples/scene.json \
  http://IP_DA_TV/api/v1/draw/batch
```

Troque `epoch` no exemplo pelo Unix timestamp atual se quiser a alternativa ao NTP. `tz_offset` é o deslocamento em segundos. O exemplo desenha uma tela; ele não consulta calendários ou tarefas.

## Validação e limites da evidência

- Revisão do código por agentes e revisão independente do **hash exato** do binário.
- Verificados ambos os cabeçalhos eboot/aplicação, segmentos, XOR e CRC completo, DIO/4 MB e partições. Todas as tabelas YCbCr foram encontradas em flash.
- Mesmo núcleo C++ executado no host com AddressSanitizer: 1.000 quadros e 128 cenas de até 32 nós, limites, UTF-8, canários, margens e viradas de minuto/meia-noite.
- Teste físico: desenhos individuais/batch/cena, rejeições preservando a cena, autenticação, web, NTP e OTA; dez substituições e 30 amostras sem queda de heap ou reinício.
- Primeira composição medida: aproximadamente 111 ms; quadros animados amostrados: até 16,8 ms. Animação e ausência de tela preta confirmadas visualmente.

A simulação foi do renderizador, **não um emulador completo do ESP8266/Wi-Fi/ST7789**. Não comprova watchdog, SPI, fragmentação ou operação indefinida. Consulte [o relatório resumido](docs/VALIDATION.md).

## Estrutura e licença

`src/`, `include/` e `lib/`: firmware; `data/web/`: interface web preservada; `tools/`: build, fontes, patch e validação; `artifacts/`: binário; `examples/`: cena; `docs/`: API, alterações e validação.

GPL-3.0-or-later conforme [LICENSE](LICENSE) do upstream. Adaptações HoloClawd mantêm atribuição e [licença MIT](LICENSE-HoloClawd). Noto Sans: [SIL OFL 1.1](assets/OFL.txt). As alterações contra o commit base estão em [docs/changes.patch](docs/changes.patch); os ajustes de empacotamento/build deste repositório estão nos próprios arquivos.
