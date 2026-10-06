// Task do System.playMusic (API 25): mistura o MusicEngine em blocos de
// 256 quadros e escreve no I2S0, no mesmo ciclo de canal do BoardIO::tone
// (canal PERSISTENTE do speakerChannel nas placas sem codec — cao e
// SmartDisplay; canal proprio + codec ES8311 acordado em volta no watch).
// A posse exclusiva (AudioPlayer::acquireOutput) e tomada AQUI na thread
// JS e devolvida pela task no fim: play() devolve false imediato se o
// alto-falante ja esta ocupado (fala do AI.speak, playWav, tom) e o
// detector de wake word dorme o tempo todo (AudioPlayer::active()).
#include "MusicSynth.h"

#include <Arduino.h>
#include <freertos/idf_additions.h>  // xTaskCreateWithCaps
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "AudioPlayer.h"
#include "BoardIO.h"
#include "Boards/Board.h"

namespace MusicSynth {
namespace {

// Bloco de mistura: 256 quadros mono -> estereo INTERCALADO (amp mono) em
// buffers estaticos — sem malloc no caminho quente (std::string/new que
// nao cresce ABORTA o aparelho nas placas sem PSRAM).
constexpr uint32_t kFrames = 256;
int16_t s_mono[kFrames];
int16_t s_frames[kFrames * 2];          // [L0,R0,L1,R1,...]
MusicEngine::Compiled s_compiled;       // .bss ~2,6 KB
MusicEngine::Renderer s_renderer;
MusicEngine::Song s_song;
volatile bool s_quit = false;
volatile bool s_active = false;
volatile uint32_t s_played = 0;         // amostras ja escritas no I2S
TaskHandle_t s_task = nullptr;
StaticSemaphore_t s_muBuf;
SemaphoreHandle_t s_mu = nullptr;

SemaphoreHandle_t mu() {
    if (s_mu == nullptr) s_mu = xSemaphoreCreateMutexStatic(&s_muBuf);
    return s_mu;
}

void taskFunc(void*) {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        const BoardProfile& bp = Board::profile();
        const bool hasCodec = bp.audioCodecWake != nullptr;
        const uint32_t rate = hasCodec ? 16000u : 44100u;

        xSemaphoreTake(mu(), portMAX_DELAY);
        const bool go = MusicEngine::compile(s_song, rate, s_compiled) &&
                        BoardIO::hasSpeaker() && !(hasCodec && BoardIO::micRecActive());
        xSemaphoreGive(mu());
        if (!go) { s_active = false; AudioPlayer::releaseOutput(); continue; }

        // Canal: espelha toneI2sOut — sem codec o persistente segue vivo
        // (o amp dessas placas so descansa com clocks rodando); com codec
        // o canal nasce por musica e morre no fim (pins emprestados do mic).
        i2s_chan_handle_t tx = nullptr;
        bool keep = false;
        if (hasCodec) {
            i2s_chan_config_t chanCfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
            chanCfg.auto_clear = true;
            chanCfg.dma_desc_num = 4;  // RAM enxuta, idem AudioPlayer
            if (i2s_new_channel(&chanCfg, &tx, nullptr) != ESP_OK) tx = nullptr;
            if (tx != nullptr) {
                i2s_std_config_t stdCfg = {};
                stdCfg.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(rate);
                stdCfg.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
                    I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
                stdCfg.gpio_cfg.mclk = bp.i2s.mclk >= 0 ? (gpio_num_t)bp.i2s.mclk
                                                        : I2S_GPIO_UNUSED;
                stdCfg.gpio_cfg.bclk = (gpio_num_t)bp.i2s.bclk;
                stdCfg.gpio_cfg.ws = (gpio_num_t)bp.i2s.lrc;
                stdCfg.gpio_cfg.dout = (gpio_num_t)bp.i2s.dout;
                stdCfg.gpio_cfg.din = I2S_GPIO_UNUSED;
                if (i2s_channel_init_std_mode(tx, &stdCfg) != ESP_OK ||
                    i2s_channel_enable(tx) != ESP_OK) {
                    i2s_del_channel(tx);
                    tx = nullptr;
                }
            }
        } else {
            tx = BoardIO::speakerChannel((int)rate);
            keep = tx != nullptr;
        }
        if (tx == nullptr) { s_active = false; AudioPlayer::releaseOutput(); continue; }

        if (hasCodec) bp.audioCodecWake();
        if (bp.audioPaPin >= 0) {
            pinMode(bp.audioPaPin, OUTPUT);
            digitalWrite(bp.audioPaPin, HIGH);
            delay(2);  // PA estabiliza antes do primeiro sample
        }

        s_renderer.reset(&s_compiled, BoardIO::volumePct());
        const uint32_t total = s_compiled.totalSamples;
        while (!s_quit && s_renderer.pos < total) {
            s_renderer.render(s_mono, kFrames);
            for (uint32_t i = 0; i < kFrames; i++) {
                s_frames[i * 2] = s_mono[i];
                s_frames[i * 2 + 1] = s_mono[i];
            }
            size_t written = 0;
            i2s_channel_write(tx, s_frames, sizeof(s_frames), &written,
                              pdMS_TO_TICKS(200));
            AudioPlayer::feedWatchdog();
            if (written == 0) break;  // canal morreu: sai limpo
            s_played += (uint32_t)(written / (2 * sizeof(int16_t)));
        }

        if (bp.audioPaPin >= 0) digitalWrite(bp.audioPaPin, LOW);
        if (hasCodec) bp.audioCodecSleep();
        if (!keep) {  // canal por musica (codec): desmonta e solta os pinos
            i2s_channel_disable(tx);
            i2s_del_channel(tx);
            BoardIO::speakerPinsPark();
            BoardIO::micPinsDirty();
        }
        // persistente (keep): fica vivo no silencio (auto_clear), idem tom
        s_active = false;
        AudioPlayer::releaseOutput();
    }
}

}  // namespace

bool play(const MusicEngine::Song& song) {
    if (!BoardIO::hasSpeaker()) return false;
    xSemaphoreTake(mu(), portMAX_DELAY);
    if (s_task == nullptr) {  // criacao sob o lock: sem janela de task dupla
        // Stack na PSRAM quando existe (a interna e o bem escasso do cao
        // com NimBLE + TLS ativos), com fallback comum
        BaseType_t okc = xTaskCreateWithCaps(taskFunc, "celermusic", 4096, nullptr,
                                             4, &s_task,
                                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (okc != pdPASS)
            okc = xTaskCreate(taskFunc, "celermusic", 3072, nullptr, 4, &s_task);
        if (okc != pdPASS) {
            s_task = nullptr;
            xSemaphoreGive(mu());
            return false;
        }
    }
    // Posse exclusiva AQUI (nao na task): retorno false imediato quando o
    // alto-falante esta ocupado — a task so devolve a guarda no fim.
    if (!AudioPlayer::acquireOutput()) {
        xSemaphoreGive(mu());
        return false;
    }
    s_song = song;
    s_quit = false;
    s_active = true;
    s_played = 0;
    xSemaphoreGive(mu());
    xTaskNotifyGive(s_task);
    return true;
}

void stop() { s_quit = true; }

bool playing() { return s_active; }

int32_t posMs() {
    if (!s_active || s_compiled.rate == 0) return -1;
    return (int32_t)(s_played * 1000ULL / s_compiled.rate);
}

}  // namespace MusicSynth
