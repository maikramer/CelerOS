'use strict';
// Encoder PNG minimalista (truecolor 8-bit, sem dependencias) — usado pelo
// scaffold (icone placeholder) e pelo emulador (snapshots do framebuffer).
// So ESCREVE PNG; decodificacao nao existe no SDK de proposito (apps sao
// preview, nao comparacao bit a bit com o device).

const zlib = require('zlib');

const CRC_TABLE = (() => {
    const t = new Int32Array(256);
    for (let n = 0; n < 256; n++) {
        let c = n;
        for (let k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320 ^ (c >>> 1)) : (c >>> 1);
        t[n] = c;
    }
    return t;
})();

function crc32(buf) {
    let c = -1;
    for (let i = 0; i < buf.length; i++) c = CRC_TABLE[(c ^ buf[i]) & 0xFF] ^ (c >>> 8);
    return (c ^ -1) >>> 0;
}

function chunk(type, data) {
    const len = Buffer.alloc(4);
    len.writeUInt32BE(data.length);
    const body = Buffer.concat([Buffer.from(type, 'ascii'), data]);
    const crc = Buffer.alloc(4);
    crc.writeUInt32BE(crc32(body));
    return Buffer.concat([len, body, crc]);
}

// pixels: Buffer RGBA (w*h*4, hasAlpha=true) ou RGB (w*h*3, hasAlpha=false)
function encodePng(w, h, pixels, hasAlpha) {
    const bpp = hasAlpha ? 4 : 3;
    const ihdr = Buffer.alloc(13);
    ihdr.writeUInt32BE(w, 0);
    ihdr.writeUInt32BE(h, 4);
    ihdr[8] = 8;                       // bit depth
    ihdr[9] = hasAlpha ? 6 : 2;        // color type: RGBA | RGB
    // scanlines com filtro "none" (nivel 9 do deflate compensa)
    const raw = Buffer.alloc((w * bpp + 1) * h);
    for (let y = 0; y < h; y++) {
        raw[y * (w * bpp + 1)] = 0;
        pixels.copy(raw, y * (w * bpp + 1) + 1, y * w * bpp, (y + 1) * w * bpp);
    }
    const idat = zlib.deflateSync(raw, { level: 9 });
    return Buffer.concat([
        Buffer.from([0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A]),
        chunk('IHDR', ihdr),
        chunk('IDAT', idat),
        chunk('IEND', Buffer.alloc(0)),
    ]);
}

module.exports = { encodePng };
