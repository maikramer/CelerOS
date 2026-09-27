# esp_components/UI

Componentes de UI reutilizáveis para aplicações ESP32 com LVGL.

## Visão Geral

Esta biblioteca fornece componentes de alto nível para criar interfaces de usuário consistentes e profissionais em dispositivos ESP32 com displays touchscreen.

## Índice

**Componentes Base:**
- [LvglUtils](#lvglutils) - Operações LVGL thread-safe
- [Theme](#theme) - Cores, fontes e constantes
- [ThemeManager](#thememanager) - Modo claro/escuro dinâmico
- [BaseScreen](#basescreen) - Classe base para telas

**Componentes de UI:**
- [Dialog](#dialog) - Dialogs modais
- [Header](#header) - Barra de status
- [Toast](#toast) - Notificações temporárias
- [Card](#card) - Containers elevados
- [InfoList](#infolist) - Lista de informações
- [InputField](#inputfield) - Campos de entrada
- [ScrollableList](#scrollablelist) - Lista scrollável

**Componentes Visuais:**
- [Gauge](#gauge) - Medidor numérico
- [ProgressRing](#progressring) - Anel de progresso circular

**Navegação e Animação:**
- [Navigator](#navigator) - Sistema de navegação com stack
- [Animation](#animation) - Transições de tela
- [GestureDetector](#gesturedetector) - Detecção de gestos
- [Layout](#layout) - Sistema responsivo

## Componentes

### LvglUtils

Utilitários para operações LVGL thread-safe.

```cpp
#include "ui/LvglUtils.h"

// Inicialização (uma vez)
ui::initLvglMutex(mutex, lvglTask);

// Lock/unlock manual
ui::lvgl_lock();
// ... operações LVGL ...
ui::lvgl_unlock();

// RAII automático (recomendado)
{
    LVGL_SCOPED_LOCK();
    // ... operações LVGL ...
} // unlock automático
```

### Theme

Sistema de cores, fontes e constantes de layout.

```cpp
#include "ui/Theme.h"

// Cores
lv_color_t bg = ui::theme::background();
lv_color_t primary = ui::theme::primary();
lv_color_t error = ui::theme::error();

// Constantes de layout
int32_t headerH = ui::theme::HEADER_H;  // 40px
int32_t btnH = ui::theme::BUTTON_H;     // 38px

// Fontes
const lv_font_t* title = ui::theme::fontTitle();
const lv_font_t* text = ui::theme::fontText();

// Registrar fontes customizadas do projeto
ui::theme::setTitleFont(&my_custom_font);
```

### BaseScreen

Classe base para criar telas com padrões consistentes.

```cpp
#include "ui/BaseScreen.h"

class MyScreen : public ui::BaseScreen {
protected:
    void onCreate() override {
        createTitle();  // Título automático
        createBackButton();  // Botão voltar
        
        // Seu conteúdo
        auto* label = lv_label_create(screen());
        lv_label_set_text(label, "Hello!");
        lv_obj_center(label);
    }
    
    const char* getTitle() const override { return "Minha Tela"; }
};

// Uso
MyScreen myScreen;
myScreen.setBackCallback([]() { goBack(); });
myScreen.show();
```

### Dialog

Dialogs modais para informações, alertas e confirmações.

```cpp
#include "ui/Dialog.h"

// Info simples
ui::Dialog::show(ui::Dialog::Type::Info, "Título", "Mensagem");

// Erro com callback
ui::Dialog::show(ui::Dialog::Type::Error, "Erro", 
                 "Falha na conexão",
                 []() { ESP_LOGI("APP", "Usuário confirmou"); });

// Confirmação
ui::Dialog::confirm("Confirmar", "Deseja continuar?",
    []() { doAction(); },     // onConfirm
    []() { cancel(); });      // onCancel (opcional)

// Loading
ui::Dialog::showLoading("Carregando...");
// ... operação assíncrona ...
ui::Dialog::hideLoading();
```

### Header

Barra de status com ícone WiFi e botão de configurações.

```cpp
#include "ui/Header.h"

auto* header = ui::Header::create(screen, {
    .showWifiStatus = true,
    .showSettingsButton = true,
    .title = "Minha App",
    .onSettingsClick = []() { openSettings(); }
});

// Atualizar status WiFi periodicamente
ui::Header::updateWifiStatus(header, networkMgr.isConnected());
```

### InfoList

Lista scrollável de pares label/valor.

```cpp
#include "ui/InfoList.h"

ui::InfoList infoList(screen);
infoList.addItem("Versão", "1.0.0");
infoList.addItem("Device ID", "ABC123");
infoList.addSeparator();
infoList.addSectionTitle("Rede");
infoList.addItem("IP", "192.168.1.100");

// Atualizar dinamicamente
infoList.updateValue(3, "192.168.1.101");
```

### InputField

Campos de entrada de texto.

```cpp
#include "ui/InputField.h"

// Textarea direto
auto* input = ui::InputField::create(screen, 
    ui::InputField::Type::Password, "Digite a senha");

// Botão que abre editor
auto* btn = ui::InputField::createButton(screen, "valor inicial",
    []() { openKeyboard(); });
    
// Atualizar texto do botão
ui::InputField::setButtonText(btn, "novo valor", true /* isPassword */);
```

## Integração com Projeto

### CMakeLists.txt

Adicione ao `idf_component.yml` ou `CMakeLists.txt` do seu projeto:

```cmake
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_SOURCE_DIR}/../esp_components/UI"
    # ... outras dependências ...
)
```

### Inicialização

```cpp
#include "ui/LvglUtils.h"
#include "ui/Theme.h"

void app_main() {
    // Após inicializar LVGL
    ui::initLvglMutex(lvgl_mutex, lvgl_task);
    
    // Opcional: registrar fontes customizadas
    ui::theme::setTitleFont(&roboto_24);
    ui::theme::setTextFont(&roboto_16);
}
```

## Cores Disponíveis

| Função | Cor | Hex |
|--------|-----|-----|
| `background()` | Branco | #FFFFFF |
| `surface()` | Branco | #FFFFFF |
| `primary()` | Azul Material | #2196F3 |
| `secondary()` | Cinza azulado | #607D8B |
| `text()` | Preto | #000000 |
| `textSecondary()` | Cinza | #757575 |
| `success()` | Verde | #4CAF50 |
| `error()` | Vermelho | #F44336 |
| `warning()` | Laranja | #FF9800 |
| `border()` | Cinza claro | #CCCCCC |

## Constantes de Layout

| Constante | Valor | Descrição |
|-----------|-------|-----------|
| `SCREEN_W` | 320 | Largura da tela |
| `SCREEN_H` | 240 | Altura da tela |
| `PADDING` | 8 | Padding padrão |
| `PADDING_H` | 16 | Padding horizontal |
| `HEADER_H` | 40 | Altura do header |
| `BUTTON_H` | 38 | Altura de botões |
| `INPUT_H` | 40 | Altura de inputs |
| `RADIUS_SM` | 4 | Raio pequeno |
| `RADIUS_MD` | 8 | Raio médio |
| `RADIUS_LG` | 18 | Raio grande |

---

## Novos Componentes (v2.0)

### ThemeManager

Gerenciador de temas com suporte a modo claro/escuro dinâmico.

```cpp
#include "ui/ThemeManager.h"

// Alternar tema
ThemeManager::instance().toggle();

// Definir tema escuro
ThemeManager::instance().setDarkTheme();

// Callback de mudança de tema
ThemeManager::instance().onThemeChange([]() {
    // Atualizar UI
});

// Verificar tema atual
if (ThemeManager::instance().isDark()) {
    // ...
}
```

### Navigator

Sistema de navegação com stack de telas e transições.

```cpp
#include "ui/Navigator.h"

// Navegar para nova tela
Navigator::instance().push(std::make_unique<MyScreen>());

// Voltar
Navigator::instance().pop();

// Voltar para raiz
Navigator::instance().popToRoot();

// Voltar para tela específica
Navigator::instance().popTo("HomeScreen");

// Callback de navegação
Navigator::instance().setNavigationCallback([](BaseScreen* from, BaseScreen* to) {
    ESP_LOGI("NAV", "Navegando de %s para %s", 
             from ? from->screenName() : "null",
             to ? to->screenName() : "null");
});
```

### Animation

Sistema de animações para transições de tela e elementos.

```cpp
#include "ui/Animation.h"

using namespace ui::anim;

// Configurar animações globais
Config cfg;
cfg.defaultEnter = ScreenTransition::SlideLeft;
cfg.defaultExit = ScreenTransition::FadeOut;
cfg.screenTransitionMs = 200;
setConfig(cfg);

// Tipos de transição disponíveis:
// - None, FadeIn, FadeOut
// - SlideLeft, SlideRight, SlideUp, SlideDown
// - OverLeft, OverRight, OverUp, OverDown

// Animar elementos individuais
fadeIn(myLabel, 200);           // Fade in em 200ms
slideIn(myPanel, Direction::Left, 300);  // Slide da esquerda
popIn(myButton, 150);           // Pop com escala
```

### GestureDetector

Detecção de gestos de touch (swipe, long press, double tap).

```cpp
#include "ui/GestureDetector.h"

// Detectar gestos em um objeto
GestureDetector::attach(myPanel, [](GestureType type) {
    switch (type) {
        case GestureType::SwipeRight:
            Navigator::instance().pop();  // Voltar com swipe
            break;
        case GestureType::SwipeLeft:
            // Próxima tela
            break;
        case GestureType::LongPress:
            // Abrir menu
            break;
        case GestureType::DoubleTap:
            // Ação especial
            break;
    }
});

// Callback detalhado
GestureDetector::attachDetailed(myPanel, [](const GestureInfo& info) {
    ESP_LOGI("GESTURE", "Delta: %d, %d - Duration: %dms", 
             info.deltaX, info.deltaY, info.durationMs);
});

// Configurar sensibilidade
GestureDetector::setSwipeThreshold(60);  // 60 pixels para detectar swipe
GestureDetector::setLongPressMs(500);    // 500ms para long press
```

### Toast

Notificações temporárias estilo Android.

```cpp
#include "ui/Toast.h"

// Toast simples
ui::showToast("Operação concluída!");

// Toast com tipo
ui::showToast("Erro ao salvar", 3000, ToastPosition::Top, ToastType::Error);
ui::showToast("Salvo com sucesso", 2000, ToastPosition::Bottom, ToastType::Success);

// Snackbar com ação
ui::showSnackbar("Arquivo deletado", "DESFAZER", []() {
    restoreFile();
});

// Fechar toast manualmente
ui::dismissToast();
```

### Card

Container com visual de card (sombra, borda arredondada).

```cpp
#include "ui/Card.h"

Card card(parent);
card.setTitle("Configurações");
card.setContent("Ajuste as preferências do sistema");
card.setIcon(LV_SYMBOL_SETTINGS);

// Card clicável
card.setOnClick([]() {
    openSettings();
});

// Acessar container LVGL para customização
lv_obj_t* container = card.container();
```

### Gauge

Medidor visual para exibição de valores numéricos.

```cpp
#include "ui/Gauge.h"

Gauge tempGauge(parent);
tempGauge.setRange(0, 100);
tempGauge.setValue(42);
tempGauge.setLabel("Temperatura");
tempGauge.setUnit("°C");

// Cores por faixa
tempGauge.setRangeColors({
    {0,  50, ui::theme::success()},   // Verde: 0-50
    {50, 75, ui::theme::warning()},   // Amarelo: 50-75
    {75, 100, ui::theme::error()}     // Vermelho: 75-100
});
```

### ProgressRing

Anel de progresso circular com suporte a modo indeterminado.

```cpp
#include "ui/ProgressRing.h"

ProgressRing ring(parent, 60);  // 60px de diâmetro
ring.setValue(75);              // 75%
ring.setColor(ui::theme::primary());

// Modo loading (animação contínua)
ring.setIndeterminate(true);

// Parar animação
ring.setIndeterminate(false);
ring.setValue(100);
```

### Layout

Sistema de layout relativo e responsivo.

```cpp
#include "ui/Layout.h"

using namespace ui::layout;

// Configurar tamanho da tela (na inicialização)
setScreenSize(320, 240);

// Posicionar elemento em 10% do topo, 5% da esquerda
auto pos = position(0.05f, 0.10f);
lv_obj_set_pos(obj, pos.x, pos.y);

// Tamanho de 90% da largura, 50% da altura
auto sz = size(0.90f, 0.50f);
lv_obj_set_size(obj, sz.w, sz.h);

// Valores responsivos por breakpoint (Small, Medium, Large)
int32_t padding = responsive(4, 8, 16);

// Detectar tamanho de tela
if (currentScreenSize() == ScreenSize::Small) {
    // Layout compacto
}
```

---

## Exemplo Completo

```cpp
#include "ui/LvglUtils.h"
#include "ui/Navigator.h"
#include "ui/ThemeManager.h"
#include "ui/BaseScreen.h"
#include "ui/Header.h"
#include "ui/Card.h"
#include "ui/Toast.h"
#include "ui/GestureDetector.h"

class HomeScreen : public ui::BaseScreen {
protected:
    void onCreate() override {
        createTitle();
        
        // Header com WiFi status
        auto* header = ui::Header::create(screen(), {
            .showWifiStatus = true,
            .title = "Home"
        });
        
        // Card de status
        Card statusCard(screen());
        statusCard.setTitle("Sistema");
        statusCard.setContent("Tudo funcionando!");
        statusCard.setIcon(LV_SYMBOL_OK);
        lv_obj_align(statusCard.container(), LV_ALIGN_CENTER, 0, 0);
        
        // Detectar swipe para menu
        GestureDetector::attach(screen(), [](GestureType type) {
            if (type == GestureType::SwipeLeft) {
                Navigator::instance().push(std::make_unique<SettingsScreen>());
            }
        });
    }
    
    const char* getTitle() const override { return "Home"; }
    const char* screenName() const override { return "HomeScreen"; }
};

void app_main() {
    // Inicializar LVGL mutex
    ui::initLvglMutex(lvgl_mutex, lvgl_task);
    
    // Configurar tema escuro
    ThemeManager::instance().setDarkTheme();
    
    // Configurar animações
    ui::anim::Config animCfg;
    animCfg.defaultEnter = ui::anim::ScreenTransition::SlideLeft;
    ui::anim::setConfig(animCfg);
    
    // Mostrar tela inicial
    Navigator::instance().push(std::make_unique<HomeScreen>());
    
    ui::showToast("App iniciado!", 2000, ToastPosition::Bottom, ToastType::Success);
}
```
