# Guia de Desenvolvimento de Apps do CelerOS

[English](App_Development_Guide.md) | **Português (BR)**

Bem-vindo ao Guia de Desenvolvimento de Apps do CelerOS! Desenvolver apps
para o CelerOS é simples. Os apps são escritos em JavaScript e usam uma
estrutura de pastas padrão contendo metadados e código.

## 1. Estrutura de Pastas do App

No CelerOS, um app não é apenas um arquivo `.js`. Cada app é uma **pasta**
contendo todos os arquivos necessários. Quando você instala o app com
`celerctl apps install <pasta>`, pelo cartão SD (Installer) ou pela App
Store, a pasta inteira é o que vai para o aparelho.

Uma pasta de app padrão tem esta cara:
```text
MyAwesomeApp/
├── app.json
├── icon.png
└── main.js
```

## 2. O Arquivo `app.json` (Metadados do App)

O `app.json` é o coração da identidade do seu app. O Installer do CelerOS lê
esse arquivo para instalar, atualizar e categorizar sua aplicação com
segurança.

### Formato de exemplo:
```json
{
  "name": "Meu App",
  "packageName": "com.desenvolvedor.meuapp",
  "version": "1.0.0",
  "metaUrl": "https://raw.githubusercontent.com/.../meuapp/app.json",
  "author": "Fulano",
  "description": "Um app legal que faz coisas incríveis.",
  "type": "App",
  "category": "Utilities",
  "api": 6,
  "changelog": "Lancamento inicial."
}
```

### Detalhe dos campos:
- **`name`**: o nome de exibição do app. É o que o usuário vê na Home.
- **`packageName`**: identificador globalmente único do seu app. **Regras:
  minúsculas, estilo separado por pontos, sem espaços** (ex.:
  `com.seunome.appnome`). O SO usa esse valor para detectar se o app já está
  instalado.
- **`version`**: versionamento semântico (ex.: `1.0.0`, `1.2.1`). Se for
  instalado um app com o mesmo `packageName` e número de versão maior, o SO
  oferece "Atualizar" em vez de "Instalar".
- **`metaUrl`** *(opcional)*: URL raw do `app.json` na internet (ex.: seu
  repositório GitHub). Apps publicados no **CelerOS Hub não precisam dela** —
  o próprio catálogo da loja carrega nome, versão, changelog, tamanho e o
  checksum MD5 do seu `main.js`; `metaUrl` serve só para checagem de versão
  fora do hub.
- **`author`**: seu nome ou estúdio. Se outra pessoa tentar instalar um app
  com o seu `packageName` mas um `author` diferente, o SO emite um aviso de
  conflito para proteger o app contra sobrescrita maliciosa.
- **`description`**: resumo curto do app, exibido ao usuário na primeira
  instalação. Obs.: os apps de sistema evitam caracteres acentuados — fique
  no ASCII simples para máxima compatibilidade com a fonte embutida.
- **`type`**: classificação ampla (ex.: `App` ou `Game`). Pode digitar
  qualquer valor, sem restrição.
- **`category`**: categoria específica (ex.: `Utilities`, `Games`, `Tools`).
  Pode digitar qualquer valor, sem restrição.
- **`api`**: nível de API do CelerOS que o app mira (veja o [Guia da API
  JS](JS_API_Guide.pt-BR.md) — atualmente `6`). Verificado pelo sistema na
  instalação.
- **`changelog`**: string curta descrevendo o que mudou (uma linha por versão
  funciona bem, ex. `"1.1.0 - conserto de crash\n1.0.0 - primeiro
  lancamento"`). O hub publica junto com o catálogo e a loja do aparelho
  mostra sob o título **"Novidades"** na tela de atualização.

### Campos gerenciados pelo CelerOS Hub

Ao publicar pelo hub (`tools/celerhub.py` / `publish_app.py`), quatro campos
do `app.json` são calculados e gravados pelo servidor — **não escreva à mão**
(um publish manual os recalcula de qualquer forma):

- **`size`** — tamanho em bytes do `main.js` (a loja confere o espaço livre
  no disco antes de baixar).
- **`md5`** — checksum do `main.js`; o aparelho valida depois de baixar e
  aborta o update em caso de divergência, sem tocar na versão instalada.
- **`published_at`** — data/hora UTC do publish.
- **`publisher`** — nome do token que publicou o pacote.

### Regras de atualização de apps (CelerOS Hub)

- **O catálogo é o canal de update.** A loja compara a versão de cada entrada
  com a instalada; versão nova ganha badge **"Atualizar"** na lista, aparece
  na aba **Atualizações** e no botão **"Atualizar tudo"**.
- **Suba a `version` a cada publish.** O hub rejeita versão ≤ à publicada
  (`--force` para exceções, ex. republicar um pacote corrigido).
- **O seu `packageName` tem dono.** Quem publica primeiro vira o dono; só o
  mesmo token (ou o root do hub) atualiza/remove o pacote depois.
- **Limites de tamanho:** `main.js` ≤ **48 KB** — apps acima de **30 KB**
  precisam declarar `api: 6` (firmware antigo baixava via `Net.get`, que
  trunca em 32 KB; o downloader streaming da API 6 não tem teto).
  `icon.png` ≤ 16 KB.
- **O ícone viaja com o update:** mantenha um `icon.png` (PNG 64×64, ≤ 16 KB)
  no pacote — a loja baixa na instalação/atualização e o launcher renova o
  cache de ícones sozinho.
- **O update cai na pasta que o launcher executa** (resolvida por
  `packageName`): atualizar um app preinstalado atualiza in-place, sem criar
  cópia sombreada.
- **A App Store se atualiza sozinha:** a loja é um pacote comum do hub
  (`celeros.appstore`). Quando o catálogo tem loja mais nova, ela aparece
  como qualquer update — depois de instalar, a loja pede para sair e abrir
  de novo (o `main.js` novo é relido do disco na próxima abertura).

## 3. O Arquivo `icon.png` (Ícone do App)

Um PNG quadrado de 64x64 (ou maior) exibido no Launcher. Na instalação ele é
decodificado para o cache de ícones do sistema (RGB565 + alpha de 4 bits),
então transparência e cantos arredondados funcionam. Apps sem `icon.png`
recebem um ícone genérico.

## 4. O Arquivo `main.js` (Lógica do App)

O `main.js` é o ponto de entrada da aplicação. Quando o usuário toca no app
no Launcher, o SO carrega e executa esse arquivo JavaScript.

Como o CelerOS cuida da tradução para C++ por baixo, você escreve JavaScript
de alto nível e simples para desenhar gráficos, ler arquivos e acionar
componentes de UI.

### Seu Primeiro App (`main.js`)
Um exemplo simples que pinta a tela de azul, escreve "Hello CelerOS!",
espera 3 segundos e sai de volta ao Launcher:

```javascript
// Limpa a tela
Graphics.fillScreen(Graphics.COLOR_BLUE);

// Escreve um texto no centro
Graphics.setTextColor(Graphics.COLOR_WHITE);
Graphics.drawString("Hello CelerOS!", 120, 160, 2);

// Espera 3 segundos
System.delay(3000);

// Fecha o app e volta ao Launcher do SO
System.exit();
```

> [!IMPORTANT]
> Para ver tudo o que dá para fazer no `main.js`, consulte o **[Guia da API
> JS](JS_API_Guide.pt-BR.md)** completo! Lá está toda a documentação de
> Graphics, pinos GPIO, sistema de arquivos, componentes de UI e mais.
