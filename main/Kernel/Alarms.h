#ifndef CELER_KERNEL_ALARMS_H
#define CELER_KERNEL_ALARMS_H

// Agendador de alarmes do sistema (API 15): ate MAX alarmes persistidos no
// NVS (dias da semana, rotulo, ativo), um timer de contagem regressiva e uma
// soneca. Substitui o alarme unico em RAM da API 12 (TimeManager::setAlarm
// continua como compat e mapeia para o slot 0).
//
// Disparo: tick() roda 1x/s no loop da UI E no present() dos apps; um evento
// vencido vira o "toque pendente" (ring) que a AlarmScreen consome — com app
// aberto o present() pede a saida do app para a tela de alarme aparecer.
// deepSleepNow() acorda por timer no proximo evento (nextEvent).

#include "../Utils/AlarmCalc.h"
#include <stdint.h>
#include <string>
#include <time.h>

namespace Alarms {

constexpr int MAX = 8;
constexpr int SNOOZE_MIN = 5;

enum class Kind : uint8_t { Alarm, Timer, Snooze };

struct Ring {
    Kind kind = Kind::Alarm;
    int id = -1;          // slot do alarme (Alarm/Snooze); -1 timer
    std::string label;
};

void init();  // le o NVS (chamar apos o FileSystem/NVS)

// CRUD. id = slot 0..MAX-1. add devolve o slot ou -1 (cheio/invalido).
int add(const celer::AlarmSpec& a);
bool set(int id, const celer::AlarmSpec& a);
bool remove(int id);
bool get(int id, celer::AlarmSpec& out);

// Timer de contagem regressiva (um so). seconds 1..86400.
bool setTimer(uint32_t seconds, const std::string& label);
void cancelTimer();
// Segundos restantes (-1 sem timer); label preenchido quando ativo.
int32_t timerRemaining(std::string* label = nullptr);

// 1x/s: confere eventos vencidos; true quando ha toque pendente.
bool tick();
bool ringing(Ring* out = nullptr);
void dismiss();        // para o toque
void snooze();         // para e reagenda em SNOOZE_MIN

// Hora/fuso trocados a mao: conferencia recomeca de "agora" (pular o
// relogio para frente nao dispara os alarmes do intervalo).
void clockChanged();

// Epoch do proximo evento (alarme/timer/soneca); 0 = nenhum.
time_t nextEvent();

}  // namespace Alarms

#endif  // CELER_KERNEL_ALARMS_H
