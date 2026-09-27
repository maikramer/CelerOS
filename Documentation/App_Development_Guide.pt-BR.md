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
  "api": 5,
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
  repositório GitHub). A App Store usa essa URL para checar automaticamente
  se há versões novas do seu app.
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
  JS](JS_API_Guide.pt-BR.md) — atualmente `5`). Verificado pelo sistema na
  instalação.
- **`changelog`**: string curta descrevendo o que mudou. Quando o usuário
  atualiza o app, este campo substitui a descrição e aparece sob o título
  "What's New"!

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
