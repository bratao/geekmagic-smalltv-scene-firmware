# API HTTP — SmallTV Scene Firmware

Base: `http://IP_DA_TV/api/v1`. Exemplos usam placeholders; nunca coloque o token na URL. Requisições JSON usam `Content-Type: application/json`. Todas as rotas de desenho/capacidades/status e as operações protegidas do firmware exigem:

```http
Authorization: Bearer SEU_TOKEN
```

O token é o configurado na interface web do seu dispositivo. A API é HTTP na rede local. Não exponha diretamente a TV à internet.

Current source version: **`v1.5.0-smalltv-scene4-efficient`**. This version adds optional resource capture and reduces report-monitor polling and native clock-animation transfers. Existing Wi-Fi, authentication, and drawing contracts remain unchanged.

## Optional resource capture — scene4-efficient

Collection is disabled after boot and uses fixed RAM counters with no persistent logs. All routes require Bearer authentication:

| Method and route | Behavior |
| --- | --- |
| `POST /diagnostics/resources` | `{"enabled":true,"duration_s":60}` starts a fresh capture; duration is 1–600 seconds and defaults to 60. `{"enabled":false}` stops and preserves results. |
| `GET /diagnostics/resources` | Current/completed loop, native-render, transmitted-pixel, and heap counters. |
| `DELETE /diagnostics/resources` | Stops and clears the capture. |

POST success is `200 {"status":"ok"}`; invalid input is 400 and bodies over 256 bytes are 413. DELETE returns `200 {"status":"reset"}`. Captures stop automatically at the duration limit, checked at loop completion. Starting another capture replaces the previous one. Reboot discards all counters.

Heap is sampled once per second while enabled. Rendering includes native scenes only, and time overlaps loop work. `cooperative_work_pct` is a wall-time activity indicator, **not measured CPU utilization, power, or temperature**. Avoid frequent GET polling during comparisons. Full field definitions, limitations, and usage are in [RESOURCE-DIAGNOSTICS.md](RESOURCE-DIAGNOSTICS.md); numerical comparisons are in [the performance report](PERFORMANCE-scene4-efficient.md).

Clock-colon-only animation now sends cropped RGB565 regions at native resolution. Minute changes retain normal full affected-row redraws. No new drawing payload or animation command is required.

## WiFi profiles — scene2-wifi

All routes in this section require the Bearer header above. Request bodies are JSON, limited to **2,048 bytes**. No profile response includes a password.

### GET /wifi/networks

Returns the saved networks in priority order:

```json
{"networks":[{"ssid":"EXAMPLE_NETWORK","has_password":true}],"max_networks":3}
```

### PUT /wifi/networks

Replaces the complete ordered list, with zero to three unique SSIDs. Saving **does not reconnect**:

```json
{"networks":[
  {"ssid":"EXAMPLE_NETWORK"},
  {"ssid":"SECOND_NETWORK","password":"YOUR_WIFI_PASSWORD"},
  {"ssid":"OPEN_NETWORK","password":""}
]}
```

Omitting `password` preserves the saved password only when the SSID exactly matches an existing profile. A new or renamed SSID must include `password`; an empty string explicitly represents an open network. Duplicate SSIDs and invalid credential lengths are rejected. Success: `200 {"status":"saved"}`. `409` means a connection is in progress. `507` means persistence failed; the previous settings are retained. This is full replacement: leaving a profile out removes it from the saved list.

### GET /wifi/scan

Starts an asynchronous scan when idle, or polls a pending scan. While pending:

```http
HTTP/1.1 202 Accepted
```

```json
{"status":"scanning"}
```

On completion, HTTP 200 returns an array of networks with `ssid`, `rssi` and `enc`. HTTP 409 indicates Wi-Fi is connecting; HTTP 503 indicates a scan error. Poll about once per second with a bounded timeout, rather than issuing concurrent scans. The web page stops polling after 20 pending responses and offers cancel/retry. Cancelling the browser wait does not cancel the radio scan already started on the device.

### POST /wifi/connect

Connect using a saved password without transmitting it again:

```json
{"ssid":"EXAMPLE_NETWORK"}
```

An explicit `password` updates an existing profile or adds a new one if capacity permits; new profiles require it, with `""` allowed for an open network. The network is persisted before connection is scheduled. Success is an acceptance response, **not proof of association**:

```http
HTTP/1.1 202 Accepted
```

```json
{"status":"connecting","message":"Network saved. Connection starts shortly; the device IP may change."}
```

The response is sent before switching Wi-Fi. The browser may lose connectivity; read the new IP on the display and reopen the device there. HTTP 409 indicates Wi-Fi is busy or the three-profile list is full; HTTP 507 indicates a persistence failure. If scheduling fails after saving, the error explicitly says the network was saved and the connection should be retried.

### GET /wifi/status

Returns `connected`, `connecting`, `ap_mode`, `ssid`, `ip` and `saved_networks`. A completed connection or fallback to the device access point should end a client's busy indicator. Avoid treating every disconnected state as an indefinitely pending request.

## Persisted token and browser origin

`GET /token/check` validates the supplied Bearer token. `POST /token/save` accepts `{"token":"YOUR_NEW_TOKEN"}` authenticated with the current token and persists the update to device configuration. Use the new token for later calls after a successful save, including after reboot.

### GET /web/token — automatic local web authorization

The web UI calls this endpoint without Bearer authentication, with `X-SmallTV-Web: 1`, to retrieve `{"token":"CURRENT_SAVED_TOKEN"}`. The endpoint accepts the device's literal local IP Host and same-origin browser context only; it does not provide CORS permission, and the response is non-cacheable. Use the IP shown on the TV rather than an arbitrary hostname. Do not log the response or put it in a URL.

This is an intentional local-network trust model: someone who can open the local device UI can obtain its token. The custom header and origin checks restrict cross-origin browser access, not a trusted-LAN client that constructs HTTP requests. Other control routes continue to require Bearer authentication. Do not expose this interface to untrusted networks.

The UI shares a single pending token request between simultaneous calls, keeps the result in memory and browser storage, and automatically retries a 401 once with a refreshed token. A fresh origin or a page restored through browser history reloads the saved token from the TV. `/token.html` prefills the current token masked; explicit manual changes remain supported. Requests to external origins are rejected before loading or attaching the token. Logs and OTA also obtain current authorization automatically; firmware upload bodies are not automatically replayed.

Updated Wi-Fi/token/logs/OTA pages and required scripts are embedded in scene3-web firmware and override their older LittleFS copies. Install **firmware only** through `/ota/fw`; do not upload a filesystem image for this upgrade. Other filesystem content and saved configuration remain intact.


Recovery limitation: corrupt, nonblank secure NVS is intentionally not overwritten automatically. Token reset cannot persist through `secure.put` while the corruption remains. Keep an appropriate backup and use explicit physical serial recovery to diagnose/repair this case; neither a web token reset nor a firmware-only update guarantees recovery from all storage corruption.

## Asynchronous NTP synchronization

`POST /ntp/sync` now returns HTTP **202** with `{"status":"ok","pending":true}` when synchronization is scheduled. HTTP **503** means the device is offline. The accepted response preserves `status:"ok"` for clients but does not confirm a completed synchronization. Query `GET /ntp/status` later rather than blocking or polling repeatedly in the request path.

## Dois modos de desenho

| Modo | Uso | Atualização |
| --- | --- | --- |
| Tradicional | Comandos individuais ou batch HoloClawd | Desenha diretamente no LCD, sem reter a lista |
| Cena (`mode:"scene"`) | Painéis, relógio, texto e animação | Retém nós; compõe faixas e envia somente linhas diferentes |

Uma cena nova **substitui a lista completa**; não acrescenta nós à anterior. Sua validação e alocação antecedem o commit. O desenho começa no loop após a aceitação HTTP. A validação rejeitada preserva a cena/GIF anterior. Um comando tradicional válido encerra a cena ativa antes de desenhar, para ela não sobrepor o comando depois. Iniciar GIF ou OTA libera o estado da cena.

Na primeira transição de GIF para cena, é útil liberar previamente o decoder preservando os pixels:

```http
POST /api/v1/gif/stop
Content-Type: application/json

{"keep_screen":true}
```

Faça isso uma vez, não antes de cada republicação. Se faltar heap para a cena candidata, a submissão falha sem substituir o estado anterior.

## Capabilities

`GET /display/capabilities`

```json
{"preserve_gif_screen":true,"gif_last_frame_delay":true,"drawing_api":true,"native_scene":true,"patch":"display3"}
```

Verifique `native_scene:true` antes de usar o modo retido. O identificador `patch` identifica este conjunto de extensões, não substitui a versão de firmware.

## Cena nativa — POST /draw/batch

```json
{
  "mode":"scene",
  "bg":"#000000",
  "epoch":1789140000,
  "tz_offset":-10800,
  "commands":[
    {"type":"clock","x":10,"y":5,"font":"clock","color":"#ffffff","period_ms":4000,"min":64,"max":255},
    {"type":"text","x":10,"y":55,"w":220,"font":"heading","color":"#55eaff","text":"Reuniões"},
    {"type":"text","x":10,"y":82,"w":220,"font":"title","text":"Planejamento"},
    {"type":"line","x0":10,"y0":144,"x1":229,"y1":144,"color":"#444444"}
  ]
}
```

Resposta: `200 {"status":"ok","mode":"scene"}`. Isso confirma aceitação; compare `revision` e `rendered_revision` no status para confirmar composição.

### Envelope

| Campo | Obrigatório | Tipo / limite | Padrão / significado |
| --- | --- | --- | --- |
| `mode` | sim | string `scene` | Seleciona este modo |
| `commands` | sim | array de 0..32 objetos | Lista completa, ordem de pintura |
| `epoch` | sim | inteiro sem sinal de 32 bits | Unix timestamp UTC, usado quando NTP não está disponível |
| `tz_offset` | não | inteiro -50400..50400 | 0; segundos somados ao UTC |
| `bg` | não | RGB hexadecimal | `#000000` |

Corpo máximo: **6.144 bytes**; texto acumulado **1.536 bytes UTF-8**, cada texto até **512 bytes**. Coordenadas inteiras -480..480; dimensões e raios 0..480. A área visível é 0..239 nos dois eixos. Desenho fora dela é recortado. Evite valores extremos para reduzir trabalho desnecessário.

### Campos comuns dos nós

| Campo | Tipo / limite | Padrão |
| --- | --- | --- |
| `type` | um tipo da tabela seguinte | obrigatório |
| `x`,`y` | coordenadas inteiras | 0,0 |
| `color` | seis dígitos RGB, `#` opcional | branco |
| `fill` | booleano | true |
| `pulse` | booleano | false; anima a intensidade da cor |
| `period_ms` | inteiro 1000..60000 | 4000 |
| `min`,`max` | inteiros 0..255, min ≤ max | 128,240 |

As cores viram RGB565 na saída. `pulse` usa uma curva de 40 níveis, verificada a cada 100 ms. Períodos longos repetem níveis; períodos curtos podem saltar alguns. O relógio anima os dois-pontos mesmo sem `pulse:true`; os dígitos permanecem fixos até mudar o minuto. O ciclo de 4 segundos com 64..255 mostrou boa visibilidade no aparelho testado.

### Tipos e geometria

| `type` | Campos específicos | Comportamento |
| --- | --- | --- |
| `text` | `text` obrigatório, `font`, `w` | Uma linha UTF-8, recorte e reticências |
| `clock` | `font`, `w`, parâmetros de pulso | HH:MM calculado localmente |
| `rect` | `x,y,w,h,fill` | Retângulo |
| `roundrect` | `x,y,w,h,r,fill` | Raio limitado à metade da menor dimensão |
| `circle` | `x,y,r,fill` | Centro e raio |
| `ellipse` | `x,y,rx,ry,fill` | Centro e raios; `rx` também aceita `r` como alternativa |
| `line` | `x0,y0,x1,y1` | Segmento; `x,y` podem substituir x0,y0 |
| `triangle` | `x0,y0,x1,y1,x2,y2,fill` | Triângulo; x,y como alternativa para primeiro vértice |
| `pixel` | `x,y` | Um pixel |

Na cena, `w` padrão é 220; `h`, raios e coordenadas restantes são 0. Especifique a geometria para formas. `clear` não é nó de cena: use `bg` e uma lista vazia para substituir por uma tela lisa.

### Fontes e texto

| `font` | Tamanho nominal | Peso |
| --- | ---: | --- |
| `small` | 15 px | regular |
| `meta` | 16 px | regular |
| `heading` | 18 px | bold |
| `title` | 22 px | bold |
| `clock` | 41 px | bold, subconjunto numérico |

`text` usa `small` por padrão; `clock` usa `clock`. A posição y é a referência superior tipográfica adotada pelo renderizador; acentos podem se estender alguns pixels acima dela. A fonte de relógio contém dígitos, espaço, dois-pontos e `?`; use as demais para títulos.

Texto é uma linha, não HTML/SVG/Markdown. Glifos incluídos: ASCII, Latin-1 e pontuação selecionada, incluindo português. Caracteres não disponíveis viram `?`. Não há carregamento de fontes, imagens externas ou execução de scripts. Strings com NUL são rejeitadas nos campos de cena. A largura `w` limita inclusive a tinta do glifo.

### Tempo e atualização

NTP controla a hora UTC quando válida. Sem ele, o relógio usa `epoch + tempo decorrido`, com `tz_offset` aplicado na exibição. Nessa alternativa, o epoch inteiro e o transporte podem introduzir atraso; não há promessa de segundo exato. O contador suporta rollover de `millis()`.

Mudança de minuto é verificada em cada loop, antes da limitação de 100 ms do pulso. Não exige novo JSON. O deslocamento de fuso é fixo: alterações sazonais devem ser enviadas pelo cliente. Data e dia da semana desenhados como texto continuam estáticos até uma nova cena.

## Desenho tradicional

Todos são `POST /draw/<tipo>`. Corpo máximo **8.192 bytes**; batch até **64 comandos**; texto até **512 bytes**, `size` inteiro 1..8; geometria dentro dos mesmos limites -480..480 e 0..480. Use tipos corretos e valores explícitos. O modo tradicional usa a fonte interna da biblioteca, não Noto Sans.

| Rota | Exemplo de corpo |
| --- | --- |
| `/draw/clear` | `{"color":"#000000"}` |
| `/draw/text` | `{"x":10,"y":10,"text":"Hello","size":2,"color":"#ffffff","bg":"#000000"}` |
| `/draw/rect` | `{"x":10,"y":40,"w":50,"h":30,"color":"#ff0000","fill":true}` |
| `/draw/circle` | `{"x":120,"y":120,"r":30,"color":"#00ff00","fill":false}` |
| `/draw/line` | `{"x0":10,"y0":10,"x1":229,"y1":229,"color":"#0000ff"}` |
| `/draw/pixel` | `{"x":120,"y":120,"color":"#ffffff"}` |
| `/draw/triangle` | `{"x0":120,"y0":20,"x1":50,"y1":150,"x2":190,"y2":150,"fill":true}` |
| `/draw/ellipse` | `{"x":120,"y":120,"rx":50,"ry":30,"fill":false}` |
| `/draw/roundrect` | `{"x":10,"y":10,"w":100,"h":50,"r":10,"fill":true}` |

Sucesso individual: `200 {"status":"ok"}`. Clear individual admite corpo vazio e usa preto. No batch, **especifique a cor do clear**: o caminho compartilhado usa branco quando `color` falta.

Batch tradicional, sem `mode:"scene"`:

```json
{"commands":[
  {"type":"rect","x":10,"y":10,"w":80,"h":30,"color":"#003355","fill":true},
  {"type":"text","x":15,"y":15,"text":"Hello","size":2,"color":"#ffffff","bg":"#003355"}
]}
```

Resposta: `200 {"status":"ok","processed":2}`. A validação ocorre antes de desenhar; a execução é sequencial e direta, portanto não há composição atômica de todo esse batch. Para painéis animados use o modo de cena.

## Diagnóstico — GET /draw/status

The JSON below is a historical scene1 example. In scene4-efficient, `state_bytes` is 7,536 and two cumulative native-scene fields are added: `pixels_drawn` (submitted pixels) and `spi_transfers` (bitmap transfer calls). They reset when scene state is freed, and their 32-bit values wrap after 4,294,967,295; use bounded deltas rather than assuming indefinite monotonicity. Optional capture counters are separate.

```json
{"active":true,"free_heap":22024,"max_free_block":16448,"reset_reason":"Software/System restart",
 "uptime_ms":311123,"heap_fragmentation":24,"state_bytes":7528,"max_nodes":32,"text_capacity":1536,
 "nodes":12,"text_bytes":104,"frames":2116,"rows_drawn":28934,"rows_skipped":58306,
 "last_render_us":12884,"revision":13,"rendered_revision":13,"max_render_us":110792,"last_pixels":3360}
```

Os números acima são exemplo, não valores garantidos. `free_heap` e `max_free_block` são bytes; fragmentação é percentual. `last_render_us`/`max_render_us` são microssegundos de composição/envio; `last_pixels` conta pixels enviados na última passagem. `frames` conta passagens com faixas marcadas, não necessariamente pixels diferentes. `rows_skipped` conta linhas cujo hash não mudou dentro das faixas examinadas. Uptime é `millis()` e volta a zero em rollover/reboot. Revisões são locais ao estado da cena, reiniciando quando esse estado é liberado.

Não faça polling por quadro em produção; o renderer já anima localmente. A resposta de status também aloca memória temporária, portanto não mede o mínimo de heap interno do parser.

## GIF e preservação de tela

`POST /gif/play`:

```json
{"name":"example.gif","keep_screen":true}
```

`POST /gif/stop`:

```json
{"keep_screen":true}
```

O padrão de `keep_screen` é false, preservando comportamento upstream. Stop responde `{"status":"stopped"}`; play responde `{"status":"playing","file":"..."}` conforme implementação. Ao usar true, o LCD mantém os pixels até o novo conteúdo chegar. O último quadro agora respeita seu delay antes de reiniciar o loop.

## Demais rotas preservadas

Estas são operações upstream; veja os handlers em `src/web/Api.cpp` para respostas completas.

| Método e rota | Uso / corpo principal |
| --- | --- |
| `GET /wifi/scan` | Async scan: 202 pending, 200 array; see Wi-Fi section |
| `GET /wifi/status` | Connection/AP state and saved profile count |
| `GET /wifi/networks` | Saved ordered profiles, no passwords |
| `PUT /wifi/networks` | Replace up to three profiles without reconnecting |
| `POST /wifi/connect` | Connect saved SSID; 202 before switching Wi-Fi |
| `GET /ntp/status` | Estado e última sincronização |
| `GET /ntp/config` | Servidor NTP |
| `POST /ntp/config` | `{"ntp_server":"pool.ntp.org"}` |
| `POST /ntp/sync` | Async: 202 `status:ok,pending:true`; 503 offline |
| `GET /display/rotation` | Rotação atual |
| `POST /display/rotation` | `{"rotation":0}`; siga valores aceitos pelo handler |
| `POST /reboot` | Reiniciar dispositivo |
| `GET /gif` | Listar GIFs |
| `POST /gif` | Upload multipart de GIF |
| `DELETE /gif` | `{"name":"example.gif"}` |
| `GET /web/token` | Automatic same-origin local-IP web bootstrap; requires `X-SmallTV-Web: 1` |
| `GET /token/check` | Verificar token |
| `POST /token/save` | Persist token, authenticated with the current token |
| `GET /logs` | Logs recentes |
| `GET /logs/download` | Download dos logs |
| `POST /logs/clear` | Limpar buffer de logs |
| `GET /ota/status` | Estado do updater |
| `POST /ota/fw` | Upload multipart do firmware |
| `POST /ota/fs` | Upload do filesystem; não usar para esta atualização |
| `POST /ota/cancel` | Solicitar cancelamento do updater |

A web permanece na raiz `/`. O fallback `/legacyupdate` é disponibilizado pelo upstream quando LittleFS está indisponível/vazio; não é uma rota garantida em todo boot normal. RescueMode permanece separado do fluxo de cena.

## Erros e limites operacionais

- `401`: autenticação rejeitada.
- `400`: JSON, envelope ou comando inválido. Na submissão de cena, falha de alocação também é retornada como 400 com mensagem `scene allocation failed` ou `renderer allocation failed`; o cliente pode tentar novamente após liberar recursos.
- `413`: corpo acima do limite da rota.
- A validação de drawing rejeita tipos desconhecidos, coordenadas/tamanhos fora dos limites e cores inválidas. Ela não é um schema JSON completo: campos desconhecidos podem ser ignorados.
- Um batch tradicional validado ainda pode falhar por memória em seu parsing de execução; prefira cenas compactas e não envie requisições concorrentes desnecessárias.
- OTA pode responder **HTTP 200 com erro no JSON**. Para sucesso, verifique `status:"Upload successful"` e a mensagem de conclusão; não use apenas o status HTTP.
- O parser do webserver recebe o corpo antes da validação da rota: os limites reduzem a carga aceita pelo renderer, mas não devem ser tratados como proteção contra tráfego hostil ilimitado.

