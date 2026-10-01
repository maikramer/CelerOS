// Servico de movimento do watch (F2): task propria lendo o QMI8658 a ~30 Hz,
// alimentando pedometro e detector de "levantar o pulso" — portas fieis do
// firmware Rust (waveshare-watch-rs): Pedometer de crates/watch-sdk/src/
// fitness.rs e RaiseDetector de src/peripherals/imu.rs.
//
// Compilado so para a board waveshare-watch (main/CMakeLists.txt); a face
// generica do CelerOS ve o servico pelos hooks do BoardProfile (raisePoll).
// Passos persistem no NVS (CelerSettings) a cada 20 e rolam no dia novo.

#include "Boards/Board.h"
#include "Qmi8658.h"
#include "Motion.h"

#include "../../Utils/CelerSettings.h"
#include "../../Kernel/TimeManager.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

namespace Motion {

// ---- Pedometer (porte do fitness.rs; unidades em g) ------------------------
// Pico/vale sobre a aceleracao DINAMICA (magnitude - gravidade via LP de 1
// polo). Warm-up de 10 amostras com LP mais rapido estabiliza a linha de base.
namespace {
constexpr float kPeakMinG = 0.20f;   // pico minimo (g)
constexpr float kPeakMaxG = 2.20f;   // pico maximo (rejeita spike/tranco)
constexpr uint32_t kMinStepMs = 280; // cadencia minima entre passos
constexpr int kPedoWarm = 10;

struct Pedometer {
    float lp = 1.0f;        // linha de base (gravidade ~1g)
    float prev = 0.0f;      // hp anterior
    float peak = 0.0f;
    bool rising = false;
    int warm = 0;
    uint32_t sinceMs = 0;

    bool feed(float ax, float ay, float az, uint32_t dtMs) {
        float mag = sqrtf(ax * ax + ay * ay + az * az);
        uint32_t dt = dtMs < 1 ? 1 : (dtMs > 250 ? 250 : dtMs);
        sinceMs += dt;
        if (warm < kPedoWarm) {
            lp = (warm == 0) ? mag : lp * 0.6f + mag * 0.4f;
            prev = 0.0f;
            warm++;
            return false;
        }
        lp = lp * 0.90f + mag * 0.10f;
        float hp = mag - lp;
        bool step = false;
        if (hp >= prev) {
            if (!rising) { rising = true; peak = hp; }
            else if (hp > peak) peak = hp;
        } else if (rising) {  // fim da subida = pico
            rising = false;
            if (peak >= kPeakMinG && peak <= kPeakMaxG && sinceMs >= kMinStepMs) {
                step = true;
                sinceMs = 0;
            }
            peak = 0.0f;
        }
        prev = hp;
        return step;
    }
};

// ---- RaiseDetector (porte do imu.rs) ---------------------------------------
// Filtro de 2 amostras: |az| baixo (punho caido) numa amostra -> |az| alto
// (face pra cima) na seguinte, com |ax|+|ay| pequeno (recusa twist). O
// cooldown de 4 amostras evita retrigger no balanco do gesto.
struct RaiseDetector {
    float lastZ = 0.0f;
    bool have = false;
    int cool = 0;

    bool feed(float ax, float ay, float az, int sens) {
        float z = fabsf(az);
        float xy = fabsf(ax) + fabsf(ay);
        if (cool > 0) { cool--; lastZ = z; have = true; return false; }
        float down, up, xyMax;
        if (sens == 0)      { down = 0.22f; up = 0.88f; xyMax = 0.55f; }
        else if (sens == 2) { down = 0.48f; up = 0.58f; xyMax = 1.10f; }
        else                { down = 0.35f; up = 0.70f; xyMax = 0.80f; }
        bool hit = have && lastZ < down && z > up && xy < xyMax;
        lastZ = z;
        have = true;
        if (hit) cool = 4;
        return hit;
    }
};

// ---- estado do servico ------------------------------------------------------
Pedometer s_pedo;
RaiseDetector s_raise;
portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
volatile bool s_raisePending = false;  // consumido pelo raisePoll()
TaskHandle_t s_task = nullptr;
int32_t s_steps = 0;
int32_t s_dayKey = 0;      // yyyymmdd do acumulo corrente (roll diario)
int32_t s_pendingPersist = 0;
int s_raiseSens = 1;
int s_raiseCount = 0;
float s_ax = 0, s_ay = 0, s_az = 1;   // ultima amostra (cache p/ Sensors JS)
volatile bool s_imuOk = false;

int32_t dayKey() {
    return TimeManager::getYear() * 10000 + TimeManager::getMonth() * 100 + TimeManager::getDay();
}

void persist() {
    char v[16];
    snprintf(v, sizeof(v), "%ld", (long)s_steps);
    CelerSettings::set("steps", v);
    snprintf(v, sizeof(v), "%ld", (long)s_dayKey);
    CelerSettings::set("steps_day", v);
    s_pendingPersist = 0;
}

void load() {
    // Chamado pela task depois de ~3 s (FS/NVS ja montados no boot).
    std::string sens = CelerSettings::get("raise_sens", "1");
    s_raiseSens = atoi(sens.c_str());
    if (s_raiseSens < 0 || s_raiseSens > 2) s_raiseSens = 1;
    std::string d = CelerSettings::get("steps_day", "");
    if (!d.empty()) s_dayKey = atol(d.c_str());
    int32_t today = dayKey();
    if (s_dayKey != today) {
        // dia novo (ou primeira leitura): recomeca a contagem
        s_dayKey = today;
        s_steps = 0;
        persist();
        return;
    }
    std::string s = CelerSettings::get("steps", "");
    s_steps = s.empty() ? 0 : atol(s.c_str());
}

void motionTask(void*) {
    // Espera o boot assentar (FS/NVS): Board::init roda antes do FileSystem.
    vTaskDelay(pdMS_TO_TICKS(3000));
    load();

    if (!Qmi8658::init()) {
        ESP_LOGE("celer.imu", "servico de movimento sem IMU");
        s_task = nullptr;
        vTaskDelete(nullptr);
        return;
    }
    Qmi8658::idleAccel30Hz(0x10);  // threshold medio
    ESP_LOGI("celer.imu", "QMI8658 ativo (30 Hz, pedo+raise sens=%d)", s_raiseSens);
    portENTER_CRITICAL(&s_mux);
    s_imuOk = true;
    portEXIT_CRITICAL(&s_mux);

    TickType_t last = xTaskGetTickCount();
    int32_t lastLoggedMilestone = (s_steps / 500) * 500;
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(33));
        uint32_t dtMs = (uint32_t)(xTaskGetTickCount() - last) * portTICK_PERIOD_MS;
        last = xTaskGetTickCount();

        float ax, ay, az;
        if (!Qmi8658::readAccelG(&ax, &ay, &az)) continue;
        Qmi8658::clearMotionIrq();
        portENTER_CRITICAL(&s_mux);
        s_ax = ax; s_ay = ay; s_az = az;
        portEXIT_CRITICAL(&s_mux);

        if (s_pedo.feed(ax, ay, az, dtMs)) {
            portENTER_CRITICAL(&s_mux);
            s_steps++;
            portEXIT_CRITICAL(&s_mux);
            if (++s_pendingPersist >= 20) persist();
            if (s_steps - lastLoggedMilestone >= 500) {
                lastLoggedMilestone = (s_steps / 500) * 500;
                ESP_LOGI("celer.imu", "passos: %ld", (long)s_steps);
            }
        }

        if (s_raise.feed(ax, ay, az, s_raiseSens)) {
            portENTER_CRITICAL(&s_mux);
            s_raisePending = true;
            portEXIT_CRITICAL(&s_mux);
            s_raiseCount++;
            if (s_raiseCount <= 3 || (s_raiseCount % 10) == 0) {
                ESP_LOGI("celer.imu", "raise detectado (#%d)", s_raiseCount);
            }
        }
    }
}

}  // namespace

void start() {
    if (s_task != nullptr) return;
    xTaskCreate(motionTask, "celerimu", 4096, nullptr, 2, &s_task);
}

bool raisePoll() {
    portENTER_CRITICAL(&s_mux);
    bool v = s_raisePending;
    s_raisePending = false;
    portEXIT_CRITICAL(&s_mux);
    return v;
}

int32_t steps() {
    portENTER_CRITICAL(&s_mux);
    int32_t v = s_steps;
    portEXIT_CRITICAL(&s_mux);
    return v;
}

bool accel(float* x, float* y, float* z) {
    portENTER_CRITICAL(&s_mux);
    bool ok = s_imuOk;
    *x = s_ax; *y = s_ay; *z = s_az;
    portEXIT_CRITICAL(&s_mux);
    return ok;
}

bool temp(float* c) { return s_imuOk ? Qmi8658::readTemp(c) : false; }

void prepareSleep() {
    portENTER_CRITICAL(&s_mux);
    bool ok = s_imuOk;
    s_imuOk = false;
    portEXIT_CRITICAL(&s_mux);
    if (!ok) return;
    Qmi8658::powerDown();
    if (s_pendingPersist > 0) persist();
    ESP_LOGI("celer.imu", "IMU desligado p/ deep sleep (passos: %ld)", (long)s_steps);
}

}  // namespace Motion
