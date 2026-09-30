# Motor JavaScript do CelerOS - Manual de Referência Completo

[English](JS_API_Guide.md) | **Português (BR)**

Bem-vindo à **Referência da API JavaScript do CelerOS**. Este documento traz
os detalhes técnicos da especificação do motor JavaScript, características
de desempenho e toda API nativa exposta pelo kernel C++ para interagir com o
hardware do ESP32.

---
## Versão do Runtime JS do CelerOS
### Runtime JS: v1.0.0
### Nível de API: 9
---

## 1. Especificações do Motor e Compatibilidade ECMAScript

**O runtime JavaScript do CelerOS** usa o **Duktape 2.x**

### 1.1 Compatibilidade ECMAScript
- **Conforme ES5 / ES5.1:** o motor é totalmente compatível com a
  especificação ECMAScript 5.1.
- **Suporte parcial a ES6 (ES2015):** suporta built-ins modernos como
  `TypedArrays` (Uint8Array, Int32Array, etc.), `Proxy` e `Reflect`.
- **Sintaxe moderna NÃO suportada:** por priorizar memória ultra baixa, os
  açúcares sintáticos modernos não estão disponíveis. Você não pode usar:
  - Arrow functions `() => {}`
  - `let` e `const` (use `var`)
  - `class` do ES6 (use herança prototipal tradicional)
  - Template literals `` `string ${var}` ``
  - `Promise` (o built-in foi removido da compilação para poupar flash — use
    as chamadas bloqueantes, ex.: `Net.get`)

### 1.2 Limites de Memória e Desempenho
- **Estratégia de execução:** bytecode compilado nativamente e executado por
  uma máquina virtual de pilha.
- **Coleta de lixo (GC):** contagem de referências libera a maioria dos
  objetos imediatamente; um mark-and-sweep completo (para ciclos) roda a
  partir do `System.delay(ms)` no máximo uma vez por segundo, ou na hora
  quando o heap está apertado. O tempo gasto coletando é descontado do delay
  pedido.
- **Heap máximo:** ~90KB de RAM livre utilizável por script (com WiFi
  desligado). Minimize sempre alocações dinâmicas de arrays em loops de
  animação de alta velocidade.

---

## 2. Objeto Global: `System`

O objeto `System` oferece bindings de baixo nível acelerados por hardware
para o SO do ESP32.

### Propriedades de Display

#### `System.screenWidth()`
- **Retorna:** `Integer` — sempre `240` (canvas virtual do CelerOS 1.1+).
- **Descrição:** largura do canvas de design em que os apps desenham. Em
  painéis maiores o runtime escala tudo para a tela física (veja a nota do
  canvas virtual na seção 3).

#### `System.screenHeight()`
- **Retorna:** `Integer` — sempre `320` (canvas virtual do CelerOS 1.1+).
- **Descrição:** altura do canvas de design em que os apps desenham.

> Hardcodar 240x320 é o padrão esperado: o runtime escala desenho, sprites
> e toque para o painel físico, então o mesmo app renderiza idêntico (e em
> tela cheia) em qualquer placa.

### Utilitários do SO

#### `System.getOSVersion()`
- **Retorna:** `String` (ex.: `"1.2.0"`)
- **Descrição:** devolve a string da versão atual do SO.

#### `System.getAPILevel()`
- **Retorna:** `Integer` (ex.: `5`)
- **Descrição:** devolve o inteiro do Nível de API do SO.

#### `System.millis()`
- **Retorna:** `Integer`
- **Descrição:** devolve o uptime total do hardware ESP32 em milissegundos
  desde o boot. Útil para física com delta-time e temporização de loop.

#### `System.micros()`
- **Retorna:** `Integer`
- **Descrição:** devolve o uptime total do hardware ESP32 em microssegundos
  desde o boot. Essencial para temporização de altíssima resolução (ex.:
  protocolos bit-banged customizados). Note que o inteiro de 32 bits faz
  wrap a cada ~71 minutos.

#### `System.getTemperature()`
- **Retorna:** `Float`
- **Descrição:** lê o sensor de temperatura interno do ESP32 e devolve o
  valor em Celsius.

#### `System.hasTemperatureSensor()`
- **Retorna:** `Boolean`
- **Descrição:** verifica se a revisão do hardware ESP32 instalada suporta o
  sensor de temperatura interno (alguns chips novos removeram). Devolve
  `true` se suportado.

#### `System.delay(ms)`
- **Parâmetros:** `ms` (Integer) - Quantidade de milissegundos para pausar a
  execução.
- **Retorna:** `undefined`
- **Descrição:** pausa a execução do JavaScript. **CRÍTICO:** essa função
  comanda o kernel C++ para executar Garbage Collection em background. Se
  você tem um loop `while(true)`, é OBRIGATÓRIO incluir um `System.delay(10)`
  para evitar que o SO trave por exaustão de heap.

#### `System.delayMicroseconds(us)`
- **Parâmetros:** `us` (Integer) - Quantidade de microssegundos para pausar.
- **Retorna:** `undefined`
- **Descrição:** delays sub-milissegundo de alta precisão, nativamente.
  Bloqueia a CPU de forma limpa, sem disparar Garbage Collection.

#### `System.print(str)`
- **Parâmetros:** `str` (String)
- **Retorna:** `undefined`
- **Descrição:** imprime uma mensagem no monitor serial USB físico num
  computador conectado (baud 115200). Útil para depurar variáveis enquanto a
  tela renderiza frames.

#### `System.getTouch()`
- **Retorna:** `Object` -> `{ x: Integer, y: Integer, touched: Boolean }`
- **Descrição:** consulta o controlador de toque.
  - `touched` é `true` se um dedo/stylus está pressionando a tela.
  - `x` e `y` são coordenadas em pixels. Se `touched` é `false`, `x` e `y`
    valem 0.
- **Gatilho oculto de saída:** se o usuário tocar em `x >= 200` e `y <= 40`
  (canto superior direito), o kernel C++ aborta instantaneamente o motor JS
  e força o fechamento do app para impedir que o usuário fique preso no SO.

#### `System.getInfo()`
- **Retorna:** `Object` -> `{ totalRAM, freeRAM, minFreeRAM, maxAllocRAM, cpuFreqMHz, chipModel, chipCores, chipRevision, flashSize, uptimeMs, appRAM }` (Integers, exceto chipModel: String)
- **Descrição:** devolve um objeto com o estado atual do hardware ESP32,
  incluindo uso de memória, velocidade de CPU e especificações. Útil para
  depurar vazamentos de memória e checar uptime.
  - `minFreeRAM`: o menor valor de RAM livre registrado desde o boot.
  - `maxAllocRAM`: o maior bloco contíguo único que se pode alocar.
  - `appRAM`: heap livre quando o app atual foi aberto (antes de carregar o código; em placas sem PSRAM, RAM interna mais a IRAM acessível a byte para onde o runtime transborda) — quanto de RAM a placa dá a um app. `freeRAM` é medido agora, com o app já carregado. Ausente em firmware antigo.

#### `System.getIPAddress()`
- **Retorna:** String
- **Descrição:** devolve o endereço IP local atual do ESP32 (ex.
  "192.168.1.11") se o WiFi estiver conectado.

#### `System.isWiFiActive()`
- **Retorna:** Boolean
- **Descrição:** devolve `true` se o ESP32 estiver conectado a uma rede WiFi.

#### `System.restart()`
- **Retorna:** None
- **Descrição:** reinicia instantaneamente o hardware ESP32.

#### `System.getTime()`
- **Retorna:** `String` (ex.: `"14:30"` ou `"02:30 PM"`)
- **Descrição:** devolve a hora local atual formatada pelo SO, respeitando
  automaticamente a preferência do usuário (12 ou 24 horas).

#### `System.getSeconds()`
- **Retorna:** `Integer` (0-59)
- **Descrição:** devolve o segundo local atual direto do RTC.

#### `System.getDate()`
- **Retorna:** `String` (ex.: `"15/06/2026"`)
- **Descrição:** devolve a data local atual formatada como DD/MM/AAAA.

#### `System.getYear()`
- **Retorna:** `Integer` (ex.: `2026`)
- **Descrição:** devolve o ano local atual com 4 dígitos.

#### `System.getMonth()`
- **Retorna:** `Integer` (1-12)
- **Descrição:** devolve o mês local atual.

#### `System.getDay()`
- **Retorna:** `Integer` (1-31)
- **Descrição:** devolve o dia local atual do mês.

#### `System.getTimezone()`
- **Retorna:** `String` (ex.: `"UTC-8"`)
- **Descrição:** devolve o offset/fuso horário configurado pelo usuário.

#### `System.prompt(promptMsg, initialText, options)`
- **Parâmetros:**
  - `promptMsg` (String) - Cabeçalho exibido acima do teclado.
  - `initialText` (String) - Texto pré-preenchido na caixa de entrada.
  - `options` (Object, opcional, API level 7) - `{mask: true}` oculta o texto
    digitado atrás de bullets, com botão ver/ocultar ao lado do X (senhas, PINs).
- **Retorna:** `String`
- **Descrição:** suspende completamente a execução do JavaScript e abre o
  teclado QWERTY nativo em C++ em tela cheia (com shift — toque duplo trava o
  caps lock —, duas páginas de símbolos e uma página de acentos PT-BR).
  Quando o usuário toca em "OK", a execução retoma e a string
  digitada é devolvida. Devolve string vazia `""` se o usuário tocar em "X"
  (cancelar).

---

## 3. Pipeline de Desenho do Display

**Frame buffer automático (CelerOS 1.2+, placas com PSRAM):** toda chamada
de desenho vai para um frame off-screen do tamanho do painel, e o frame só
vai para o vidro quando o app *cede*: `System.delay()`, `System.getTouch()`,
`System.prompt()`, e logo antes das chamadas bloqueantes (`Net.*`,
`System.wifiScan/wifiConnect`, `System.otaCheck/otaStart` — também após cada
callback de progresso de OTA —, `FS.copyFile/copyDirectory/removeDirectory`).
Apps que limpam e redesenham a tela inteira a cada evento param de piscar,
sem mudança de código. `System.present()` força o frame para o vidro
(animações ou computações longas que nunca cedem); `System.isBuffered()`
diz se a placa tem o frame (`false` na CYD, onde o desenho vai direto ao
TFT).

> Regra prática: desenhe a tela completa e depois ceda. Algo desenhado logo
> antes de uma chamada C++ longa que não está na lista acima só aparece no
> próximo yield — chame `System.present()` antes.

### Motor de Cores
Cores em JS são sempre inteiros **RGB565 de 16 bits** (ex.: `0xF800` =
vermelho) em qualquer placa — use `System.color(r, g, b)` ou
`System.theme()` para construí-las. (O CelerOS 1.2 corrigiu um bug em que,
em painéis de 16 bits, valores RGB565 passavam como RGB888 e saíam com tom
errado.)

> **Canvas virtual (CelerOS 1.1+):** os apps sempre rodam num **canvas de
> design 240x320**. Em placas com painéis maiores (ex.: SmartDisplay 4"
> 480x480), `System.screenWidth()/screenHeight()` reportam 240/320, todas as
> coordenadas/tamanhos de desenho são escalados para a tela física, sprites
> são alocados no tamanho escalado e `System.getTouch()` devolve coordenadas
> no espaço 240x320 — o mesmo app renderiza idêntico (e em tela cheia) em
> qualquer placa. Cores são RGB565 em todo lugar; o runtime converte para o
> formato nativo do painel.

#### `System.color(r, g, b)`
- **Parâmetros:** `r`, `g`, `b` (Integers 0-255)
- **Retorna:** `Integer` (cor empacotada de 16 bits)
- **Descrição:** empacota valores RGB 8/8/8 de 24 bits no formato RGB 5/6/5
  de 16 bits esperado pelo hardware.

### APIs de Gráficos

#### `System.fillScreen(color)`
- **Parâmetros:** `color` (Integer 16 bits)
- **Descrição:** inunda a tela inteira com uma única cor. Extremamente
  rápido: bypass do loop de pixels usando DMA de SPI direto do hardware.

#### `System.drawPixel(x, y, color)`
- **Parâmetros:** `x` (Int), `y` (Int), `color` (Int 16 bits)
- **Descrição:** renderiza um único pixel.

#### `System.drawLine(x1, y1, x2, y2, color)`
- **Parâmetros:** `x1`, `y1`, `x2`, `y2` (Ints), `color` (Int 16 bits)
- **Descrição:** usa o algoritmo de Bresenham para renderizar uma linha
  reta entre dois pontos.

#### `System.drawRect(x, y, w, h, color)`
#### `System.fillRect(x, y, w, h, color)`
- **Parâmetros:** `x`, `y` (coords do topo-esquerda), `w` (largura), `h`
  (altura), `color` (Int 16 bits)
- **Descrição:** desenha retângulos vazados ou preenchidos.

#### `System.drawRoundRect(x, y, w, h, radius, color)`
#### `System.fillRoundRect(x, y, w, h, radius, color)`
- **Parâmetros:** `x` (Int), `y` (Int), `w` (Int), `h` (Int), `radius` (Int),
  `color` (Int)
- **Descrição:** desenha retângulo com cantos arredondados na cor
  especificada.

#### `System.drawBMP(path, x, y)`
- **Parâmetros:** `path` (String), `x` (Int), `y` (Int)
- **Retorna:** `Boolean` (`true` se ok, `false` se não suportado ou arquivo
  ausente)
- **Descrição:** lê uma imagem `.bmp` de 16, 24 ou 32 bits do sistema de
  arquivos (`/sd/` ou `/local/`) e a transmite em `x, y` sem usar RAM do
  JavaScript. A imagem é escalada pelo canvas virtual (um BMP de 240px de
  largura preenche a largura da tela em qualquer placa).

#### `System.drawPNG(path, x, y)`
- **Parâmetros:** `path` (String), `x` (Int), `y` (Int)
- **Retorna:** `Boolean` (`true` se ok, `false` se falhou caminho/decode)
- **Descrição:** desenha uma imagem `.png` do sistema de arquivos (`/sd/` ou
  `/local/`) em `x, y`, decodificada em streaming linha a linha (sem pico de
  RAM de framebuffer completo; apenas a janela deflate de ~44 KB durante o
  decode). O alpha do PNG é mesclado sobre o fundo existente. Como toda
  chamada de desenho, a imagem é escalada pelo canvas virtual 240x320
  (CelerOS 1.2+; antes, o PNG mantinha o tamanho nativo em painéis grandes).
  PNGs entrelaçados são suportados. Ideal para fundos e fotos; use
  `System.drawIcon()` para ícones 64x64 estilo launcher.

#### `System.drawCircle(x, y, radius, color)`
#### `System.fillCircle(x, y, radius, color)`
- **Parâmetros:** `x`, `y` (coords do centro), `radius` (Int), `color` (Int
  16 bits)
- **Descrição:** renderiza círculos perfeitos, vazados ou preenchidos.

#### `System.drawTriangle(x1, y1, x2, y2, x3, y3, color)`
#### `System.fillTriangle(x1, y1, x2, y2, x3, y3, color)`
- **Parâmetros:** `x1, y1, x2, y2, x3, y3` (coords dos vértices), `color`
  (Int 16 bits)
- **Descrição:** renderiza triângulos vazados ou preenchidos. Útil para
  projeções 3D ou indicadores de UI.

#### `System.drawFastVLine(x, y, h, color)`
- **Parâmetros:** `x, y` (coords de início), `h` (altura), `color` (Int 16
  bits)
- **Descrição:** desenho de linha vertical acelerado por hardware.
  Substancialmente mais rápido que `System.fillRect()` para fatias de
  raycaster.

#### `System.drawFastHLine(x, y, w, color)`
- **Parâmetros:** `x, y` (coords de início), `w` (largura), `color` (Int 16
  bits)
- **Descrição:** desenho de linha horizontal acelerado por hardware.

---

### Double Buffering de Hardware (Mini-Sprites)
O double buffering permite desenhar formas invisivelmente num buffer de RAM
off-screen (um Sprite) e depois "empurrar" o frame completo para a tela
física numa única transferência instantânea de DMA de hardware. Isso
**elimina completamente o flickering de tela**.

> [!CAUTION]
> **Limitações Severas de RAM e Fragmentação de Heap**
> O ESP32-WROOM tem RAM contígua muito limitada (~320KB no total, mas pela
> fragmentação do WiFi/WebManager, o maior bloco alocável costuma ficar
> abaixo de ~30KB). Alocar um buffer massivo (ex.: `240x320` a 16 bits
> ocupa 153,6 KB) fará o motor devolver `false` de `System.createSprite`
> instantaneamente.
> **Sempre** mantenha seus buffers o menor possível. A arquitetura
> recomendada é **Renderização em Fatias**: divida a tela em pequenas fatias
> horizontais (ex.: 10 fatias de 32px de altura) ou colunas verticais.
> `System.createSprite()` agora dispara Garbage Collection agressivo para
> desfragmentar a memória antes da alocação e cai automaticamente para cor
> de 8 bits para evitar travamentos se a RAM estiver muito fragmentada para
> 16 bits.

#### `System.createSprite(width, height)`
- **Parâmetros:** `width, height` (Integer)
- **Retorna:** `Boolean` (`true` se alocou com sucesso um buffer de cor 16
  ou 8 bits na RAM, `false` se a RAM esgotou)
- **Descrição:** aloca um buffer de Sprite persistente off-screen na RAM.
  Força GC automaticamente e cai para 8 bits para garantir memória contígua.

#### `System.bindSprite(enabled)`
- **Parâmetros:** `enabled` (Boolean)
- **Descrição:** se `true`, **TODAS** as chamadas `System.draw...` e
  `System.fill...` subsequentes são interceptadas e desenhadas
  *invisivelmente* no Sprite persistente em vez da tela. Se `false`, volta a
  desenhar direto no TFT.

#### `System.pushSprite(x, y)`
- **Parâmetros:** `x, y` (Integer - coordenadas do topo-esquerda onde colar
  o buffer na tela física)
- **Descrição:** empurra todo o buffer oculto para a tela física
  instantaneamente via DMA. O buffer permanece na RAM e pode ser modificado
  e empurrado de novo.

#### `System.deleteSprite()`
- **Descrição:** destrói o Sprite instantaneamente e libera a RAM. Você
  DEVE chamar isso ao terminar para evitar vazamentos severos de memória!

### APIs de Texto

#### `System.setTextColor(fg_color, bg_color)`
- **Parâmetros:** `fg_color` (frente), `bg_color` (fundo)
- **Descrição:** define as cores ativas de renderização de texto. Com
  `bg_color`, o texto sobrescreve em nível de hardware, apagando os pixels
  anteriores sem precisar desenhar retângulo manual.

#### `System.setTextSize(size)`
- **Parâmetros:** `size` (Integer 1-5)
- **Descrição:** multiplica a escala da fonte pixel padrão.

#### `System.drawString(text, x, y, font)`
- **Parâmetros:**
  - `text` (String) - Texto a renderizar.
  - `x`, `y` (Ints) - Coordenada topo-esquerda onde começa a renderização.
  - `font` (Integer 1, 2 ou 4) - Seleção de fonte de hardware. 2 é padrão, 4
    é negrito/grande.
- **Descrição:** renderiza strings em alta velocidade no display. O texto é
  UTF-8 e todas as fontes cobrem Latin-1 (U+0020..U+00FF), então acentos
  funcionam: `"Configurações"`, `"25°C"`. Caracteres fora dessa faixa
  (travessão, aspas curvas, €, emoji) não são desenhados.

---

## 4. GPIO de Hardware (Entrada/Saída de Propósito Geral)

O CelerOS habilita controle direto dos pinos do microcontrolador ESP32 via
`System.gpio`.

### Constantes
- `System.gpio.INPUT`
- `System.gpio.OUTPUT`
- `System.gpio.INPUT_PULLUP`
- `System.gpio.HIGH`
- `System.gpio.LOW`

### Funções

#### `System.gpio.pinMode(pin, mode)`
- **Parâmetros:** `pin` (número do pino de hardware), `mode` (constante de
  GPIO)
- **Descrição:** define o estado elétrico físico de um pino (ex.: pino 2
  como OUTPUT para acender um LED).

#### `System.gpio.digitalWrite(pin, state)`
- **Parâmetros:** `pin` (Integer), `state` (HIGH ou LOW)
- **Descrição:** aplica 3,3V (HIGH) ou 0V (LOW) num pino específico.

#### `System.gpio.digitalRead(pin)`
- **Parâmetros:** `pin` (Integer)
- **Retorna:** `Integer` (1 para HIGH, 0 para LOW)
- **Descrição:** lê o estado de tensão físico de um pino.

#### `System.gpio.analogRead(pin)`
- **Parâmetros:** `pin` (Integer)
- **Retorna:** `Integer` (0 a 4095)
- **Descrição:** aciona o conversor analógico-digital de 12 bits do ESP32
  para ler um nível de tensão contínuo.

#### `System.gpio.analogWrite(pin, pwmValue)`
- **Parâmetros:** `pin` (Integer), `pwmValue` (0 a 255)
- **Descrição:** inicia um sinal PWM (Pulse Width Modulation) automático de
  hardware num pino. Útil para controle de motores ou dimmer de LEDs.

#### `System.gpio.pulseIn(pin, state, [timeout])`
- **Parâmetros:** `pin` (Integer), `state` (HIGH ou LOW), `timeout` (Integer
  opcional em microssegundos, padrão 1.000.000)
- **Retorna:** `Integer` (duração do pulso em microssegundos, ou 0 se deu
  timeout)
- **Descrição:** **Medição Nativa de Pulsos por Hardware.** Suspende o motor
  JS e delega ao kernel C++ a medição precisa da duração de um pulso de
  hardware de entrada. Isso contorna totalmente a sobrecarga de execução do
  JavaScript, dando precisão absoluta de microssegundos (crucial para ler
  sensores ultrassônicos HC-SR04).

---

## 5. Sistema de Arquivos Unificado (FS)

O objeto global `FS` controla a camada C++ de sistema de arquivos virtual.
Roteia dinamicamente as operações para o cartão SD físico (prefixo `/sd/`)
ou para a Flash Interna de alta velocidade (prefixo `/local/`).

#### `FS.exists(path)`
- **Parâmetros:** `path` (String)
- **Retorna:** `Boolean`
- **Descrição:** valida se um arquivo ou pasta existe fisicamente.

#### `FS.readTextFile(path)`
- **Parâmetros:** `path` (String)
- **Retorna:** `String` (ou `null` se o arquivo não existe)
- **Descrição:** carregador de alta velocidade para RAM. Lê o arquivo
  inteiro num bloco contíguo de String na RAM. Não use em arquivos maiores
  que ~20KB!

#### `FS.writeTextFile(path, content)`
- **Parâmetros:** `path` (String), `content` (String)
- **Retorna:** `Boolean`
- **Descrição:** apaga qualquer arquivo existente e escreve a totalidade de
  `content` no disco.

#### `FS.appendTextFile(path, content)`
- **Parâmetros:** `path` (String), `content` (String)
- **Retorna:** `Boolean`
- **Descrição:** acrescenta a string ao final de um arquivo existente.

#### `FS.deleteFile(path)`
- **Parâmetros:** `path` (String)
- **Retorna:** `Boolean`
- **Descrição:** apaga permanentemente um arquivo da partição.

#### `FS.renameFile(pathFrom, pathTo)`
- **Parâmetros:** `pathFrom` (String), `pathTo` (String)
- **Retorna:** `Boolean`
- **Descrição:** renomeia um arquivo ou o move entre diretórios na mesma
  partição.

#### `FS.listDir(path)`
- **Parâmetros:** `path` (String)
- **Retorna:** `Array[String]`
- **Descrição:** percorre um diretório e devolve um array de caminhos
  absolutos (ex.: `["/local/app.js"]`).

#### `FS.mkdir(path)`
- **Parâmetros:** `path` (String)
- **Retorna:** `Boolean`
- **Descrição:** cria um novo diretório.

#### `FS.rmdir(path)`
- **Parâmetros:** `path` (String)
- **Retorna:** `Boolean`
- **Descrição:** remove um diretório vazio.

#### `FS.isDirectory(path)` / `FS.isFile(path)`
- **Parâmetros:** `path` (String)
- **Retorna:** `Boolean`
- **Descrição:** avalia se o caminho alvo é diretório ou arquivo.

#### `FS.getFileSize(path)`
- **Parâmetros:** `path` (String)
- **Retorna:** `Integer` (bytes)
- **Descrição:** devolve o tamanho físico total de um arquivo em bytes.

#### `FS.getTotalSpace(drive)` / `FS.getUsedSpace(drive)` / `FS.getFreeSpace(drive)`
- **Parâmetros:** `drive` (String - `"/local"` ou `"/sd"`)
- **Retorna:** `Integer` (bytes)
- **Descrição:** devolve métricas exatas de armazenamento da partição
  indicada.

#### `FS.getFileMD5(path)`
- **Parâmetros:** `path` (String)
- **Retorna:** `String` (representação hexadecimal do hash MD5)
- **Descrição:** usa o motor criptográfico `mbedtls` acelerado por hardware
  para processar o arquivo em streaming e devolver seu hash MD5 preciso.

#### `FS.mountSD()` / `FS.unmountSD()`
- **Parâmetros:** None
- **Retorna:** `Boolean` (mount devolve o status de sucesso)
- **Descrição:** dispara um remount/unmount SPI do cartão SD físico.

---
**Use os Apps e Games do catálogo do CelerOS Hub como exemplo:
https://os.celer.tec.br/store — veja também `data/apps/` neste repositório**
---
*Versão do Documento: 1.1 (Construído para o Ambiente JavaScript do CelerOS)*

## 6. Rede: `Net` (Nível de API 2)

Cliente HTTP para apps JS. As chamadas são **bloqueantes** (o script espera
a resposta, timeout de 10s). HTTP e HTTPS são suportados; respostas maiores
que 32 KB são truncadas.

> Nota de memória: HTTPS (TLS) consome ~45 KB de heap durante a chamada e
> divide a memória com o runtime JS (~90 KB) — mantenha payloads pequenos,
> especialmente em placas sem PSRAM.

#### `Net.isConnected()`
- **Retorna:** Boolean
- **Descrição:** devolve `true` se o WiFi estiver conectado.

#### `Net.get(url)`
- **Parâmetros:** `url` (String, `http://` ou `https://`)
- **Retorna:** String (corpo da resposta) ou `null` em falha (DNS, timeout,
  status HTTP fora de 2xx).
- **Descrição:** executa um HTTP GET. Segue redirects. Lança erro se o WiFi
  não estiver conectado.

#### `Net.getJSON(url)`
- **Parâmetros:** `url` (String)
- **Retorna:** objeto/array JS parseado, ou `null` em falha da requisição.
- **Descrição:** como `Net.get()`, mas parseia o corpo como JSON. Um corpo
  JSON malformado lança um erro visível no script.

#### `Net.post(url, body, contentType)`
- **Parâmetros:**
  - `url` (String)
  - `body` (String) — corpo da requisição
  - `contentType` (String, opcional — padrão `"text/plain"`, ex.:
    `"application/json"`)
- **Retorna:** String (corpo da resposta) ou `null` em falha.
- **Descrição:** executa um HTTP POST. Lança erro se o WiFi não estiver
  conectado.

#### `Net.download(url, caminho, onProgress)` (API 6)
- **Parâmetros:**
  - `url` (String, `http://` ou `https://`)
  - `caminho` (String) — caminho de destino no VFS (ex.
    `"/local/apps/<pkg>/main.js.new"`)
  - `onProgress` (Função, opcional) — chamada por chunk com
    `(bytesBaixados, bytesTotais)`; `bytesTotais` é `-1` quando o servidor
    não manda `Content-Length`
- **Retorna:** Boolean — `true` no sucesso; em falha o arquivo parcial é
  removido
- **Descrição:** baixa direto para um arquivo em modo **streaming** — o
  corpo nunca passa pela heap do JS, então **não sofre o teto de 32 KB** (é
  o mecanismo da App Store para instalar/atualizar apps; os limites de
  tamanho ficam no hub). Erros dentro de `onProgress` não abortam o
  download. É assim que a loja atualiza apps: grava num arquivo de staging
  `*.new`, valida o `FS.getFileMD5()` contra o checksum do catálogo e então
  `FS.renameFile()` por cima do código antigo (atômico dentro do mesmo
  filesystem — nunca renomeie entre `/local` ↔ `/sd`).

### Exemplo

```javascript
var quote = Net.getJSON("http://economia.awesomeapi.com.br/json/last/USD-BRL");
if (quote === null) {
    System.print("request failed");
} else {
    System.print("USD/BRL: " + quote.USDBRL.bid);
}
```

## 12. Nível de API 3 — Apps de Sistema (W8)

Introduzido com o rework W8: as telas de sistema (Settings, App Store,
Installer, Help, Web Server) agora são apps JS que vivem no LittleFS. O
nível 3 adiciona os bindings de que eles precisam — cores de tema, cópia de
arquivos, controle de OTA e o formato de pacote de app.

### 12.1 Tema e Ícones

#### `System.theme()`
- **Retorna:** Object `{bg, card, raised, stroke, accent, accentD, onAccent, text, textDim, ok, warn, err}` — a paleta de tema do SO como valores RGB565, prontos para qualquer chamada de desenho. Os apps de sistema usam isso para herdar o visual do CelerOS em qualquer placa.

#### `System.textWidth(str, font)`
- **Parâmetros:** `str` (String), `font` (Number, padrão 2)
- **Retorna:** Number — largura da string em pixels no espaço virtual
  240x320. `System.drawString` usa datum topo-esquerda; centralize na mão:
  `x = 120 - (System.textWidth(s, 2) >> 1)`.

#### `System.drawIcon(name, x, y)`
- **Parâmetros:** `name` (String: `appstore`, `installer`, `settings`,
  `help`, `web`, `time`, `about`, `update`, `app`, `wifi_on`, `wifi_off`,
  `terminal`, `calculator`, `snake` — ou um caminho absoluto
  `/local`/`/sd`), posição no espaço virtual
- **Descrição:** desenha um ícone 64x64 de `/local/icons/<name>.png` com
  mesclagem de alpha (`.bin` legado aceito).

### 12.2 Ciclo de Vida do App

#### `System.exitApp()`
Fecha o app e volta ao launcher (igual a tocar o canto superior direito).

#### `System.rescanApps()`
Pede ao launcher para reescanear `/local/apps` e `/sd/apps`. Chame depois de
instalar/remover apps.

#### `System.openWifiSetup()`
Empurra a tela nativa de configuração de WiFi. Como os apps JS rodam de
forma síncrona, chame `System.exitApp()` logo em seguida — a tela de setup
assume quando o script cede.

#### `System.present()`
Mostra o frame buffer automático no vidro agora (no-op sem ele). Só é
necessário em loops que nunca chamam `delay()`/`getTouch()`.

#### `System.fontHeight(font)`
Altura em pixels (canvas virtual) de uma fonte numérica (1/2/4) como
renderizada nesta placa — use `y - (System.fontHeight(f) >> 1)` para
centralizar texto verticalmente.

#### `System.setClip(x, y, w, h)` / `System.clearClip()`
Restringe o desenho a um retângulo do canvas virtual (o que ficar fora é
descartado) — ex.: uma lista rolante cujas linhas parciais não podem
pintar sobre o cabeçalho. `clearClip()` restaura a tela inteira. O clip é
zerado quando um app inicia.

#### `System.isBuffered()`
Devolve `true` quando a placa tem o frame buffer automático (placas com
PSRAM).

### 12.3 Hardware e Sistema

#### `System.setBrightness(level)` / `System.getBrightness()` / `System.backlightSupported()`
Controle do backlight (5–100). `setBrightness` persiste em
`/local/brightness.txt`. Em placas sem backlight PWM,
`backlightSupported()` devolve `false` e os setters são no-op.

#### `System.setAutoBrightness(on)` / `System.getAutoBrightness()` (API 7)
Brilho automático pelo sensor de luz da placa: o nível escolhido pelo usuário
vira o máximo e a tela escurece até 30% dele no escuro (suavizado, checado 1x
por segundo, inclusive com app aberto). `getAutoBrightness()` devolve
`true`/`false`, ou `null` em placas sem sensor de luz. Persistido na
configuração `auto_brightness`.

#### `System.wifiStatus()`
- **Retorna:** `{connected, ip, webServer, savedNetworks}` (Booleans/String).

#### `System.webActive()` / `System.webSetActive(bool)`
Estado e toggle do servidor web (file manager + upload web) — ao vivo, sem
reboot.

#### `System.md5(str)`
- **Retorna:** MD5 em hex minúsculo da string (mesmo formato de
  `FS.getFileMD5`). Mantido para dados legados — **não use para senhas**
  (veja `setPin` abaixo).

#### Permissões do app (`app.json` → runtime, F4)
`"permissions": ["fs","net","gpio","system"]` controla o que o runtime registra para o app: sem `fs` não existe objeto `FS`, sem `net` não existe `Net`, sem `gpio` não existe `System.gpio` e sem `system` as chamadas que afetam o aparelho (`restart`, `factoryReset`, `otaCheck/otaStart`, `openWifiSetup`, `web*`, `wifiConnect`) ficam ausentes. **App sem o campo mantém tudo** (compatibilidade com a loja existente); apps de sistema (`"system": true`) sempre recebem tudo. `FS.appData()` devolve a pasta privada do app `/local/data/<packageName>/` (criada na primeira chamada) — use para recordes e estado em vez de arquivos soltos em `/local`.

#### `System.toast(mensagem)` / `System.beep(freq, ms)`
`toast` enfileira notificação do sistema (aparece na hora com a UI viva — `CELEROS_APP_TASK` — ou quando o app sai). `beep` toca um tom (onda quadrada) na saída de alto-falante da placa (bloqueante; 20–20000 Hz, até 5000 ms). A CYD aciona o conector de alto-falante (GPIO26, amplificador na placa); devolve `false` em placa sem alto-falante (SmartDisplay).

#### `System.led(r, g, b)` (API 7)
Acende o LED RGB de status da placa, 0–255 por canal (PWM). `System.led()`
ou `System.led(0, 0, 0)` apaga; o LED também apaga quando o app sai. Devolve
`false` em placa sem LED. A CYD tem um no verso (R=GPIO4, G=GPIO16,
B=GPIO17).

#### `System.lightLevel()` (API 7)
Luz ambiente pelo sensor da placa: `0` (escuro) a `100` (sala iluminada);
`-1` sem sensor. Na CYD o sensor (LDR ao lado da tela) só separa "iluminado"
de "escurecendo": qualquer sala normalmente iluminada lê perto de 100.

`System.getInfo()` também informa `hasLed`, `hasLightSensor` e `hasSpeaker`
para detecção de recursos.

#### `System.setting(key)` / `System.setting(key, value)`
Configurações do sistema em NVS (`web_on`, `nowifi`, `install_sd`, `brightness`, ...). Leitura devolve a string ou `null`; escrita devolve `true`. Apps de sistema usam isto em vez de arquivos `/local/*.txt` soltos (arquivos legados são importados e removidos no primeiro boot).

#### `System.setPin(pin)` / `System.verifyPin(pin)` / `System.pinClear()` / `System.pinState()`
PIN do Settings, tratado nativamente desde a 1.3: SHA-256 com salt
(`settings_pin2.bin`), sem hash exposto ao JS. `setPin` aceita 4–6 dígitos;
`verifyPin` faz upgrade transparente de um PIN MD5 legado no primeiro
sucesso. `pinState()` retorna `0` (sem PIN), `1` (ativo) ou `2` (corrompido —
flag setada sem arquivo; a UI deve pedir redefinição).

#### `System.webAuthInfo()` / `System.webAuthSetPass(senha)`
Credenciais do servidor web (Basic Auth desde a 1.3 — toda rota exige a
senha). `webAuthInfo()` → `{user, pass}` para exibição ao dono do aparelho;
`webAuthSetPass` aceita 6–31 caracteres.

#### `System.otaCheck()`
- **Retorna:** `{fetchFailed, available, hasFirmware, version, url, changelog, guide, type}` — resultado do manifest do canal de updates do aparelho.

#### `System.otaStart(url, progressCallback)`
- **Parâmetros:** `url` vinda de `otaCheck()`, callback que recebe `percent`
  (0–100) durante a gravação
- **Retorna:** `{ok, error?}` — grava o slot OTA inativo; em caso de sucesso
  o app deve oferecer `System.restart()`.

#### `System.setTimezone(tz)` / `System.setManualTime(year, month, day, hour, minute)` / `System.set24hFormat(bool)` / `System.get24hFormat()` / `System.setNtpEnabled(bool)` / `System.getNtpEnabled()`
Configurações de horário (persistidas pelo TimeManager).

#### `System.factoryReset(mode)`
- `"configs"` — limpa arquivos de configuração em `/local` e redes WiFi
  salvas, **mantém** apps e ícones.
- `"total"` — formata a partição LittleFS inteira (**apps são apagados**; a
  recuperação exige `tools/flash_data.sh` ou `celerctl apps install`).
  Confirme sempre duas vezes na UI.

#### `Net.beginGet(url)` / `Net.pollGet(handle)` / `Net.cancelGet(handle)` (não-bloqueante)
`beginGet` dispara o GET em task de fundo e devolve um handle (`-1` sem slot livre ou sem WiFi). `pollGet` devolve `null` enquanto roda e depois `{done:true, ok, status, body, error}` (corpo limitado a 32 KB, igual às chamadas bloqueantes). `cancelGet` abandona a requisição (o slot se libera quando a task estoura o timeout; tasks nunca são mortas no meio do TLS). Máximo de 2 requisições concorrentes.

### 12.4 WiFi (Net)

- `Net.wifiScan()` → array `[{ssid, rssi, secure}]` (bloqueante, ~2s).
- `Net.wifiConnect(ssid, password)` → Boolean (bloqueante, até 15s; salva as
  credenciais).
- `Net.wifiDisconnect()` — desconecta o STA, mantém redes salvas.

### 12.5 Cópia de arquivos (FS)

- `FS.copyFile(src, dst)` → Boolean — binário seguro.
- `FS.copyDirectory(srcDir, dstDir)` → Boolean — cópia recursiva.
- `FS.removeDirectory(path)` → Boolean — exclusão **recursiva** (diferente
  de `FS.rmdir`, que exige diretório vazio).

### 12.6 Pacote de app (app.json)

```json
{
  "name": "Meu App",
  "packageName": "celeros.meuapp",
  "version": "1.0.0",
  "author": "voce",
  "description": "...",
  "type": "Utility",
  "category": "Utility",
  "api": 3,
  "topbar": true,
  "system": true,
  "order": 30,
  "icon": "settings"
}
```

- `system: true` — app de sistema: ordenado primeiro na grade do launcher
  (as telas de sistema nativas agora são apps assim).
- `order` — posição entre os apps de sistema.
- `icon` — nome do ícone em `/local/icons`. **Preferível:** embarque um
  `icon.png` (64x64 com alpha; decodificado no carregamento — CelerOS 1.2+)
  dentro da pasta do app — ele sobrepõe o nome e viaja com o pacote quando
  instalado via SD/celerctl. O `icon.bin` legado (v2 RGB565+A4) ainda é
  aceito.
- `packageName` — identidade usada pelo dedup do launcher, installer e App
  Store.
- Caminhos de instalação: `/local/apps/<Nome>/` (LittleFS) ou
  `/sd/apps/<Nome>/` (cartão SD). Reinstale/atualize com
  `celerctl apps install <pasta> [--sd]`.

---

## 13. Nível de API 5 — Teclado Acoplado (W9)

Introduzido com os apps W9 (Terminal, Calculator, Snake): uma **sessão de
teclado não bloqueante** que o app controla no próprio loop, então a entrada
de texto pode conviver com a UI do app (linha de input de shell, campos de
chat, formulários...). `System.prompt()` (seção 2) continua sendo a escolha
certa para diálogos simples de uso único.

O teclado renderiza no **mesmo alvo do app** (sprite do app > frame PSRAM >
display), então sobrevive a `present()` e compõe com o frame buffer
automático em qualquer placa. Com `field: false` o teclado é desenhado como
um bloco compacto ancorado no rodapé da tela; tudo acima de
`System.keypadRect().y` pertence ao app.

### 13.1 API de Sessão

#### `System.keypadOpen(options)` → Boolean
- **Parâmetros:** `options` (Object, opcional): `{title, initial, maxLen, field, mask}`.
  - `title` (String) — rótulo do cabeçalho (exibido só com campo).
  - `initial` (String) — texto pré-preenchido.
  - `maxLen` (Number, padrão 64, máx 256) — limite do buffer.
  - `field` (Boolean, padrão true) — desenha o campo de input nativo + botão
    X em cima. `field: false` desenha o teclado puro acoplado no rodapé (o
    app ecoa a linha por conta própria).
  - `mask` (Boolean, padrão false, API level 7) — oculta o texto do campo
    atrás de bullets, com botão ver/ocultar ao lado do X.
- **Retorna:** `false` se já há uma sessão aberta (uma por vez) ou não há
  display.
- **Descrição:** abre a sessão de teclado e a desenha imediatamente. Enter
  (OK) **não** fecha a sessão: limpa o buffer e mantém o teclado aberto —
  ideal para UIs linha a linha. A sessão termina no `X` (só com
  `field: true`, reportado como evento `cancel`) ou com
  `System.keypadClose()`.

#### `System.keypadPoll()` → Object|null
- **Retorna:** `null` quando nada aconteceu, ou um objeto de evento por
  chamada:
  - `{type: "change"}` — o buffer mudou (tecla/backspace/espaço); leia com
    `System.keypadText()`.
  - `{type: "enter", text: "..."}` — OK pressionado; `text` é a linha, o
    buffer é limpo depois.
  - `{type: "cancel"}` — X pressionado; a sessão **se fechou sozinha**
    (redesenhe sua tela, o teclado sumiu).
- **Descrição:** bombeia o toque para o teclado, redesenha as teclas quando
  necessário (feedback de pressionamento, páginas de layout, cursor piscando)
  e reporta no máximo um evento por chamada. O canto de saída do SO no topo
  direito continua funcionando com o teclado aberto. Chame a cada iteração
  do loop enquanto a sessão estiver aberta.

#### `System.keypadText()` → String
Buffer de input atual (o mesmo texto que o campo nativo mostraria).

#### `System.keypadRect()` → `{x, y, w, h}`
Área ocupada pelo teclado no canvas virtual 240x320 (`h` é 0 com a sessão
fechada). O app não deve desenhar dentro dela.

#### `System.keypadDraw()`
Re-blita o teclado depois que o app repinta uma região que o sobrepõe (não é
necessário se o app só desenha acima de `keypadRect().y`).

#### `System.keypadClose()`
Fecha a sessão e libera o teclado. O próximo `present()` restaura o frame do
app. Sessões também são fechadas automaticamente quando o app sai.

### 13.2 Exemplo — linha de input acima de um teclado acoplado

```javascript
var T = System.theme();
System.keypadOpen({ field: false, maxLen: 96 });
var kbTop = System.keypadRect().y;   // o app é dono de 0..kbTop

while (true) {
    var ev = System.keypadPoll();
    if (ev && ev.type === "enter") {
        handleLine(ev.text);          // ex.: comando, mensagem, busca...
        redrawScreen();
    } else if (ev && ev.type === "change") {
        redrawInputLine(System.keypadText());
    }
    System.delay(20);                 // GC + present (obrigatorio)
}
```

Implementação de referência: `data/apps/Terminal/main.js` (app W9
pré-instalado).

### 13.3 Constantes globais de cor (correção da API 5)

`BLACK, WHITE, RED, GREEN, BLUE, YELLOW, CYAN, MAGENTA, ORANGE, DARKGREY`
agora são registrados como constantes globais RGB565 (antes do W9 o
registro era um no-op — os apps precisavam de `System.color()`).
`System.color()` e `System.theme()` seguem sendo a forma recomendada de obter
cores.

---

## 14. Topbar do Sistema

Todo app JS roda sob a **topbar do sistema** — uma faixa desenhada pelo core
com o nome do app e o botão **X** de sair à direita. O modo vem do pacote,
não do código:

- **Fixa** — `"topbar": true` no `app.json`: a faixa fica sempre visível e o
  canvas virtual 240x320 mapeia a área abaixo dela (as coordenadas do
  `getTouch` já descontam a faixa). Use em apps estilo sistema, com
  menus/listas/formulários — Settings, App Store, Terminal.
- **Retrátil (padrão)** — sem o campo `topbar`: o app roda em **tela cheia**
  (canvas 1:1 sobre o painel inteiro, como os apps pré-topbar). Deslizar o
  dedo de cima para baixo na borda superior revela a faixa por **3 segundos**;
  todo o gesto de revelar é consumido pelo sistema — o app nunca vê o
  press/release dele. Ideal para jogos (Snake, 2048, Breakout).

Notas:

- No modo fixa, toques na faixa nunca chegam ao app; no retrátil, apenas
  enquanto ela está visível (X incluído — dispara no release, com debounce).
- Em placas com PSRAM a faixa é composta dentro do quadro (sem flicker; ao
  esconder, a área volta atomically no próximo push). Na CYD (desenho direto) a faixa só é
  desenhada quando muda, e o display fica recortado abaixo dela — o app não
  consegue pintar por cima (`System.setClip` é intersectado com essa área); ao
  esconder, ela permanece até o app repintar a região.
- Nada para codificar em nenhum dos modos: o mesmo main.js renderiza
  corretamente nos dois — o campo no `app.json` é o contrato inteiro.

### 14.1 Conteúdo custom (API 6)

Apps podem colocar texto próprio e **chips** tocáveis na faixa:

#### `System.topbarText(texto)`
Substitui o nome do app exibido na faixa (`""` restaura o nome). Redesenha sozinho.

#### `System.topbarButtons(labels)` → Number
Coloca até 3 chips tocáveis na faixa, da direita para a esquerda antes do X (`labels` = array de strings curtas, ex.: `["+", "Limpar"]`). Retorna quantos couberam. `[]` limpa.

#### `System.topbarPop()` → String|null
Consome o toque de chip mais antigo desde a última chamada (FIFO). O chip destaca enquanto pressionado e dispara no release (mesmo contrato com debounce do X).

```javascript
System.topbarText("Toques: 0");
System.topbarButtons(["+", "Limpar"]);
while (true) {
    var id = System.topbarPop();
    if (id === "+") { n += 10; System.topbarText("Toques: " + n); }
    else if (id === "Limpar") { n = 0; System.topbarText("Toques: 0"); }
    System.delay(20);
}
```

No modo retrátil os chips só são tocáveis enquanto a faixa está visível.
Para continuar instalável em firmware antigo, detecte a função
(`typeof System.topbarText === "function"`).

Implementação de referência: `data/apps/Touch Test/main.js`.

---

## 15. Celer Link (API 9)

Link Bluetooth LE entre CelerOS próximos. O caso clássico: uma placa com
CelerOS no robô fazendo `CelerLink.start()`, e o controle vindo de outro
CelerOS (por exemplo o 4848 SmartDisplay) com `scan()`, `connect()` e
`send()` dentro de um app JS.

**Disponibilidade:** só em placas compiladas com Bluetooth
(`CONFIG_CELEROS_BLUETOOTH`; a SmartDisplay tem, a CYD é experimental).
Detecte com `typeof CelerLink !== "undefined"` para continuar instalável em
qualquer firmware.

**Segurança — leia isto:** na v1 **não há nenhuma**. Sem pareamento, sem
criptografia, sem autenticação: qualquer device próximo pode conectar e trocar
mensagens. Ótimo para brinquedos e protótipos; não use para nada sensível.

Modelo: uma conexão por vez, mensagens de até **240 bytes**, melhor esforço.
Quem quer ser controlado chama `start()` (advertising BLE + servidor GATT,
visível como `Celer-XXXX`). O controle escaneia, conecta e troca mensagens.
Os dois sentidos funcionam; o link é simétrico depois de conectado.

#### `CelerLink.start([nome])` → Boolean
Vira controlável: liga o advertising e o servidor GATT. `nome` é o nome BLE
do device (default `Celer-XXXX`, XXXX do fim da MAC do rádio). A primeira
chamada de qualquer função inicializa o Bluetooth (~300 ms).

#### `CelerLink.stop()` → Boolean
Para o advertising (o device deixa de ser descobrível).

#### `CelerLink.scan([timeoutMs])` → Array
Escaneamento bloqueante (default 2500 ms) por CelerOS próximos. Retorna
`[{id: "AA:BB:CC:DD:EE:FF", name: "Celer-9F2A", rssi: -55}]` — sem ordem
garantida, ordene por `rssi` se importar.

#### `CelerLink.connect(idOuNome, [timeoutMs])` → Boolean
Conecta num device do último `scan()`, pelo `id` (MAC) ou pelo `nome`.
Bloqueante (default 4000 ms). Substitui a conexão atual.

#### `CelerLink.disconnect()` → Boolean
Derruba a conexão atual.

#### `CelerLink.send(mensagem)` → Boolean
Envia uma mensagem ao peer conectado (até 240 bytes). **String** vai como
bytes crus; **objeto** é serializado como JSON — comunicação estruturada sem
parser no firmware (quem recebe decide como ler).

#### `CelerLink.poll()` → String|null
Consome a mensagem recebida mais antiga (FIFO de 8; overflow descarta).
`null` quando vazia. Chame no loop do app, como o `keypadPoll`.

#### `CelerLink.status()` → Object
`{connected: Boolean, peer: "AA:BB:CC:DD:EE:FF"|"", listening: Boolean}`
(`listening` = advertising ligado via `start()`).

Sair do app reseta a sessão sozinho (desconecta e para o advertising);
chame `stop()`/`disconnect()` só para controlar no meio do app.

### Exemplo — lado do robô (controlável)

```javascript
// "bot": recebe comandos e age (aqui beep/LED; servos num robo de verdade)
CelerLink.start();  // "Celer-XXXX" no ar
System.drawString("esperando controle...", 10, 10);
while (true) {
    var msg = CelerLink.poll();
    if (msg !== null) {
        var cmd = null;
        try { cmd = JSON.parse(msg); } catch (e) {}
        if (cmd && cmd.type === "move") {
            System.beep(50, 10);
            System.led(0, (cmd.speed || 0) > 128 ? 255 : 0, 0);
        }
    }
    System.delay(20);
}
```

### Exemplo — lado do controle (o 4848)

```javascript
// "remote": escaneia, conecta e envia o D-pad como JSON
var peers = CelerLink.scan(3000);
if (!peers.length) { System.drawString("nenhum CelerOS por perto", 10, 10); System.delay(2000); System.exitApp(); }
if (!CelerLink.connect(peers[0].id)) { System.drawString("conectou nao", 10, 10); System.delay(2000); System.exitApp(); }

var last = "";
while (true) {
    var t = System.getTouch();
    var dir = "";
    if (t.touched) {
        if (t.y < 100) dir = "up"; else if (t.y > 220) dir = "down";
        else if (t.x < 100) dir = "left"; else if (t.x > 140) dir = "right";
    }
    if (dir !== last) {
        last = dir;
        if (dir) CelerLink.send({type: "move", dir: dir, speed: 200});
    }
    System.delay(20);
}
```

Os dois exemplos também funcionam em par com o app nRF Connect no celular
(conecte no `Celer-XXXX`, escreva na característica, ative notificações).
