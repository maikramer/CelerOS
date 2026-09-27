# Drivers Component

Componente com drivers de hardware para ESP32, incluindo leitura de bateria via ADC e controle de motores de passo.

## Índice

- [Battery](#battery)
- [Stepper](#stepper)

---

## Battery

Classe para leitura de tensão de bateria usando ADC com calibração automática.

### Características

- **Calibração automática**: Suporta Curve Fitting e Line Fitting
- **Múltiplas amostras**: Média de 4 leituras para maior precisão
- **Divisor de tensão**: Multiplicador 2x para baterias com divisor resistivo
- **ADC de 12 bits**: Resolução de 4096 níveis

### Configuração

| Parâmetro | Valor Padrão |
|-----------|--------------|
| Atenuação | 11 dB |
| Unidade ADC | ADC_UNIT_1 |
| Largura de bits | 12 bits |
| Amostras | 4 |
| VREF padrão | 1100 mV |

### API

```cpp
class Battery {
public:
    /**
     * @brief Construtor
     * @param channel Canal ADC para leitura (ex: ADC_CHANNEL_0)
     */
    Battery(adc_channel_t channel);
    
    /**
     * @brief Lê a tensão da bateria
     * @return Tensão em mV (já considerando divisor 2x)
     */
    uint32_t GetVoltage();
};
```

### Exemplo de Uso

```cpp
#include "Battery.h"

// Configurar bateria no canal ADC 0
Battery battery(ADC_CHANNEL_0);

void checkBattery() {
    uint32_t voltage = battery.GetVoltage();
    
    ESP_LOGI("BAT", "Tensão: %lu mV", voltage);
    
    // Calcular porcentagem (exemplo para Li-Ion 3.7V)
    // 4200mV = 100%, 3000mV = 0%
    int percentage = (voltage - 3000) * 100 / 1200;
    percentage = std::clamp(percentage, 0, 100);
    
    ESP_LOGI("BAT", "Bateria: %d%%", percentage);
}

// Task de monitoramento
void batteryTask(void* param) {
    Battery* bat = static_cast<Battery*>(param);
    
    while (true) {
        uint32_t voltage = bat->GetVoltage();
        
        if (voltage < 3300) {
            ESP_LOGW("BAT", "Bateria baixa: %lu mV", voltage);
        }
        
        vTaskDelay(pdMS_TO_TICKS(60000)); // A cada minuto
    }
}
```

### Esquema de Hardware

```
VBat ─────┬───── R1 (10k) ─────┬───── ADC_CHANNEL
          │                    │
          │                    R2 (10k)
          │                    │
          └────────────────────┴───── GND

Divisor: Vout = Vin * R2 / (R1 + R2) = Vin / 2
```

### Níveis de Bateria (Li-Ion típico)

| Tensão | Estado |
|--------|--------|
| > 4100 mV | Cheio (100%) |
| 3800-4100 mV | Bom (50-100%) |
| 3500-3800 mV | Médio (20-50%) |
| 3300-3500 mV | Baixo (5-20%) |
| < 3300 mV | Crítico |

---

## Stepper

Controle de motor de passo com driver (STEP/DIR/ENABLE) usando timer de alta precisão.

### Características

- **Controle preciso**: Timer ESP para pulsos consistentes
- **Direção bidirecional**: Suporte a movimento para frente/trás
- **Evento de conclusão**: Callback quando movimento termina
- **Velocidade configurável**: Passos por segundo

### API

```cpp
class Stepper {
public:
    /**
     * @brief Construtor
     * @param step Pino GPIO para pulso STEP
     * @param direction Pino GPIO para direção
     * @param enable Pino GPIO para habilitar driver
     */
    Stepper(gpio_num_t step, gpio_num_t direction, gpio_num_t enable);
    
    /**
     * @brief Inicializa GPIOs
     */
    void Init();
    
    /**
     * @brief Define direção do movimento
     * @param front true = frente, false = trás
     */
    void SetDirection(bool front);
    
    /**
     * @brief Move número de passos
     * @param steps Número de passos (negativo = direção inversa)
     * @param speed Velocidade em passos/segundo (padrão: 200)
     */
    void Move(int32_t steps, uint32_t speed = 200);
    
    /**
     * @brief Para o movimento imediatamente
     */
    void Stop();
    
    // Propriedades públicas
    gpio_num_t Step;
    gpio_num_t Direction;
    gpio_num_t Enable;
    bool IsMoving;
    bool MovingFront;
    uint32_t StepCount;
    uint32_t DesiredSteps;
    
    // Evento disparado ao terminar movimento
    Event<> OnFinishStepping;
};
```

### Exemplo de Uso

```cpp
#include "Stepper.h"

// Configurar stepper nos pinos GPIO
Stepper stepper(GPIO_NUM_25, GPIO_NUM_26, GPIO_NUM_27);

void setup() {
    stepper.Init();
    
    // Registrar callback de conclusão
    stepper.OnFinishStepping.addHandler([]() {
        ESP_LOGI("STEPPER", "Movimento concluído!");
    });
}

void moveForward() {
    // Mover 1000 passos para frente a 400 passos/s
    stepper.SetDirection(true);
    stepper.Move(1000, 400);
}

void moveBackward() {
    // Mover 500 passos para trás a 200 passos/s
    stepper.SetDirection(false);
    stepper.Move(500, 200);
}

void emergencyStop() {
    stepper.Stop();
}
```

### Exemplo com Posicionamento

```cpp
class PositionController {
public:
    PositionController(Stepper& stepper) : _stepper(stepper) {
        _stepper.OnFinishStepping.addHandler([this]() {
            _isMoving = false;
            ESP_LOGI("POS", "Posição atual: %ld", _currentPosition);
        });
    }
    
    void moveTo(int32_t targetPosition, uint32_t speed = 200) {
        if (_isMoving) {
            ESP_LOGW("POS", "Já está em movimento");
            return;
        }
        
        int32_t delta = targetPosition - _currentPosition;
        if (delta == 0) return;
        
        _isMoving = true;
        _stepper.SetDirection(delta > 0);
        _stepper.Move(abs(delta), speed);
        _currentPosition = targetPosition;
    }
    
    void home(uint32_t speed = 100) {
        // Mover até sensor de home
        _stepper.SetDirection(false);
        _stepper.Move(INT32_MAX, speed);
        // Aguardar sensor de home parar o movimento
    }
    
    int32_t getPosition() const { return _currentPosition; }
    
private:
    Stepper& _stepper;
    int32_t _currentPosition = 0;
    bool _isMoving = false;
};
```

### Conexão com Driver (A4988/DRV8825)

```
ESP32          Driver
─────          ──────
GPIO_STEP  ──> STEP
GPIO_DIR   ──> DIR
GPIO_EN    ──> ENABLE (LOW = ativo)
GND        ──> GND

Driver         Motor
──────         ─────
1A, 1B     ──> Bobina 1
2A, 2B     ──> Bobina 2
VMOT       ──> 12-24V DC
```

### Configuração de Microstepping

| MS1 | MS2 | MS3 | Resolução |
|-----|-----|-----|-----------|
| LOW | LOW | LOW | Full step |
| HIGH | LOW | LOW | 1/2 step |
| LOW | HIGH | LOW | 1/4 step |
| HIGH | HIGH | LOW | 1/8 step |
| HIGH | HIGH | HIGH | 1/16 step |

---

## Dependências

```cmake
idf_component_register(
    SRCS
        "Battery.cpp"
        "Stepper.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        driver
        esp_timer
        Utility
)
```

## Compatibilidade

- ESP-IDF >= 5.0.0
- Plataformas: ESP32, ESP32-S2, ESP32-S3, ESP32-C3, ESP32-C6
