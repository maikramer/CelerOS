#include "AudioPlayer.h"
#include "../Boards/Board.h"
#include "BoardIO.h"
#include <Arduino.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include "driver/i2s_std.h"
#include "esp_task_wdt.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"

// QOA ("Quite OK Audio", phoboslab, MIT): decoder de ~60 linhas, ponto
// fixo, sem tabelas/malloc — 5x menor que PCM16 e MUITO mais leve que um
// decoder MP3 (sem componente, sem licenca, sem saga). O header de FRAME
// ja traz canais e taxa: nem precisa decodificar pra abrir o canal I2S.
#include "Audio/qoa.h"

namespace AudioPlayer {

// Corte por fora (AI.cancel/exit do app no meio da fala do TTS, que toca na
// worker do AI): os loops checam a cada chunk e o pedido limpa no inicio de
// cada playWav. s_playing garante UMA reproducao por vez — o TTS roda na
// worker do AI e um playWav da task JS nao pode escrever no mesmo canal I2S
// no meio dela (vira Busy; o app decide o fallback).
static std::atomic_bool s_stopReq{false};
static std::atomic_bool s_playing{false};

void requestStop() { s_stopReq = true; }

bool acquireOutput() {
    bool livre = false;
    return s_playing.compare_exchange_strong(livre, true);
}
void releaseOutput() { s_playing = false; }

bool active() { return s_playing; }  // detector de wake dorme enquanto toca

void feedWatchdog() {
    if (esp_task_wdt_status(nullptr) == ESP_OK) esp_task_wdt_reset();
}

namespace {

// little-endian helpers (WAV e LE por especificacao)
uint32_t rdU32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
uint16_t rdU16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }

struct WavInfo {
    uint32_t sampleRate = 0;
    uint16_t channels = 0;     // 1 ou 2
    long dataOffset = 0;       // onde comecam as amostras
    uint32_t dataBytes = 0;
};

// Percorre os chunks RIFF ate achar fmt (obrigatorio) e data. Sem parser de
// JSON/strings gigante: cabecalho WAV sao blocos de 8 bytes (id + tamanho).
// Arquivo nao confiavel: todo salto e para FRENTE e dentro do arquivo. Antes
// um tamanho >= 2^31 virava long negativo no fseek, o parser voltava ao
// mesmo cabecalho e o loop nunca terminava (watchdog reiniciava o aparelho).
bool parseHeader(FILE* f, WavInfo& out) {
    if (fseek(f, 0, SEEK_END) != 0) return false;
    const long fileSize = ftell(f);
    if (fileSize < 12 || fseek(f, 0, SEEK_SET) != 0) return false;

    uint8_t h[12];
    if (fread(h, 1, 12, f) != 12) return false;
    if (memcmp(h, "RIFF", 4) != 0 || memcmp(h + 8, "WAVE", 4) != 0) return false;

    bool haveFmt = false, haveData = false;
    uint8_t ch[8];
    for (;;) {
        const long pos = ftell(f);
        if (pos < 0 || fileSize - pos < 8 || fread(ch, 1, 8, f) != 8) break;
        const uint64_t sz = rdU32(ch + 4);
        const long body = pos + 8;                       // inicio do conteudo
        const uint64_t avail = (uint64_t)(fileSize - body);
        if (memcmp(ch, "fmt ", 4) == 0) {
            uint8_t fmt[16];
            if (sz < 16 || sz > avail || fread(fmt, 1, 16, f) != 16) return false;
            if (rdU16(fmt) != 1) return false;             // so PCM linear
            out.channels = rdU16(fmt + 2);
            out.sampleRate = rdU32(fmt + 4);
            if (rdU16(fmt + 14) != 16) return false;       // 16 bits
            haveFmt = true;
        } else if (memcmp(ch, "data", 4) == 0) {
            // data truncado (download pela metade): toca o que existe
            out.dataBytes = (uint32_t)(sz < avail ? sz : avail);
            out.dataOffset = body;
            haveData = true;
        }
        if (haveFmt && haveData) break;
        // proximo chunk: conteudo + padding par, sempre adiante
        const uint64_t next = (uint64_t)body + sz + (sz & 1);
        if (next >= (uint64_t)fileSize) break;
        if (fseek(f, (long)next, SEEK_SET) != 0) return false;
    }
    if (!haveFmt || !haveData) return false;
    // "data" pode vir antes do "fmt ": volta ao inicio das amostras
    return fseek(f, out.dataOffset, SEEK_SET) == 0;
}

// ---- canal de saida (WAV e MP3 compartilham) ------------------------------
// Sem codec o canal e PERSISTENTE (BoardIO::speakerChannel): o amp dessas
// placas nao tem pino de enable e so descansa frio recebendo silencio com
// os clocks correndo — apaga-lo por som deixou a bobina do cao cozinhando
// (bancada 2026-10-03). Com codec, o canal nasce por som e morre no fim
// (clocks emprestados do I2S1 do mic + micPinsDirty no teardown).
struct TxSink {
    i2s_chan_handle_t tx = nullptr;
    bool keep = false;   // canal persistente: fica ligado no silencio no fim
};

bool openTxSink(uint32_t sampleRate, TxSink& s) {
    const BoardProfile& bp = Board::profile();
    const AudioI2sPins& p = bp.i2s;
    if (bp.audioCodecWake != nullptr) {
        i2s_chan_config_t chanCfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
        // I2S_NUM_0 fixo: o I2S1 fica reservado ao microfone (mesma regra do
        // toneI2s — NUM_AUTO podia roubar o canal RX dele)
        chanCfg.auto_clear = true;
        // DMA enxuto (4 descritores ~3,8 KB internos em vez dos 6 default):
        // com a RAM interna apertada (sdkconfig regenerado + API 17) o default
        // falhava em "allocate DMA buffer failed" e TODO playWav vinha false
        // (bancada 2026-10-02). 4 descritores bastam para o stream de 16 kHz.
        chanCfg.dma_desc_num = 4;
        if (i2s_new_channel(&chanCfg, &s.tx, nullptr) != ESP_OK) return false;

        i2s_std_config_t stdCfg = {};
        stdCfg.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sampleRate);
        stdCfg.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
        stdCfg.gpio_cfg.mclk = p.mclk >= 0 ? (gpio_num_t)p.mclk : I2S_GPIO_UNUSED;
        stdCfg.gpio_cfg.bclk = (gpio_num_t)p.bclk;
        stdCfg.gpio_cfg.ws = (gpio_num_t)p.lrc;
        stdCfg.gpio_cfg.dout = (gpio_num_t)p.dout;
        stdCfg.gpio_cfg.din = I2S_GPIO_UNUSED;
        if (i2s_channel_init_std_mode(s.tx, &stdCfg) != ESP_OK || i2s_channel_enable(s.tx) != ESP_OK) {
            i2s_del_channel(s.tx);
            s.tx = nullptr;
            return false;
        }
    } else {
        s.tx = BoardIO::speakerChannel((int)sampleRate);
        if (s.tx == nullptr) return false;
        s.keep = true;
    }
    if (bp.audioCodecWake != nullptr) bp.audioCodecWake();
    if (bp.audioPaPin >= 0) {
        pinMode(bp.audioPaPin, OUTPUT);
        digitalWrite(bp.audioPaPin, HIGH);
        delay(2);  // PA estabiliza antes do primeiro sample
    }
    return true;
}

void closeTxSink(TxSink& s) {
    const BoardProfile& bp = Board::profile();
    if (bp.audioPaPin >= 0) digitalWrite(bp.audioPaPin, LOW);
    if (bp.audioCodecWake != nullptr) bp.audioCodecSleep();
    if (!s.keep && s.tx != nullptr) {  // persistente: auto_clear segue mandando silencio
        i2s_channel_disable(s.tx);
        i2s_del_channel(s.tx);
        BoardIO::speakerPinsPark();
    }
    if (bp.audioCodecWake != nullptr) BoardIO::micPinsDirty();  // pins do I2S1 voltam mortos
    s.tx = nullptr;
}

}  // namespace

// QOA em STREAMING: frame-a-frame do arquivo (header ja diz taxa/canais),
// decode no buffer e o MESMO caminho do WAV (mono duplicado + volume).
namespace {

WavError playQoaStream(FILE* f) {
    qoa_desc q = {};
    uint8_t fh[8];
    if (fread(fh, 1, 8, f) != 8 || memcmp(fh, "qoaf", 4) != 0) {
        fclose(f);
        return WavError::BadHeader;
    }
    // big endian (QOA e BE por especificacao)
    const uint32_t total = ((uint32_t)fh[4] << 24) | ((uint32_t)fh[5] << 16) | (fh[6] << 8) | fh[7];
    if (total == 0) {
        fclose(f);
        return WavError::BadHeader;
    }

    const uint32_t kFrameMax = 8 + 16 * 2 + 8 * 256 * 2;             // frame max stereo
    uint8_t* fbuf = (uint8_t*)malloc(kFrameMax);
    int16_t* pcm = (int16_t*)malloc(QOA_FRAME_LEN * 2 * sizeof(int16_t));   // 20 KB -> PSRAM
    int16_t (*frames)[2] = (int16_t(*)[2])malloc(256 * 2 * sizeof(int16_t));
    if (fbuf == nullptr || pcm == nullptr || frames == nullptr) {
        free(fbuf); free(pcm); free(frames);
        fclose(f);
        return WavError::NoAudio;
    }

    TxSink sink;
    const int vol = BoardIO::volumePct();
    uint32_t played = 0;
    while (played < total) {
        if (s_stopReq) break;  // corte pedido por fora (fala do TTS)
        // header do frame (8 B, BE): canais, taxa, amostras, tamanho
        uint8_t h[8];
        if (fread(h, 1, 8, f) != 8) break;
        const uint32_t channels = h[0];
        const uint32_t rate = ((uint32_t)h[1] << 16) | ((uint32_t)h[2] << 8) | h[3];
        const uint32_t samples = ((uint32_t)h[4] << 8) | h[5];
        const uint32_t frameSize = ((uint32_t)h[6] << 8) | h[7];
        if (channels < 1 || channels > 2 || rate < 8000 || rate > 48000 || samples == 0 ||
            frameSize < 8 + 16 * channels || frameSize > kFrameMax) break;
        fseek(f, -8, SEEK_CUR);  // qoa_decode_frame le o frame INTEIRO (header incluso)
        if (fread(fbuf, 1, frameSize, f) != frameSize) break;

        if (sink.tx == nullptr) {
            q.channels = channels;
            q.samplerate = rate;
            ESP_LOGI("celer.audio", "qoa: %u Hz ch=%u (%u amostras)", rate, channels, total);
            if (!openTxSink(rate, sink)) break;
        }
        unsigned frameLen = 0;
        if (qoa_decode_frame(fbuf, frameSize, &q, pcm, &frameLen) == 0 || frameLen == 0) break;

        for (uint32_t b = 0; b < frameLen; b += 256) {
            const uint32_t m = (frameLen - b < 256) ? frameLen - b : 256;
            for (uint32_t i = 0; i < m; i++) {
                const int32_t l = pcm[(b + i) * q.channels];
                const int32_t r = q.channels == 2 ? pcm[(b + i) * q.channels + 1] : l;
                frames[i][0] = (int16_t)((l * vol) / 100);
                frames[i][1] = (int16_t)((r * vol) / 100);
            }
            size_t written = 0;
            i2s_channel_write(sink.tx, frames, m * sizeof(frames[0]), &written, portMAX_DELAY);
            feedWatchdog();
        }
        played += frameLen;
    }

    if (sink.tx != nullptr) closeTxSink(sink);
    free(fbuf); free(pcm); free(frames);
    fclose(f);
    return WavError::None;
}

}  // namespace

static WavError playWavLocked(const char* path);

WavError playWav(const char* path) {
    const BoardProfile& bp = Board::profile();
    if (bp.i2s.dout < 0) return WavError::NoAudio;  // CYD: sem alto-falante

    bool livre = false;
    if (!s_playing.compare_exchange_strong(livre, true)) return WavError::Busy;
    s_stopReq = false;
    const WavError e = playWavLocked(path);
    s_playing = false;
    return e;
}

// Reproducao em si (a guarda de concorrencia fica no playWav publico).
static WavError playWavLocked(const char* path) {
    const BoardProfile& bp = Board::profile();
    const AudioI2sPins& p = bp.i2s;
    if (p.dout < 0) return WavError::NoAudio;

    FILE* f = fopen(path, "rb");
    if (f == nullptr) return WavError::OpenFailed;

    // Placa com codec: gravacao em curso tem prioridade (mesmo motivo do
    // toneI2s — o I2S0 compartilha bclk/ws/mclk com o I2S1 do mic)
    if (bp.audioCodecWake != nullptr && BoardIO::micRecActive()) {
        fclose(f);
        return WavError::NoAudio;
    }

    // Fareio o formato: RIFF..WAVE = WAV PCM; magica "qoaf" = QOA
    {
        uint8_t m[4];
        const size_t rn = fread(m, 1, 4, f);
        fseek(f, 0, SEEK_SET);
        if (rn == 4 && m[0] == 'q' && m[1] == 'o' && m[2] == 'a' && m[3] == 'f') {
            return playQoaStream(f);
        }
    }

    WavInfo wi;
    if (!parseHeader(f, wi) ||
        (wi.channels != 1 && wi.channels != 2) ||
        wi.sampleRate < 8000 || wi.sampleRate > 48000 || wi.dataBytes < 4) {
        fclose(f);
        return WavError::BadHeader;
    }

    TxSink sink;
    if (!openTxSink(wi.sampleRate, sink)) {
        fclose(f);
        return WavError::NoAudio;
    }

    // Streaming: le MONO/STEREO do arquivo em chunks e entrega ESTEREO ao
    // I2S (mono duplicado L/R, como o tom — o amp e mono nas placas atuais).
    // int16 puro + fread: sem std::string/new no caminho (regra do firmware).
    const int kFrames = 256;                    // 256 frames = 1 KB por canal de leitura
    int16_t* src = (int16_t*)malloc(kFrames * wi.channels * sizeof(int16_t));
    int16_t (*frames)[2] = (int16_t(*)[2])malloc(kFrames * 2 * sizeof(int16_t));
    const int vol = BoardIO::volumePct();

    if (src != nullptr && frames != nullptr) {
        uint32_t remaining = wi.dataBytes / (2u * wi.channels);  // em frames
        while (remaining > 0) {
            if (s_stopReq) break;  // corte pedido por fora (fala do TTS)
            uint32_t want = remaining < (uint32_t)kFrames ? remaining : kFrames;
            size_t got = fread(src, 2 * wi.channels, want, f);
            if (got == 0) break;
            for (uint32_t i = 0; i < got; i++) {
                int32_t l = src[i * wi.channels];
                int32_t r = wi.channels == 2 ? src[i * wi.channels + 1] : l;
                frames[i][0] = (int16_t)((l * vol) / 100);
                frames[i][1] = (int16_t)((r * vol) / 100);
            }
            size_t written = 0;
            i2s_channel_write(sink.tx, frames, got * sizeof(frames[0]), &written, portMAX_DELAY);
            feedWatchdog();
            remaining -= got;
        }
    }
    free(src);
    free(frames);

    closeTxSink(sink);
    fclose(f);
    return WavError::None;
}

// ---- PcmFeed: streaming ao vivo do TTS (AI.speak) --------------------------
// begin/write/end seguem a mesma disciplina do playWav: guarda de UMA
// reproducao por vez (s_playing), corte por fora checado por bloco e
// canal aberto/fechado pelo TxSink (codec acordado em volta, amp frio no
// silencio nas placas sem pino de enable).

// Reserva para (re)comecar: 200 ms. Na falta de dado o player PAUSA ate
// juntar isso de novo (ou o download acabar) — uma pausa limpa em vez de
// uma rajada de cliques de 10 ms.
static constexpr uint32_t kResumeMs = 200;

void PcmFeed::playerTask(void* arg) {
    PcmFeed* f = (PcmFeed*)arg;
    int16_t frames[256][2];  // bloco estereo (mono duplicado + volume)
    const size_t resume = (size_t)f->_rate * kResumeMs / 1000;
    bool starving = false;
    int64_t starveT0 = 0;
    while (!f->_quit) {
        if (!f->_go) {
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(20));
            continue;
        }
        if (s_stopReq) break;
        const size_t w = f->_w;
        __sync_synchronize();  // amostras publicadas antes do indice
        const size_t r = f->_r;
        const size_t avail = w - r;
        if (avail == 0 || (starving && avail < resume && !f->_eof)) {
            if (avail == 0 && f->_eof) break;  // fim: tocou tudo
            if (!starving) {  // engasgo: rede atras da voz
                starving = true;
                starveT0 = esp_timer_get_time();
                f->_starves = f->_starves + 1;
            }
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10));
            continue;
        }
        if (starving) {
            starving = false;
            f->_starveMs = f->_starveMs + (uint32_t)((esp_timer_get_time() - starveT0) / 1000);
        }
        const size_t m = avail < 256 ? avail : 256;
        for (size_t i = 0; i < m; i++) {
            const int32_t v = ((int32_t)f->_ring[(r + i) % f->_cap] * f->_vol) / 100;
            frames[i][0] = (int16_t)v;
            frames[i][1] = (int16_t)v;
        }
        __sync_synchronize();
        f->_r = r + m;  // espaco devolvido ao produtor
        size_t written = 0;
        i2s_channel_write(f->_tx, frames, m * sizeof(frames[0]), &written, portMAX_DELAY);
    }
    f->_done = true;
    vTaskSuspend(nullptr);  // stack na PSRAM: quem libera e o end()
}

bool PcmFeed::begin(uint32_t sampleRate, size_t capBytes) {
    const BoardProfile& bp = Board::profile();
    if (bp.i2s.dout < 0 || sampleRate < 8000 || sampleRate > 48000) return false;
    if (bp.audioCodecWake != nullptr && BoardIO::micRecActive()) return false;
    bool livre = false;
    if (!s_playing.compare_exchange_strong(livre, true)) return false;  // playWav em curso
    _held = true;  // deste ponto em diante o end() e obrigatorio
    // corte VELHO (cancel de uma fala que ja tinha acabado): sem limpar, todo
    // feed seguinte nascia morto ate algum playWav zerar a flag
    s_stopReq = false;
    _hasCarry = false;
    _rate = sampleRate;
    _w = 0;
    _r = 0;
    _go = false;
    _eof = false;
    _quit = false;
    _done = false;
    _starves = 0;
    _starveMs = 0;
    _cap = capBytes / 2;
    if (_cap < sampleRate) _cap = sampleRate;  // minimo 1 s de fila
    _ring = (int16_t*)heap_caps_malloc(_cap * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (_ring == nullptr) {
        end();
        return false;
    }
    TxSink s;
    if (!openTxSink(sampleRate, s)) {
        end();
        return false;
    }
    _tx = s.tx;
    _keep = s.keep;
    _vol = BoardIO::volumePct();
    // Player acima da worker do AI (3), do wake word (3) e da main (1): o
    // DMA do I2S segura so ~40 ms. Stack na PSRAM (a task nao toca a flash;
    // durante escrita de flash ninguem roda mesmo — o LogPersist segura o
    // flush enquanto toca).
    TaskHandle_t h = nullptr;
    if (xTaskCreatePinnedToCoreWithCaps(playerTask, "pcmfeed", 4096, this, 6, &h,
                                        tskNO_AFFINITY,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        end();
        return false;
    }
    _task = h;
    return true;
}

bool PcmFeed::write(const void* le16, size_t bytes) {
    if (_tx == nullptr || _ring == nullptr) return false;
    const uint8_t* p = (const uint8_t*)le16;
    size_t left = bytes;
    while (left > 0) {
        if (s_stopReq || _done) return false;  // corte pedido / player saiu
        // byte a byte (LE): o chunk HTTP nao garante tamanho par nem
        // alinhamento de 2 — o cast direto desalinhava a fala inteira
        if (!_hasCarry) {
            _carry = *p++;
            left--;
            _hasCarry = true;
            continue;
        }
        while (_w - _r >= _cap) {  // fila cheia: espera o player abrir espaco
            if (s_stopReq || _done) return false;
            if (!_go) start();      // fila cheia sem tocar: nao ha o que esperar
            vTaskDelay(pdMS_TO_TICKS(5));
        }
        _ring[_w % _cap] = (int16_t)(uint16_t)(_carry | ((uint16_t)*p++ << 8));
        left--;
        _hasCarry = false;
        __sync_synchronize();  // amostra visivel antes do indice
        _w = _w + 1;
    }
    if (_task != nullptr) xTaskNotifyGive((TaskHandle_t)_task);
    return true;
}

void PcmFeed::start() {
    if (_go || _task == nullptr) return;
    _go = true;
    xTaskNotifyGive((TaskHandle_t)_task);
}

uint32_t PcmFeed::bufferedMs() const {
    return _rate ? (uint32_t)((uint64_t)(_w - _r) * 1000 / _rate) : 0;
}

bool PcmFeed::finish() {
    if (_task == nullptr) return false;
    _eof = true;
    start();
    xTaskNotifyGive((TaskHandle_t)_task);
    // espera o player drenar: o que falta tocar + 3 s de folga
    const int64_t limite = esp_timer_get_time() + (int64_t)(bufferedMs() + 3000) * 1000;
    while (!_done && !s_stopReq && esp_timer_get_time() < limite) {
        vTaskDelay(pdMS_TO_TICKS(20));
        feedWatchdog();
    }
    const bool inteiro = _done && !s_stopReq && _w == _r;
    // ultimo bloco ainda no DMA (~40 ms): sem isto o canal com codec
    // fechava cortando a ultima silaba
    if (inteiro) vTaskDelay(pdMS_TO_TICKS(60));
    return inteiro;
}

void PcmFeed::end() {
    bool seguro = true;  // nada mais toca no canal/fila
    if (_task != nullptr) {
        _quit = true;
        xTaskNotifyGive((TaskHandle_t)_task);
        for (int i = 0; i < 100 && !_done; i++) vTaskDelay(pdMS_TO_TICKS(5));
        if (_done) {
            vTaskDeleteWithCaps((TaskHandle_t)_task);
        } else {
            // player preso no I2S (nao deve acontecer: o DMA sempre drena):
            // vaza task, fila, canal e guarda em vez de solta-los sob ele
            seguro = false;
            ESP_LOGE("celer.audio", "pcmfeed: player nao saiu; recursos retidos");
        }
        _task = nullptr;
    }
    if (!seguro) return;
    if (_tx != nullptr) {
        TxSink s{_tx, _keep};
        closeTxSink(s);
        _tx = nullptr;
    }
    free(_ring);
    _ring = nullptr;
    _cap = 0;
    if (_held) {  // so solta a guarda que o begin pegou (nunca a de um playWav)
        s_playing = false;
        _held = false;
    }
}

}  // namespace AudioPlayer
