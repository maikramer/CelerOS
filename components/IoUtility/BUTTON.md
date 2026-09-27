# Button - Wrapper C++ para iot_button

O componente `Button` é um wrapper C++ moderno e event-driven para o componente [`iot_button`](https://docs.espressif.com/projects/esp-iot-solution/en/latest/input_device/button.html) do ESP-IoT-Solution.

## Índice

- [Características](#características)
- [Instalação](#instalação)
- [Uso Básico](#uso-básico)
- [Eventos Disponíveis](#eventos-disponíveis)
- [Configuração](#configuração)
- [API Completa](#api-completa)
- [Exemplos Avançados](#exemplos-avançados)

---

## Características

- **Event-Driven**: Todos os eventos do botão são expostos via `Event<Args...>`
- **Debounce Automático**: Tratado internamente pelo `iot_button`
- **Suporte Completo**: Todos os eventos do `iot_button` disponíveis
- **C++ Moderno**: RAII, sem vazamento de memória
- **Configurável**: Tempos de long press, short press, múltiplos cliques

## Instalação

O componente `button` do ESP-IoT-Solution é necessário. Ele já está configurado como dependência no `idf_component.yml`.

Se precisar adicionar manualmente:

```bash
idf.py add-dependency "espressif/button>=3.0.0"
```

---

## Uso Básico

### Criar um Botão

```cpp
#include "Button.h"

// Botão no GPIO 0, ativo baixo (pressionado = LOW)
Button btn(GPIO_NUM_0, true);

// Verificar se foi criado com sucesso
if (!btn.isValid()) {
    ESP_LOGE("App", "Falha ao criar botão!");
}
```

### Registrar Handlers de Eventos

```cpp
// Clique simples
btn.onClick.addHandler([](Button* b) {
    ESP_LOGI("App", "Botão clicado!");
});

// Clique duplo
btn.onDoubleClick.addHandler([](Button* b) {
    ESP_LOGI("App", "Clique duplo!");
});

// Long press
btn.onLongPressStart.addHandler([](Button* b) {
    ESP_LOGI("App", "Long press iniciado!");
});
```

---

## Eventos Disponíveis

| Evento | Parâmetros | Descrição |
|--------|------------|-----------|
| `onPress` | `Button*` | Botão pressionado |
| `onRelease` | `Button*` | Botão liberado |
| `onClick` | `Button*` | Clique simples detectado |
| `onDoubleClick` | `Button*` | Clique duplo detectado |
| `onMultipleClick` | `Button*, uint8_t count` | Múltiplos cliques |
| `onPressRepeat` | `Button*, uint8_t count` | Pressionamentos repetidos |
| `onPressRepeatDone` | `Button*, uint8_t count` | Fim dos pressionamentos repetidos |
| `onLongPressStart` | `Button*` | Início de long press |
| `onLongPressHold` | `Button*, uint32_t duration_ms` | Durante long press |
| `onLongPressUp` | `Button*, uint32_t duration_ms` | Fim de long press |
| `onPressEnd` | `Button*` | Fim de qualquer sequência |
| `onAnyEvent` | `Button*, ButtonEvent` | Qualquer evento (debug) |

### Diagrama de Eventos

```
Pressionar                        Soltar
    │                                │
    ▼                                ▼
┌─────────┐                    ┌──────────┐
│ onPress │                    │ onRelease│
└────┬────┘                    └────┬─────┘
     │                              │
     │ < short_press_time?          │
     │         │                    │
     │         ▼                    │
     │    ┌─────────┐               │
     │    │ onClick │ ◄─────────────┤ (se clique simples)
     │    └─────────┘               │
     │                              │
     │ >= long_press_time?          │
     │         │                    │
     │         ▼                    │
     │  ┌─────────────────┐         │
     │  │ onLongPressStart│         │
     │  └────────┬────────┘         │
     │           │                  │
     │           ▼ (periodicamente) │
     │  ┌─────────────────┐         │
     │  │ onLongPressHold │         │
     │  └────────┬────────┘         │
     │           │                  │
     │           ▼                  │
     │  ┌─────────────────┐         │
     │  │  onLongPressUp  │ ◄───────┘
     │  └─────────────────┘
     │
     ▼ (sempre no final)
┌───────────┐
│ onPressEnd│
└───────────┘
```

---

## Configuração

### Tempos

```cpp
Button btn(GPIO_NUM_0);

// Tempo para detectar long press (padrão: 1500ms)
btn.setLongPressTime(2000);  // 2 segundos

// Tempo para detectar short press (padrão: 180ms)
btn.setShortPressTime(100);  // 100ms
```

### Múltiplos Cliques

```cpp
// Registrar detecção de triplo clique
btn.registerMultipleClick(3);

// Registrar detecção de quádruplo clique
btn.registerMultipleClick(4);

// Handler para múltiplos cliques
btn.onMultipleClick.addHandler([](Button* b, uint8_t count) {
    ESP_LOGI("App", "Clicado %d vezes!", count);
});
```

### Long Press Personalizado

```cpp
// Registrar long press em tempo específico
btn.registerLongPressDuration(3000);  // 3 segundos
btn.registerLongPressDuration(5000);  // 5 segundos
```

### Configuração Completa

```cpp
ButtonConfig config;
config.gpio = GPIO_NUM_0;
config.activeLow = true;
config.enablePullUp = true;
config.longPressTimeMs = 2000;
config.shortPressTimeMs = 150;

Button btn(config);
```

---

## API Completa

### Construtores

```cpp
// GPIO + ativo baixo
Button(gpio_num_t gpio, bool activeLow = true);

// Configuração completa
Button(const ButtonConfig& config);
```

### Configuração

| Método | Descrição |
|--------|-----------|
| `setLongPressTime(ms)` | Define tempo para long press |
| `setShortPressTime(ms)` | Define tempo para short press |
| `registerMultipleClick(count)` | Registra detecção de N cliques |
| `registerLongPressDuration(ms)` | Registra long press em tempo específico |

### Estado

| Método | Descrição |
|--------|-----------|
| `isPressed()` | Verifica se está pressionado |
| `isValid()` | Verifica se foi inicializado |
| `getRepeatCount()` | Contador de repetições |
| `getTicksTime()` | Tempo desde que foi pressionado |
| `getLongPressHoldCount()` | Contador de eventos hold |
| `getGpio()` | Retorna o GPIO |
| `getHandle()` | Retorna handle do iot_button |

### Power Management

| Método | Descrição |
|--------|-----------|
| `stop()` | Para processamento (economia de energia) |
| `resume()` | Retoma processamento |

---

## Exemplos Avançados

### Controle de LED com Botão

```cpp
#include "Button.h"
#include "driver/gpio.h"

#define LED_GPIO GPIO_NUM_2
#define BTN_GPIO GPIO_NUM_0

Button btn(BTN_GPIO);
bool ledState = false;

void setup() {
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
    
    // Toggle LED no clique
    btn.onClick.addHandler([](Button* b) {
        ledState = !ledState;
        gpio_set_level(LED_GPIO, ledState);
    });
    
    // LED pisca durante long press
    btn.onLongPressHold.addHandler([](Button* b, uint32_t duration) {
        static bool blink = false;
        blink = !blink;
        gpio_set_level(LED_GPIO, blink);
    });
    
    // LED desliga ao soltar long press
    btn.onLongPressUp.addHandler([](Button* b, uint32_t duration) {
        gpio_set_level(LED_GPIO, ledState);
    });
}
```

### Menu com Múltiplos Botões

```cpp
Button btnUp(GPIO_NUM_32);
Button btnDown(GPIO_NUM_33);
Button btnSelect(GPIO_NUM_34);

int menuIndex = 0;

void setupMenu() {
    btnUp.onClick.addHandler([](Button* b) {
        menuIndex = (menuIndex > 0) ? menuIndex - 1 : 0;
        updateDisplay();
    });
    
    btnDown.onClick.addHandler([](Button* b) {
        menuIndex++;
        updateDisplay();
    });
    
    btnSelect.onClick.addHandler([](Button* b) {
        selectMenuItem(menuIndex);
    });
    
    btnSelect.onLongPressStart.addHandler([](Button* b) {
        enterEditMode();
    });
}
```

### Debug de Todos os Eventos

```cpp
Button btn(GPIO_NUM_0);

btn.onAnyEvent.addHandler([](Button* b, ButtonEvent event) {
    const char* eventNames[] = {
        "PRESS_DOWN", "PRESS_UP", "PRESS_REPEAT", "PRESS_REPEAT_DONE",
        "SINGLE_CLICK", "DOUBLE_CLICK", "MULTIPLE_CLICK",
        "LONG_PRESS_START", "LONG_PRESS_HOLD", "LONG_PRESS_UP", "PRESS_END"
    };
    
    int idx = static_cast<int>(event);
    if (idx >= 0 && idx < 11) {
        ESP_LOGI("BTN", "GPIO %d: %s", b->getGpio(), eventNames[idx]);
    }
});
```

### Economia de Energia

```cpp
Button btn(GPIO_NUM_0);

void enterDeepSleep() {
    // Parar processamento do botão antes de dormir
    btn.stop();
    
    // Configurar wake-up por GPIO...
    esp_deep_sleep_start();
}

void wakeUp() {
    // Retomar processamento após acordar
    btn.resume();
}
```

---

## Migração do FilteredInput

Se você estava usando `FilteredInput` ou `FilteredInputEx`, aqui está como migrar:

### Antes (FilteredInput)

```cpp
FilteredInputEx btn([](){ return gpio_get_level(GPIO_NUM_0); }, 50);
btn.ActiveLow = true;

btn.PressedEvent.addHandler([](FilteredInput* f, void*) {
    ESP_LOGI("App", "Pressionado");
});

btn.ClickedEvent.addHandler([](FilteredInput* f, void*) {
    ESP_LOGI("App", "Clicado");
});
```

### Depois (Button)

```cpp
Button btn(GPIO_NUM_0, true);

btn.onPress.addHandler([](Button* b) {
    ESP_LOGI("App", "Pressionado");
});

btn.onClick.addHandler([](Button* b) {
    ESP_LOGI("App", "Clicado");
});
```

### Mapeamento de Eventos

| FilteredInputEx | Button |
|-----------------|--------|
| `PressedEvent` | `onPress` |
| `ReleasedEvent` | `onRelease` |
| `ClickedEvent` | `onClick` |

---

## Referências

- [ESP-IoT-Solution Button Documentation](https://docs.espressif.com/projects/esp-iot-solution/en/latest/input_device/button.html)
- [Component Registry](https://components.espressif.com/components/espressif/button)
