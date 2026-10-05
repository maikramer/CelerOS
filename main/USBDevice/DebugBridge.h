#ifndef DEBUG_BRIDGE_H
#define DEBUG_BRIDGE_H

#include <stdint.h>

// Celer Debug Bridge: o canal do celerctl (HostLink proto 2) sobre TCP na
// rede WiFi — um 3o transporte ao lado da UART do SerialLink e da CDC1 do
// USBDevice. Mesmo protocolo, mesmos opcodes, zero mudanca no celerctl alem
// do transporte.
//
// A cada conexao o device manda o banner "CELERBRIDGE 1 <board> <ip> <porta>"
// e espera "AUTH <token>\n" (token em NVS, comando "bridge" do shell mostra;
// 3 erros ou 15 s sem autenticar = close). Apos o "OK\n" o socket vira um
// pipe binario de frames HostLink. Na MESMA porta, um socket UDP responde a
// sonda "CELERPROBE1" do "celerctl devices" (descoberta na LAN sem mDNS).
//
// O listener nasce com o WiFi STA conectado e morre com ele (PowerPolicy
// segura o radio enquanto ha cliente autenticado — ver PowerPolicy::tick).
// 1 cliente por vez: a sessao de comandos do HostLink e unica no device.
namespace DebugBridge {

// Boot (uma vez, depois do WebManager::startAsync): carrega/gera o token do
// NVS, cria o mutex de escrita e assina o evento de estado do NetworkManager
// (o handler so seta flag — roda na task sys_evt).
void begin();

// Servico ALWAYS: cria a task do listener na 1a conexao de rede (placas que
// nunca sobem WiFi nao pagam a stack da task).
void tick(bool inApp);

// Ha cliente TCP autenticado? (PowerPolicy adia o WiFi idle-off)
bool sessionActive();

// Token de pareamento (NVS ns "celer", key "bridge_token"; 8 chars gerados
// na 1a leitura). Ponteiro estatico — o caller copia imediatamente.
const char* token();
// Fixa um token escolhido (6..31 chars, sem espacos/controle) — o caminho
// de automacao do "celerctl provision". false = token invalido ou NVS fora.
bool tokenSet(const char* t);
// Gera um token novo (comando "bridge reset" do shell).
void tokenReset();

// Porta TCP/UDP configurada (CELEROS_DEBUG_BRIDGE_PORT).
uint16_t port();

// Estado para o comando "bridge" do shell.
bool listening();

}  // namespace DebugBridge

#endif  // DEBUG_BRIDGE_H
