#ifndef HOST_LINK_H
#define HOST_LINK_H

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/stream_buffer.h"

// Protocolo HostLink: canal binario da ferramenta celerctl sobre a CDC1
// nativa ou a UART do CH340 (multiplexada com o console).
//
// Frame (little-endian):
//   [0x43 'C'][cmd u8][len u16][payload de len bytes]
//
// Resposta reusa o opcode do pedido; o payload de resposta comeca com
// u8 status (0 = OK, 1 = erro; no erro o resto do payload e a mensagem).
//
// ESTE ARQUIVO E A FONTE UNICA DOS OPCODES: tools/celerctl.py extrai os
// valores por regex para manter os dois lados em sincronia.
//
// Cada transporte (UART0 do SerialLink, CDC1 do USBDevice) cria a SUA
// instancia — a maquina de frames e por canal. A sessao de comandos e
// unica no device: o frame HELLO define o canal ativo (o ultimo ganha) e
// respostas, logs (KL_LOG_DATA) e demais frames de saida seguem sempre
// pelo canal que abriu a sessao. Comandos por canal inativo recebem erro.

class HostLink {
public:
    // Funcao de escrita no canal (CDC nativo, UART do CH340, ...)
    typedef bool (*WriteFn)(const uint8_t* data, size_t len);
    // Hook de troca de baud (so faz sentido em canais com baud, ex. UART)
    typedef void (*BaudFn)(uint32_t baud);

    HostLink(WriteFn writer, BaudFn baudHook = nullptr);

    // Loop da task do canal CDC (USB nativo): consome o stream buffer e
    // alimenta a maquina de frames. Nunca retorna.
    void run(StreamBufferHandle_t rx);

    // Maquina de estados alimentada byte a byte (UART e outros canais).
    // Ao completar um frame, executa e responde.
    void feed(uint8_t byte);

    // Encerra a sessao se ela pertence a esta instancia (timeout de idle
    // do canal UART, por exemplo). Retorna true se havia sessao.
    bool endSession();

    // Canal da sessao vigente (nullptr = nenhuma).
    static HostLink* active();

    // Envia um frame KL_LOG_DATA pelo canal ativo (false se nao ha
    // sessao). Thread-safe: chamado pelo LogSink a partir de tasks
    // arbitrarias; nao passa pelos locks do dispatch (sem deadlock).
    static bool sendLogFrame(const char* line, size_t n);

    static constexpr uint16_t MAX_PAYLOAD = 4096;  // maior payload aceitado

private:
    // Valida o canal, abre/troca a sessao no HELLO e despacha o comando.
    void process(uint8_t cmd, const uint8_t* payload, uint16_t len);

    WriteFn m_writer;
    BaudFn m_baudHook;

    // parser de frames (estado por instancia)
    uint8_t m_payload[MAX_PAYLOAD];
    enum { WANT_MAGIC, WANT_CMD, WANT_LEN_LO, WANT_LEN_HI, WANT_PAYLOAD } m_state;
    uint8_t m_cmd;
    uint16_t m_need = 0, m_got = 0;
    int64_t m_lastByteUs = 0;
};

// --- opcodes host -> device ---
constexpr uint8_t KL_HELLO = 0x01;        // payload "CELERCTL1" -> identificacao do device
constexpr uint8_t KL_INFO = 0x02;         // -> JSON versao/board/heap/fs
constexpr uint8_t KL_LS = 0x03;           // path -> u16 n + entradas {isDir,size,mtime,name}
constexpr uint8_t KL_STAT = 0x04;         // path -> exists,isDir,u32 size,u32 mtime
constexpr uint8_t KL_READ = 0x05;         // path\0 + u32 offset + u32 len -> dados
constexpr uint8_t KL_WRITE_BEGIN = 0x06;  // path -> abre escrita (cria diretorios pais)
constexpr uint8_t KL_WRITE_CHUNK = 0x07;  // dados brutos -> append
constexpr uint8_t KL_WRITE_END = 0x08;    // -> fecha + u32 total escrito
constexpr uint8_t KL_DELETE = 0x09;       // path
constexpr uint8_t KL_MKDIR = 0x0A;        // path
constexpr uint8_t KL_RENAME = 0x0B;       // from\0 + to\0
constexpr uint8_t KL_EXEC = 0x0C;         // cmdline -> u8 exitCode + u32 len + saida
constexpr uint8_t KL_REBOOT = 0x0D;       // ack e reinicia
constexpr uint8_t KL_SET_BAUD = 0x0F;     // u32 baud -> troca o baud do canal (UART)
constexpr uint8_t KL_LOG_ON = 0x10;       // liga o stream de logs (logcat)
constexpr uint8_t KL_LOG_OFF = 0x11;      // desliga o stream de logs
constexpr uint8_t KL_OTA_BEGIN = 0x13;    // -> abre escrita na proxima particao OTA
constexpr uint8_t KL_OTA_CHUNK = 0x14;    // payload -> esp_ota_write
constexpr uint8_t KL_OTA_END = 0x15;      // -> fecha, valida e marca boot (sem reiniciar)
constexpr uint8_t KL_OTA_ABORT = 0x16;    // cancela escrita OTA
constexpr uint8_t KL_SCREENSHOT = 0x17;   // [u8 fmt: 1=RLE] -> u16 w + u16 h + u8 fmt, depois chunks KL_SCR_DATA
constexpr uint8_t KL_TOUCH = 0x19;        // u8 n + n × {u8 down,u16 x,u16 y,u16 delayMs}
constexpr uint8_t KL_COREDUMP = 0x1A;     // -> u32 size, depois chunks KL_COREDUMP_DATA (ELF da particao)

// --- opcodes device -> host ---
constexpr uint8_t KL_EXEC_CONT = 0x0E;    // continuacao da saida do EXEC (dados puros)
constexpr uint8_t KL_LOG_DATA = 0x12;     // linha de log (texto, com \n)
constexpr uint8_t KL_SCR_DATA = 0x18;     // continuacao do screenshot (RGB565 cru)
constexpr uint8_t KL_COREDUMP_DATA = 0x1B; // continuacao do coredump (binario ELF)

#endif // HOST_LINK_H
