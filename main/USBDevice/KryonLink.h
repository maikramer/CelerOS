#ifndef KRYON_LINK_H
#define KRYON_LINK_H

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/stream_buffer.h"

// Protocolo KryonLink: canal binario da ferramenta kryonctl sobre a CDC1.
//
// Frame (little-endian):
//   [0x4B 'K'][cmd u8][len u16][payload de len bytes]
//
// Resposta reusa o opcode do pedido; o payload de resposta comeca com
// u8 status (0 = OK, 1 = erro; no erro o resto do payload e a mensagem).
//
// ESTE ARQUIVO E A FONTE UNICA DOS OPCODES: tools/kryonctl.py extrai os
// valores por regex para manter os dois lados em sincronia.

class KryonLink {
public:
    // Loop da task: consome bytes do stream buffer da CDC1, monta frames,
    // executa e responde. Nunca retorna.
    static void run(StreamBufferHandle_t rx);

    static constexpr uint16_t MAX_PAYLOAD = 4096;  // maior payload aceitado
};

// --- opcodes host -> device ---
constexpr uint8_t KL_HELLO = 0x01;        // payload "KRYONCTL1" -> identificacao do device
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

// --- opcodes device -> host ---
constexpr uint8_t KL_EXEC_CONT = 0x0E;    // continuacao da saida do EXEC (dados puros)

#endif // KRYON_LINK_H
