#ifndef CELER_LINK_H
#define CELER_LINK_H

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/stream_buffer.h"

// Protocolo CelerLink: canal binario da ferramenta celerctl sobre a CDC1.
//
// Frame (little-endian):
//   [0x43 'C'][cmd u8][len u16][payload de len bytes]
//
// Resposta reusa o opcode do pedido; o payload de resposta comeca com
// u8 status (0 = OK, 1 = erro; no erro o resto do payload e a mensagem).
//
// ESTE ARQUIVO E A FONTE UNICA DOS OPCODES: tools/celerctl.py extrai os
// valores por regex para manter os dois lados em sincronia.

class CelerLink {
public:
    // Funcao de escrita no canal (CDC nativo, UART do CH340, ...)
    typedef bool (*WriteFn)(const uint8_t* data, size_t len);
    // Hook de troca de baud (so faz sentido em canais com baud, ex. UART)
    typedef void (*BaudFn)(uint32_t baud);

    static void setWriter(WriteFn fn);
    static void setBaudHook(BaudFn fn);

    // Loop da task do canal CDC (USB nativo): consome o stream buffer e
    // alimenta a maquina de frames. Nunca retorna.
    static void run(StreamBufferHandle_t rx);

    // Maquina de estados alimentada byte a byte (UART e outros canais).
    // Ao completar um frame, executa e responde.
    static void feed(uint8_t byte);

    static constexpr uint16_t MAX_PAYLOAD = 4096;  // maior payload aceitado
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

#endif // CELER_LINK_H
