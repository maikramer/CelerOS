"""CelerNet: quadro da malha por flood de advertising (porte do NetFrame.h).

Porte fiel 1:1 de main/Bluetooth/NetFrame.h (v2) — cada no anuncia pacotes
ADV_NONCONN de 31 bytes e escuta o ar; quem ouve um quadro novo repete com
ttl-1. O quadro vai CRU no advertising data, sem estrutura AD (o magic
'C''N' no byte 0 decide o que e nosso; scanners de fora ignoram payload
desconhecido — e por isso que o bleak/BlueZ nao ve a malha).

Layout v2 (15 bytes de cabecalho + dados, total <= 31):
  'C''N' | ver(1) | tipo(1) | netId(2) | src(2) | dst(2) | seq(2) | ttl(1)
        | hops(1) | dlen(1) | dados(dlen)

  ver   = 2 (v1 nao tinha dst)
  tipo  = BEAT (presenca: [caps(1)][nome]) | DATA (mensagem inteira, <=16 B)
         | FRAG (fragmento: [idx(1)][total(1)][chunk(<=14 B)]); mensagem de
         ate 434 B = 31 fragmentos com o MESMO (src, seq)
  netId = FNV-1a 16 bits do nome da rede
  src   = 2 ultimos bytes da MAC BT (identidade do no, zero config)
  dst   = destino do unicast; 0xFFFF = broadcast (o repetidor NAO filtra
          por dst — so a ENTREGA e do destinatario)
  seq   = contador u16 por no; dedup por (src, seq, idx)

Os testes (test/test_matilha.py) cruzam estes vetores com os do test/cpp
(run_tests.cpp, testNetFrame) — a compatibilidade binaria com o firmware e
o contrato deste modulo.
"""

MAGIC = b"CN"
VERSION = 2
HDR = 15                    # cabecalho inteiro (ate dlen)
ADV_MAX = 31                # payload do ADV_NONCONN legado
DATA_MAX = ADV_MAX - HDR    # 16 bytes de dados
DST_BROADCAST = 0xFFFF
TYPE_BEAT = 0               # presenca: [caps(1)][nome]
TYPE_DATA = 1               # mensagem que cabe inteira (<= 16 B)
TYPE_FRAG = 2               # fragmento de mensagem maior
CHUNK_MAX = DATA_MAX - 2    # 14 (idx + total + chunk)
MAX_FRAGS = 31              # 31 x 14 = 434 (teto da mensagem)
MSG_MAX = 434
TTL_MAX = 8
TTL_DEFAULT = 4

# caps do BEAT: papel do no na matilha (bitmask nos dados do BEAT).
CAPS_SPEAKER = 0x01   # tem alto-falante (recebe musica)
CAPS_MIC = 0x02       # tem microfone
CAPS_DISPLAY = 0x04   # tem tela (matilha mostra nele)
CAPS_MOTORS = 0x08    # se move (patas/motores)
CAPS_LEDS = 0x10      # matriz/fitas de LED
CAPS_HUB = 0x20       # rede configurada (alcaca o hub)

CAPS_NAMES = (
    ("speaker", CAPS_SPEAKER),
    ("mic", CAPS_MIC),
    ("display", CAPS_DISPLAY),
    ("motors", CAPS_MOTORS),
    ("leds", CAPS_LEDS),
    ("hub", CAPS_HUB),
)


def caps_str(caps):
    """Bitmask do BEAT em nomes legiveis (\"speaker,display,hub\")."""
    return ",".join(n for n, bit in CAPS_NAMES if caps & bit) or "-"


class Frame:
    """Quadro decodificado; `data` e uma copia em bytes (nao um ponteiro)."""

    def __init__(self, type=TYPE_DATA, net_id=0, src=0, dst=DST_BROADCAST, seq=0,
                 ttl=TTL_DEFAULT, hops=0, data=b""):
        self.type = type
        self.net_id = net_id
        self.src = src
        self.dst = dst
        self.seq = seq
        self.ttl = ttl
        self.hops = hops
        self.data = bytes(data)

    @property
    def dlen(self):
        return len(self.data)

    def __repr__(self):
        return "Frame(tipo=%d net=%04X src=%04X dst=%04X seq=%d ttl=%d hops=%d dlen=%d)" % (
            self.type, self.net_id, self.src, self.dst, self.seq, self.ttl, self.hops,
            self.dlen)


def fnv16(s):
    """FNV-1a truncado para 16 bits (netId do nome da rede)."""
    if isinstance(s, str):
        s = s.encode()
    h = 2166136261
    for c in s:
        h ^= c
        h = (h * 16777619) & 0xFFFFFFFF
    h ^= h >> 16
    return h & 0xFFFF


def encode(f):
    """Serializa; devolve bytes do quadro (None = dados/ttl invalidos)."""
    dlen = len(f.data)
    if dlen > DATA_MAX or f.ttl == 0 or f.ttl > TTL_MAX:
        return None
    return (MAGIC + bytes([VERSION, f.type])
            + f.net_id.to_bytes(2, "big") + f.src.to_bytes(2, "big")
            + f.dst.to_bytes(2, "big") + f.seq.to_bytes(2, "big")
            + bytes([f.ttl, f.hops, dlen]) + f.data)


def decode(buf):
    """Reconhece e decodifica um quadro nosso (magic+versao+tamanhos+ttl)."""
    if len(buf) < HDR or len(buf) > ADV_MAX:
        return None
    if buf[0:2] != MAGIC or buf[2] != VERSION:
        return None
    dlen = buf[14]
    if dlen + HDR != len(buf):
        return None
    ttl = buf[12]
    if ttl == 0 or ttl > TTL_MAX:
        return None
    return Frame(type=buf[3],
                 net_id=(buf[4] << 8) | buf[5],
                 src=(buf[6] << 8) | buf[7],
                 dst=(buf[8] << 8) | buf[9],
                 seq=(buf[10] << 8) | buf[11],
                 ttl=ttl, hops=buf[13],
                 data=bytes(buf[HDR:]))


# ------------------------------------------------------------------- dedup

class DedupRing:
    """Chave de dedup (src, seq, idx): DATA/BEAT usam idx 0, FRAG usa o
    proprio idx. Ring de N entradas (potencia de 2): visto de novo = True."""

    def __init__(self, n=32):
        self.n = n
        self.e = []       # [(src, seq, idx)]
        self.pos = 0

    def seen(self, src, seq, idx):
        if (src, seq, idx) in self.e:
            return True
        if len(self.e) < self.n:
            self.e.append((src, seq, idx))
        else:
            self.e[self.pos] = (src, seq, idx)
            self.pos = (self.pos + 1) % self.n
        return False

    def clear(self):
        self.e = []
        self.pos = 0


# -------------------------------------------------------------- remontagem

class Reassembler:
    """Coleta os fragmentos de UMA mensagem (mesmos src/seq) e devolve a
    mensagem completa (bytes) quando o ultimo chega; None caso contrario.
    Slots parados a mais que o prazo viram livres (frag perdido nao trava o
    proximo)."""

    SLOTS = 4

    def __init__(self):
        self.slots = {}    # (src, seq) -> dict(total, got_mask, chunks, last_ms)

    def prune(self, now_ms, timeout_ms):
        for k in [k for k, s in self.slots.items() if now_ms - s["last_ms"] > timeout_ms]:
            del self.slots[k]

    def feed(self, src, seq, idx, total, chunk, now_ms):
        """Alimenta um fragmento; bytes = mensagem completa remontada."""
        if total == 0 or total > MAX_FRAGS or idx >= total or len(chunk) > CHUNK_MAX:
            return None
        s = self.slots.get((src, seq))
        if s is not None and s["total"] != total:
            return None    # colisao de seq: descarta
        if s is None:
            if len(self.slots) >= self.SLOTS:
                return None   # sem slot: 4 mensagens grandes em voo
            s = self.slots[(src, seq)] = {"total": total, "chunks": {}, "last_ms": now_ms}
        s["last_ms"] = now_ms
        if idx in s["chunks"]:
            return None    # repetido
        if idx * CHUNK_MAX + len(chunk) > MSG_MAX:
            return None
        if idx + 1 < total and len(chunk) != CHUNK_MAX:
            return None    # so o ultimo pode ser curto
        s["chunks"][idx] = bytes(chunk)
        if len(s["chunks"]) != total:
            return None
        out = b"".join(s["chunks"][i] for i in range(total))
        del self.slots[(src, seq)]
        return out

    def reset(self):
        self.slots = {}
