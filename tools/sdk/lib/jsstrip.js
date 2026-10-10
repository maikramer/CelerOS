'use strict';
// jsstrip.js — porte 1:1 do JsStripper do firmware (main/Utils/JsStrip.h):
// remove comentarios, indentacao e espacos repetidos/finais, PRESERVANDO
// todas as quebras de linha (o "line N" do Duktape continua batendo com o
// fonte). Opera em bytes (Buffer) para dar exatamente a saida do C++.
//
// Uso: o `celerhub publish-dep` sobe a dep ja enxuta (o hub mede o .js
// recebido no teto de compile; o aparelho enxuga do mesmo jeito antes de
// compilar, entao o tamanho medido passa a ser o custo REAL em RAM) e o
// test/sdk confere o teto das deps com ele.
//
//   node tools/sdk/lib/jsstrip.js ARQ.js > ARQ.min.js

const KW = new Set(['return', 'typeof', 'case', 'do', 'else', 'in', 'new', 'void',
                    'delete', 'throw', 'instanceof']);
const S_CODE = 0, S_STR = 1, S_LINE = 2, S_BLOCK = 3, S_REGEX = 4;

function isIdent(c) {
    return (c >= 0x61 && c <= 0x7a) || (c >= 0x41 && c <= 0x5a) || (c >= 0x30 && c <= 0x39) ||
           c === 0x5f || c === 0x24 || c >= 0x80;
}

function strip(input) {
    const src = Buffer.isBuffer(input) ? input : Buffer.from(String(input), 'utf8');
    const out = Buffer.alloc(src.length + 1);
    let n = 0;
    let s = S_CODE, quote = 0, esc = false, star = false, blockNl = false, reClass = false;
    let slash = false, lineStart = true, space = false, last = 0;
    let word = '', wlen = 0;   // wlen -1: palavra longa demais (nao e keyword)

    function emit(c) { out[n++] = c; }
    function endWord() {
        if (wlen === 0) return;
        last = 0x61;   // 'a'
        if (wlen > 0 && KW.has(word)) last = 0x6b;   // 'k'
        wlen = 0;
        word = '';
    }
    function regexAllowed() {
        if (last === 0 || last === 0x6b) return true;
        if (last === 0x61 || last === 0x29 || last === 0x5d) return false;
        return true;
    }
    function code(c) {
        if (space && !lineStart) emit(0x20);
        space = false;
        lineStart = false;
        emit(c);
    }
    function newline() {
        space = false;
        lineStart = true;
        emit(0x0a);
    }
    function step(c) {
        switch (s) {
            case S_STR:
                emit(c);
                if (esc) esc = false;
                else if (c === 0x5c) esc = true;
                else if (c === quote) { s = S_CODE; last = 0x61; }
                else if (c === 0x0a) { s = S_CODE; lineStart = true; }
                return;
            case S_LINE:
                if (c === 0x0a) { s = S_CODE; newline(); }
                return;
            case S_BLOCK:
                if (c === 0x0a) { newline(); blockNl = true; }
                else if (star && c === 0x2f) {
                    s = S_CODE;
                    if (!blockNl) space = true;
                    return;
                }
                star = c === 0x2a;
                return;
            case S_REGEX:
                emit(c);
                if (esc) esc = false;
                else if (c === 0x5c) esc = true;
                else if (c === 0x5b) reClass = true;
                else if (c === 0x5d) reClass = false;
                else if (c === 0x2f && !reClass) { s = S_CODE; last = 0x61; }
                else if (c === 0x0a) { s = S_CODE; lineStart = true; }
                return;
        }
        if (slash) {
            slash = false;
            if (c === 0x2f) { endWord(); s = S_LINE; return; }
            if (c === 0x2a) { endWord(); s = S_BLOCK; star = false; blockNl = false; return; }
            endWord();
            const re = regexAllowed();
            code(0x2f);
            if (re) {
                s = S_REGEX;
                esc = false;
                reClass = false;
                step(c);
                return;
            }
            last = 0x2f;
        }
        if (isIdent(c)) {
            if (wlen >= 0) {
                if (wlen < 11) { word += String.fromCharCode(c); wlen++; }
                else { wlen = -1; word = ''; }
            }
            code(c);
            return;
        }
        if (wlen !== 0) {
            if (wlen < 0) { wlen = 0; last = 0x61; }
            else endWord();
        }
        switch (c) {
            case 0x20: case 0x09: case 0x0d:
                if (!lineStart) space = true;
                return;
            case 0x0a:
                newline();
                return;
            case 0x2f:
                slash = true;
                return;
            case 0x22: case 0x27:
                code(c);
                s = S_STR;
                quote = c;
                esc = false;
                return;
            default:
                code(c);
                last = c;
        }
    }
    for (let i = 0; i < src.length; i++) step(src[i]);
    endWord();
    if (slash) { slash = false; code(0x2f); }
    return out.subarray(0, n);
}

module.exports = { strip };

if (require.main === module) {
    const f = process.argv[2];
    if (!f) {
        console.error('uso: node jsstrip.js ARQ.js');
        process.exit(2);
    }
    process.stdout.write(strip(require('fs').readFileSync(f)));
}
