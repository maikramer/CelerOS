#include "AudioPlayer.h"
#include "../Boards/Board.h"
#include "BoardIO.h"
#include <Arduino.h>
#include <cstdio>
#include <cstring>
#include "driver/i2s_std.h"
#include "esp_task_wdt.h"

namespace AudioPlayer {

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
    // "data" pode vir antes de "fmt ": volta ao inicio das amostras
    return fseek(f, out.dataOffset, SEEK_SET) == 0;
}

}  // namespace

WavError playWav(const char* path) {
    const BoardProfile& bp = Board::profile();
    const AudioI2sPins& p = bp.i2s;
    if (p.dout < 0) return WavError::NoAudio;

    FILE* f = fopen(path, "rb");
    if (f == nullptr) return WavError::OpenFailed;

    // Placa com codec: gravacao em curso tem prioridade (mesmo motivo do
    // toneI2s — o I2S0 compartilha bclk/ws/mclk com o I2S1 do mic)
    if (Board::profile().audioCodecWake != nullptr && BoardIO::micRecActive()) {
        fclose(f);
        return WavError::NoAudio;
    }

    WavInfo wi;
    if (!parseHeader(f, wi) ||
        (wi.channels != 1 && wi.channels != 2) ||
        wi.sampleRate < 8000 || wi.sampleRate > 48000 || wi.dataBytes < 4) {
        fclose(f);
        return WavError::BadHeader;
    }

    i2s_chan_config_t chanCfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    // I2S_NUM_0 fixo: o I2S1 fica reservado ao microfone (mesma regra do
    // toneI2s — NUM_AUTO podia roubar o canal RX dele)
    chanCfg.auto_clear = true;
    // DMA enxuto (4 descritores ~3,8 KB internos em vez dos 6 default):
    // com a RAM interna apertada (sdkconfig regenerado + API 17) o default
    // falhava em "allocate DMA buffer failed" e TODO playWav vinha false
    // (bancada 2026-10-02). 4 descritores bastam para o stream de 16 kHz.
    chanCfg.dma_desc_num = 4;
    i2s_chan_handle_t tx = nullptr;
    if (i2s_new_channel(&chanCfg, &tx, nullptr) != ESP_OK) { fclose(f); return WavError::NoAudio; }

    i2s_std_config_t stdCfg = {};
    stdCfg.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(wi.sampleRate);
    stdCfg.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    stdCfg.gpio_cfg.mclk = p.mclk >= 0 ? (gpio_num_t)p.mclk : I2S_GPIO_UNUSED;
    stdCfg.gpio_cfg.bclk = (gpio_num_t)p.bclk;
    stdCfg.gpio_cfg.ws = (gpio_num_t)p.lrc;
    stdCfg.gpio_cfg.dout = (gpio_num_t)p.dout;
    stdCfg.gpio_cfg.din = I2S_GPIO_UNUSED;
    if (i2s_channel_init_std_mode(tx, &stdCfg) != ESP_OK || i2s_channel_enable(tx) != ESP_OK) {
        i2s_del_channel(tx);
        fclose(f);
        return WavError::NoAudio;
    }

    const bool hasCodec = bp.audioCodecWake != nullptr;
    if (hasCodec) bp.audioCodecWake();
    if (bp.audioPaPin >= 0) {
        pinMode(bp.audioPaPin, OUTPUT);
        digitalWrite(bp.audioPaPin, HIGH);
        delay(2);  // PA estabiliza antes do primeiro sample
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
            i2s_channel_write(tx, frames, got * sizeof(frames[0]), &written, portMAX_DELAY);
            esp_task_wdt_reset();
            remaining -= got;
        }
    }
    free(src);
    free(frames);

    if (bp.audioPaPin >= 0) digitalWrite(bp.audioPaPin, LOW);
    if (hasCodec) bp.audioCodecSleep();
    i2s_channel_disable(tx);
    i2s_del_channel(tx);
    if (hasCodec) BoardIO::micPinsDirty();  // pins do I2S1 voltam mortos
    fclose(f);
    return WavError::None;
}

}  // namespace AudioPlayer
