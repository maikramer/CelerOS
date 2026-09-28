//
// Created by maikeu on 25/11/22.
//

#include "Stepper.h"
#include "priorities.h"

SemaphoreHandle_t finishSteppingSem = xSemaphoreCreateBinary();;

IRAM_ATTR static void PeriodicCallback(void *arg) {
    auto *st = static_cast<Stepper *>(arg);
    st->LastStepLevel = st->LastStepLevel == 0 ? 0x1 : 0x0;
    gpio_set_level(st->Step, st->LastStepLevel);
    if (st->LastStepLevel == 0x1) {
        st->StepCount++;
    }
    if (st->StepCount >= st->DesiredSteps) {
        esp_timer_stop(st->Timer);
        st->IsMoving = false;
        xSemaphoreGive(finishSteppingSem);
    }
}

[[noreturn]] static void OnFinishSteppingTsk(void *arg) {
    for (;;) {
        auto *st = static_cast<Stepper *>(arg);
        xSemaphoreTake(finishSteppingSem, portMAX_DELAY);
        st->OnFinishStepping.trigger();
    }
}

void Stepper::Init() {
    Utility::SetOutput(Step, false);
    Utility::SetOutput(Direction, false);
    Utility::SetOutput(Enable, false);
    SetDirection(true);
    Utility::CreateAndProfile("OnFinishSteppingTsk", OnFinishSteppingTsk, 2048, HIGH_PRIORITY, 1,
                              this);

    const esp_timer_create_args_t periodic_timer_args = {
            .callback = PeriodicCallback,
            .arg = this,
            .dispatch_method =ESP_TIMER_ISR,
            .name = "Stepper",
            .skip_unhandled_events = false
    };


    ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &Timer));
}

void Stepper::Move(int32_t steps, uint32_t speed) {
    constexpr uint32_t ConversionConstant = 1000000 / 2;
    constexpr TickType_t AccTime = pdMS_TO_TICKS(100);
    constexpr uint32_t AccSteps = 5;
    esp_timer_stop(Timer);
    vTaskDelay(1);
    IsMoving = true;
    LastStepLevel = 0;
    StepCount = 0;
    SetDirection(steps > 0);
    auto stepsAbs = abs(steps);
    DesiredSteps = stepsAbs;
    auto period = (uint64_t) ConversionConstant / speed;
    auto power = (int32_t) pow(2, AccSteps);
    for (int i = 0; i < AccSteps; ++i) {
        ESP_ERROR_CHECK(esp_timer_start_periodic(Timer, period * power));
        vTaskDelay(AccTime);
        ESP_ERROR_CHECK(esp_timer_stop(Timer));
        power /= 2;
        vTaskDelay(1);
    }
    ESP_ERROR_CHECK(esp_timer_start_periodic(Timer, period));
}

void Stepper::Stop() {
    esp_timer_stop(Timer);
    IsMoving = false;
    DesiredSteps = 0;
    SetDirection(true);
}
