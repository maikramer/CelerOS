'use strict';
// Gerador do celer.d.ts (autocomplete no editor) a partir do manifest do
// firmware (tools/app_lint) e dos headings do guia pt-BR (nomes de args e
// resumo). Artefato GERADO — regenerar com `celer.js types` apos mudar a API.

const fs = require('fs');
const path = require('path');
const { buildManifest } = require('../../app_lint/lint.js');

const GUIDE_PT = path.resolve(__dirname, '../../../Documentation/JS_API_Guide.pt-BR.md');

// Overrides de tipo onde a assinatura generica mentiria. O resto dos
// retornos com valor vira `any` (honesto e nao trava o autocomplete).
const RETURNS = {
    'System.theme': 'CelerTheme',
    'System.getTouch': 'TouchPoint',
    'System.millis': 'number',
    'System.micros': 'number',
    'System.textWidth': 'number',
    'FS.readTextFile': 'string | null',
    'FS.listDir': 'string[]',
};

// Nomes de argumento que indicam string (o restante e number: a API e
// majoritariamente numerica — coordenadas, cores, tamanhos, ids).
function argType(name) {
    if (/^(s|str|txt|text|texto|name|nome|label|path|arq|arquivo|ssid|pass|senha|key|chave|url|host|body|json|msg|linha|code|cod|prompt|title|descricao|id)$/i.test(name)) return 'string';
    if (/^(on|ok|down|touched|enabled)$/i.test(name)) return 'boolean';
    return 'number';
}

// Headings `#### System.foo(a, b, cor)` (API N) -> args, nivel e resumo
// (primeira frase da linha "- **Descricao:** ..." do guia pt-BR).
function parseGuide(filePath) {
    const out = new Map();  // "System.foo" -> { args: [nomes], api, doc }
    if (!fs.existsSync(filePath)) return out;
    const lines = fs.readFileSync(filePath, 'utf8').split('\n');
    for (let i = 0; i < lines.length; i++) {
        const m = /^#{3,4}\s+`?([A-Za-z_$][\w$]*(?:\.[A-Za-z_$][\w$]*)*)\s*\(([^)]*)\)`?/.exec(lines[i]);
        if (!m) continue;
        const apiM = /\(API\s+(\d+)\)/.exec(lines[i]);
        // resumo: linha "- **Descricao:**" (+ 1 linha de continuacao indentada)
        let doc = '';
        for (let j = i + 1; j < Math.min(i + 12, lines.length); j++) {
            const t = lines[j];
            const dm = /^-\s*\*\*Descri(?:c|ç)(?:a|ã)o:\*\*\s*(.+)$/.exec(t);
            if (dm) {
                doc = dm[1].replace(/[`*]/g, '').trim();
                const nxt = lines[j + 1] || '';
                if (/^ {2,}\S/.test(nxt) && !/^ {2,}-/.test(nxt)) doc += ' ' + nxt.trim();
                break;
            }
            if (/^#{3,4}\s/.test(t)) break;  // heading seguinte sem descricao
        }
        doc = doc.replace(/\s+/g, ' ').slice(0, 140);
        const args = m[2] ? m[2].split(',').map((a) => a.trim()).filter(Boolean) : [];
        if (!out.has(m[1])) out.set(m[1], { args, api: apiM ? +apiM[1] : null, doc });
    }
    return out;
}

// Assinaturas exatas das funcoes globais (o guia nao usa prefixo de objeto,
// entao o parser de headings nao as cobre; timers API 12).
const GLOBAL_SIGS = {
    setTimeout: { params: ['callback: () => void', 'ms: number'], ret: 'number' },
    setInterval: { params: ['callback: () => void', 'ms: number'], ret: 'number' },
    clearTimeout: { params: ['id: number'], ret: 'void' },
    clearInterval: { params: ['id: number'], ret: 'void' },
};

function fnSignature(objPath, fn, guide) {
    if (objPath === 'global' && GLOBAL_SIGS[fn.name]) return { params: GLOBAL_SIGS[fn.name].params, ret: GLOBAL_SIGS[fn.name].ret, doc: '', api: fn.apiLevel || null };
    const g = guide.get(objPath + '.' + fn.name);
    const names = [];
    const max = Math.max(fn.nargs || 0, g ? g.args.length : 0, fn.min || 0);
    for (let i = 0; i < max; i++) {
        const nm = g && g.args[i] ? g.args[i].replace(/[\[\]=]/g, '') : ('arg' + i);
        names.push({ name: nm || ('arg' + i), optional: i >= (fn.min || 0) });
    }
    const params = names.map((n) => n.name + (n.optional ? '?' : '') + ': ' + argType(n.name));
    let ret = 'void';
    if (fn.returns) ret = RETURNS[objPath + '.' + fn.name] || 'any';
    return { params, ret, doc: g ? g.doc : '', api: g ? g.api : (fn.apiLevel || null) };
}

function renderObject(objPath, obj, guide, indent) {
    const pad = ' '.repeat(indent);
    const lines = [];
    for (const fn of obj.fns) {
        const sig = fnSignature(objPath, fn, guide);
        if (sig.doc) lines.push(pad + '/** ' + sig.doc + (sig.api ? ' (API ' + sig.api + ')' : '') + ' */');
        if (fn.perm) lines.push(pad + '/** @permission "' + fn.perm + '" */');
        lines.push(pad + fn.name + '(' + sig.params.join(', ') + '): ' + sig.ret + ';');
    }
    for (const c of obj.consts || []) {
        lines.push(pad + '/** constante */');
        lines.push(pad + c.name + ': number;');
    }
    return lines;
}

function generate() {
    const manifest = buildManifest();
    const guide = parseGuide(GUIDE_PT);

    // arvore de objetos: "System" com filho "System.gpio"
    const roots = {};
    for (const [objPath, obj] of Object.entries(manifest.objects)) {
        const parts = objPath.split('.');
        if (parts.length === 1) roots[objPath] = { path: objPath, obj, children: [] };
    }
    for (const [objPath, obj] of Object.entries(manifest.objects)) {
        const parts = objPath.split('.');
        if (parts.length > 1 && roots[parts[0]]) roots[parts[0]].children.push({ path: objPath, obj });
    }

    const out = [];
    out.push('// celer.d.ts — tipos da API JS do CelerOS para o editor (IntelliSense).');
    out.push('// ARTEFATO GERADO por `node tools/sdk/celer.js types` (manifest do firmware');
    out.push('// + guia pt-BR). Nao editar a mao; o `celer.js check` acusa drift.');
    out.push('// API level ' + manifest.apiLevel + ' — ' + manifest.counts.functions + ' funcoes.');
    out.push('');
    out.push('interface CelerTheme {');
    out.push('    bg: number; card: number; raised: number; stroke: number;');
    out.push('    accent: number; accentD: number; onAccent: number;');
    out.push('    text: number; textDim: number; ok: number; warn: number; err: number;');
    out.push('}');
    out.push('');
    out.push('interface TouchPoint { x: number; y: number; touched: boolean; }');
    out.push('');
    // funcoes globais (ex.: timers API 12): setTimeout e irmãos no escopo global
    if (manifest.objects.global) {
        for (const fn of manifest.objects.global.fns) {
            const sig = fnSignature('global', fn, guide);
            if (sig.doc) out.push('/** ' + sig.doc + (sig.api ? ' (API ' + sig.api + ')' : '') + ' */');
            out.push('declare function ' + fn.name + '(' + sig.params.join(', ') + '): ' + sig.ret + ';');
        }
        out.push('');
    }
    for (const root of Object.values(roots)) {
        if (root.path === 'global') continue;
        const isLink = root.path === 'CelerLink';
        out.push('declare const ' + root.path + (isLink ? ': ' + root.path + 'Api | undefined' : ': {') );
        if (!isLink) out.push(...renderObject(root.path, root.obj, guide, 4));
        for (const child of root.children) {
            out.push('    ' + child.path.split('.').pop() + ': {');
            out.push(...renderObject(child.path, child.obj, guide, 8));
            out.push('    };');
        }
        if (!isLink) out.push('};');
        if (isLink) {
            out.push('');
            out.push('interface ' + root.path + 'Api {');
            out.push(...renderObject(root.path, root.obj, guide, 4));
            out.push('}');
        }
        out.push('');
    }
    for (const c of manifest.globalConsts) {
        out.push('declare const ' + c.name + ': number;');
    }
    out.push('');
    if (manifest.warnings.length) {
        out.push('// avisos do manifest:');
        for (const w of manifest.warnings) out.push('// - ' + w);
        out.push('');
    }
    return out.join('\n');
}

module.exports = { generate, parseGuide };
