#!/usr/bin/env node
// app_lint — "compilador" estatico de apps JS do CelerOS.
//
// Duas camadas:
//   1. Parser JavaScript de verdade (acorn vendorado, ecmaVersion 5 — o mesmo
//      perfil ES5 do Duktape do firmware): pega erro de sintaxe e uso de ES6.
//   2. Verificacao contra a API do projeto: funcoes, constantes, aridades,
//      permissoes, niveis de API e regras dos guias.
//
// O manifest da API e DERIVADO DO FONTE DO FIRMWARE a cada execucao — nenhuma
// lista manual para manter:
//   - nomes/aridades/namespaces: tabelas kFnsN[] de main/Runtime/JSBindings.cpp
//   - aridade minima: corpos C++ (duk_require_*) dos main/Runtime/Js*.cpp
//   - permissoes e gates: perm(celer::PERM_*) e #if CONFIG_* do registro
//   - CELEROS_API_LEVEL: main/CMakeLists.txt
//   - nivel de API por funcao (best-effort): headings do JS_API_Guide.pt-BR.md
//
// Uso:
//   node tools/app_lint/lint.js [caminhos...] [--json] [--strict]
//   node tools/app_lint/lint.js --dump-manifest
//   node tools/app_lint/lint.js check        # drift codigo x docs x harness
//
// Caminhos: pasta de app (com app.json), pasta com varios apps (data/apps)
// ou um .js avulso. Padrao: data/apps hub_apps.
// Exit: 0 limpo; 1 com erros (ou avisos, com --strict); 2 falha da ferramenta.

'use strict';

const fs = require('fs');
const path = require('path');
const acorn = require('./vendor/acorn.js');

const ROOT = path.resolve(__dirname, '..', '..');
const BINDINGS_CPP = path.join(ROOT, 'main', 'Runtime', 'JSBindings.cpp');
const RUNTIME_DIR = path.join(ROOT, 'main', 'Runtime');
const MAIN_CMAKE = path.join(ROOT, 'main', 'CMakeLists.txt');
const GUIDE_PT = path.join(ROOT, 'Documentation', 'JS_API_Guide.pt-BR.md');
const GUIDE_EN = path.join(ROOT, 'Documentation', 'JS_API_Guide.md');

// Limites de main.js derivados de tools/celerhub.py (quem recusa de verdade
// no publish). Antes eram duplicados aqui e podiam divergir em silencio.
function _hubLimit(name, fallback) {
  const m = fs.readFileSync(path.join(ROOT, 'tools', 'celerhub.py'), 'utf8')
    .match(new RegExp(name + '\\s*=\\s*([0-9xX*+\\s]+)'));
  if (!m) return fallback;
  // so digitos e operadores aritmeticos: seguro de avaliar
  return Function('"use strict"; return (' + m[1] + ')')() || fallback;
}
const MAX_MAIN_JS = _hubLimit('MAX_MAIN_JS', 48 * 1024);
const STREAM_SAFE_MAIN_JS = _hubLimit('STREAM_SAFE_MAIN_JS', 30 * 1024);

// Objetos JS da API (raizes validas de cadeia de membro).
const NAMESPACE_ROOTS = ['System', 'Net', 'FS', 'CelerLink', 'Storage', 'Sensors'];

// Globals do ES5 padrao + o que o firmware/harness injeta. Uso fora daqui sem
// declaracao vira diagnostico de variavel/funcao nao declarada.
const ES5_GLOBALS = [
  'Array', 'Boolean', 'Date', 'decodeURI', 'decodeURIComponent', 'encodeURI',
  'encodeURIComponent', 'Error', 'eval', 'EvalError', 'Function', 'Infinity',
  'isFinite', 'isNaN', 'JSON', 'Math', 'NaN', 'Number', 'Object', 'parseFloat',
  'parseInt', 'RangeError', 'ReferenceError', 'RegExp', 'String', 'SyntaxError',
  'TypeError', 'URIError', 'undefined', 'arguments',
  '__harness',  // injetado so no harness de teste; apps usam typeof p/ detectar
  // Timers do firmware (API 12): globais como no navegador
  'setTimeout', 'setInterval', 'clearTimeout', 'clearInterval',
];

// Compilado fora no Duktape lean (celeros_duk_config.yaml) ou inexistente no
// aparelho (Node-isms). Uso e erro.
const BANNED_GLOBALS = {
  Promise: 'Promise nao existe no Duktape do CelerOS (ES5)',
  Proxy: 'Proxy compilado fora do Duktape lean',
  Reflect: 'Reflect compilado fora do Duktape lean',
  Symbol: 'Symbol compilado fora do Duktape lean',
  ArrayBuffer: 'typed arrays compilados fora do Duktape lean (DUK_USE_BUFFEROBJECT_SUPPORT=false)',
  DataView: 'typed arrays compilados fora do Duktape lean (DUK_USE_BUFFEROBJECT_SUPPORT=false)',
  Uint8Array: 'typed arrays compilados fora do Duktape lean (DUK_USE_BUFFEROBJECT_SUPPORT=false)',
  Uint16Array: 'typed arrays compilados fora do Duktape lean (DUK_USE_BUFFEROBJECT_SUPPORT=false)',
  Uint32Array: 'typed arrays compilados fora do Duktape lean (DUK_USE_BUFFEROBJECT_SUPPORT=false)',
  Int8Array: 'typed arrays compilados fora do Duktape lean (DUK_USE_BUFFEROBJECT_SUPPORT=false)',
  Int16Array: 'typed arrays compilados fora do Duktape lean (DUK_USE_BUFFEROBJECT_SUPPORT=false)',
  Int32Array: 'typed arrays compilados fora do Duktape lean (DUK_USE_BUFFEROBJECT_SUPPORT=false)',
  Float32Array: 'typed arrays compilados fora do Duktape lean (DUK_USE_BUFFEROBJECT_SUPPORT=false)',
  Float64Array: 'typed arrays compilados fora do Duktape lean (DUK_USE_BUFFEROBJECT_SUPPORT=false)',
  TextEncoder: 'encoding builtins compilados fora do Duktape lean',
  TextDecoder: 'encoding builtins compilados fora do Duktape lean',
  performance: 'performance builtin compilado fora do Duktape lean',
  Map: 'Map e ES6: nao existe no Duktape do CelerOS',
  Set: 'Set e ES6: nao existe no Duktape do CelerOS',
  WeakMap: 'WeakMap e ES6: nao existe no Duktape do CelerOS',
  require: 'require nao existe no aparelho: apps sao single-file',
  module: 'module nao existe no aparelho',
  exports: 'exports nao existe no aparelho',
  process: 'process e Node.js: nao existe no aparelho',
  console: 'console nao existe no aparelho: use System.print()',
  Buffer: 'Buffer e Node.js: nao existe no aparelho',
  global: 'global e Node.js: nao existe no aparelho',
  globalThis: 'globalThis e ES2020: nao existe no aparelho',
};

// ============================================================
// Secao 1 — manifest da API, derivado do fonte do firmware
// ============================================================

// Corpo de funcao C++ a partir do indice do '{' de abertura, pulando strings
// e comentarios (suficiente para os corpos simples dos bindings).
function extractFromBody(text, braceIdx) {
  let i = braceIdx + 1;  // comeca DEPOIS do '{' de abertura (ja contado no depth)
  const start = i;
  let depth = 1;
  while (i < text.length && depth > 0) {
    const c = text[i];
    if (c === '/' && text[i + 1] === '/') { while (i < text.length && text[i] !== '\n') i++; continue; }
    if (c === '/' && text[i + 1] === '*') { i += 2; while (i < text.length && !(text[i] === '*' && text[i + 1] === '/')) i++; i += 2; continue; }
    if (c === '"' || c === "'") { const q = c; i++; while (i < text.length && text[i] !== q) { if (text[i] === '\\') i++; i++; } i++; continue; }
    if (c === '{') depth++;
    else if (c === '}') depth--;
    i++;
  }
  return depth === 0 ? text.slice(start, i - 1) : null;
}

function extractCppBody(text, headerRe) {
  const m = headerRe.exec(text);
  if (!m) return null;
  return extractFromBody(text, m.index + m[0].length - 1);
}

// State machine sobre as linhas do init(): segue a pilha Duktape
// (push_object / put_prop_string) para atribuir cada tabela kFnsN ao seu
// namespace, coletando gates de permissao e Kconfig pelo caminho.
function parseBindingsCpp(text) {
  const body = extractCppBody(text, /void\s+JSBindings::init\s*\([^)]*\)\s*\{/);
  if (body === null) throw new Error('JSBindings::init() nao encontrado em ' + BINDINGS_CPP);

  const warnings = [];
  const objects = {};       // path -> {fns:[], consts:[], perm, kcfg}
  const globalObj = { fns: [], consts: [], perm: null, kcfg: [], props: {} };
  const stack = [globalObj];
  const kcfg = [];          // regioes #if ativas (nome CONFIG_CELEROS_* ou null)
  let pendingPerm = null;   // perm(...) pendente da linha anterior
  let curTable = null;      // tabela sendo lida agora
  let doneTable = null;     // tabela fechada, aguardando o putFns

  const activeKcfg = () => kcfg.filter(Boolean);
  const mergePerm = (a, b) => (a && b && a !== b ? a + '+' + b : (a || b || null));

  for (const raw of body.split('\n')) {
    const line = raw.replace(/\/\/.*$/, '').trim();
    if (!line) continue;
    let m;

    if ((m = /^#(?:if|ifdef|ifndef)\s+(\S+)/.exec(line))) {
      kcfg.push(/^CONFIG_CELEROS_\w+$/.test(m[1]) ? m[1] : null);
      continue;
    }
    if (/^#endif/.test(line)) { kcfg.pop(); continue; }
    if (/^#else/.test(line)) { warnings.push('#else dentro de init() nao suportado pelo parser do manifest'); continue; }
    if (line[0] === '#') continue;

    const pm = /perm\(celer::(PERM_\w+)\)/.exec(line);
    const linePerm = pm ? pm[1].replace(/^PERM_/, '').toLowerCase() : null;

    if (/duk_push_object\s*\(/.test(line)) {
      stack.push({ fns: [], consts: [], perm: pendingPerm || linePerm, kcfg: activeKcfg(), props: {} });
      pendingPerm = null;
      continue;
    }
    if ((m = /static\s+const\s+JsFn\s+(\w+)\s*\[\]\s*=\s*\{/.exec(line))) {
      curTable = { name: m[1], entries: [] };
      continue;
    }
    if (curTable) {
      if ((m = /\{\s*"([^"]+)"\s*,\s*(\w+)\s*,\s*(\d+)\s*\}/.exec(line))) {
        curTable.entries.push({ name: m[1], cfn: m[2], nargs: +m[3] });
        continue;
      }
      if (/\}\s*;?\s*$/.test(line)) { doneTable = curTable; curTable = null; }
      continue; // linha de continuacao da tabela
    }
    if ((m = /putFns\s*\(\s*ctx\s*,\s*(\w+)\s*\)/.exec(line))) {
      if (!doneTable || doneTable.name !== m[1]) {
        warnings.push('putFns de tabela nao reconhecida: ' + m[1]);
        continue;
      }
      const target = stack[stack.length - 1];
      if (!target) {
        warnings.push('putFns sem objeto alvo na pilha: ' + m[1]);
        continue;
      }
      for (const e of doneTable.entries) {
        target.fns.push({
          name: e.name, cfn: e.cfn, nargs: e.nargs, min: e.nargs,
          perm: mergePerm(target.perm, linePerm),
          kcfg: Array.from(new Set(target.kcfg.concat(activeKcfg()))),
        });
      }
      doneTable = null;
      continue;
    }
    if ((m = /duk_put_prop_string\s*\(\s*ctx\s*,\s*-?\d+\s*,\s*"([^"]+)"\s*\)/.exec(line))) {
      if (/duk_push_(?:int|uint|number|boolean)\s*\(/.test(line)) {
        const vm = /duk_push_(?:int|uint)\s*\(\s*ctx\s*,\s*([^,)]+)/.exec(line);
        stack[stack.length - 1].consts.push({
          name: m[1], value: vm ? vm[1].trim() : '?',
          perm: stack[stack.length - 1].perm, kcfg: activeKcfg(),
        });
        continue;
      }
      // Fecha o objeto do topo: ganha nome e vira propriedade do novo topo.
      // O path completo so e conhecido no fim (o pai ainda nao foi nomeado).
      const obj = stack.pop();
      if (!obj || stack.length === 0) { warnings.push('duk_put_prop_string sem objeto na pilha: ' + m[1]); continue; }
      const parent = stack[stack.length - 1];
      obj.perm = mergePerm(obj.perm, linePerm);
      parent.props[m[1]] = obj;
      continue;
    }
    // `if (perm(...))` puro ou `{`/`}` de bloco: guarda o perm para a proxima op duk
    if (linePerm && !/duk_|putFns/.test(line)) pendingPerm = linePerm;
  }

  // Achata a arvore de objetos em paths completos (System, System.gpio, ...)
  const flatten = (obj, prefix) => {
    for (const [name, child] of Object.entries(obj.props)) {
      const p = prefix ? prefix + '.' + name : name;
      objects[p] = { fns: child.fns, consts: child.consts, perm: child.perm, kcfg: child.kcfg };
      flatten(child, p);
    }
  };
  flatten(globalObj, '');

  if (globalObj.fns.length) {
    objects.global = { fns: globalObj.fns, consts: globalObj.consts,
                       perm: globalObj.perm, kcfg: globalObj.kcfg };
  }
  return { objects, globalConsts: globalObj.consts, warnings };
}

// Mapa cfn -> corpo, varrendo os modulos do Runtime uma vez (Js*.cpp e o
// proprio JSBindings.cpp, onde vivem topbar/theme/drawIcon).
function scanRuntimeBodies() {
  const bodies = new Map();
  const files = fs.readdirSync(RUNTIME_DIR).filter((f) => /^Js.*\.cpp$/.test(f) || f === 'JSBindings.cpp').sort();
  for (const f of files) {
    const text = fs.readFileSync(path.join(RUNTIME_DIR, f), 'utf8');
    const re = /(?:static\s+)?duk_ret_t\s+(?:JSBindings::)?(js_\w+)\s*\([^)]*\)\s*\{/g;
    let m;
    while ((m = re.exec(text)) !== null) {
      if (bodies.has(m[1])) continue;
      const body = extractFromBody(text, m.index + m[0].length - 1);
      if (body) bodies.set(m[1], body);
    }
  }
  return bodies;
}

// Aridade minima: prefixo contiguo de indices exigidos com duk_require_* que
// NAO estejam protegidos por guarda (duk_is_*(ctx, N) ou duk_get_*_default
// tornam o indice N opcional — padrao "if (duk_is_string(ctx, 1)) ... require").
// Sem require algum, a funcao tolera chamada sem argumentos (min 0).
function minArityFromBody(body) {
  const required = new Set();
  const guarded = new Set();
  let m;
  const collect = (re, set) => {
    while ((m = re.exec(body)) !== null) set.add(+m[1]);
  };
  // duk_require_* e os helpers do Runtime no mesmo molde (requirePin do
  // JsGpio, requireKey do JsStorage): todos throw sem o argumento.
  collect(/(?:duk_)?require\w*\s*\(\s*ctx\s*,\s*(\d+)/g, required);
  collect(/duk_is_\w+\s*\(\s*ctx\s*,\s*(\d+)/g, guarded);
  collect(/duk_get_\w+_default\s*\(\s*ctx\s*,\s*(\d+)/g, guarded);
  let min = 0;
  while (required.has(min) && !guarded.has(min)) min++;
  return min;
}

// Headings do guia: `#### System.foo(a, b)` (API N) -> nivel por funcao.
function parseGuideLevels(filePath) {
  const out = { levels: new Map(), warnings: [] };
  try {
    if (!fs.existsSync(filePath)) { out.warnings.push('guia ausente: ' + filePath); return out; }
    const text = fs.readFileSync(filePath, 'utf8');
    const lvl = /^#{3,4}\s+.*\(API\s+(\d+)\)/;
    const nameRe = /([A-Za-z_$][\w$]*(?:\.[A-Za-z_$][\w$]*)*)\s*\(/g;
    for (const line of text.split('\n')) {
      if (!/^#{3,4}\s/.test(line)) continue;
      const lm = lvl.exec(line);
      let nm;
      nameRe.lastIndex = 0;
      while ((nm = nameRe.exec(line)) !== null) {
        if (!nm[1].includes('.')) continue;
        if (!out.levels.has(nm[1])) out.levels.set(nm[1], lm ? +lm[1] : null);
      }
    }
  } catch (e) { out.warnings.push('falha ao ler guia: ' + e.message); }
  return out;
}

function buildManifest() {
  const bindingsText = fs.readFileSync(BINDINGS_CPP, 'utf8');
  const parsed = parseBindingsCpp(bindingsText);

  const cm = /CELEROS_API_LEVEL=(\d+)/.exec(fs.readFileSync(MAIN_CMAKE, 'utf8'));
  if (!cm) throw new Error('CELEROS_API_LEVEL nao encontrado em main/CMakeLists.txt');
  const apiLevel = +cm[1];

  // Aridade minima inferida dos corpos C++
  const bodies = scanRuntimeBodies();
  let arityInferred = 0;
  let bodiesFound = 0;
  for (const obj of Object.values(parsed.objects)) {
    for (const fn of obj.fns) {
      const body = bodies.get(fn.cfn);
      if (body) {
        bodiesFound++;
        const min = Math.min(minArityFromBody(body), fn.nargs);
        if (min !== fn.min) arityInferred++;
        fn.min = min;
        // `return N` (N>0) no corpo = a funcao JS devolve valor (usado pelo
        // gerador de types do SDK para distinguir void de retorno)
        fn.returns = /\breturn\s+[1-9]/.test(body);
        // Permissao checada DENTRO do binding (nao na tabela de registro):
        // o corpo anota com "// lint-perm: fs" (ex.: Net.download grava no FS)
        const lp = /lint-perm:\s*(\w+)/.exec(body);
        if (lp) {
          const base = fn.perm || obj.perm;
          fn.perm = base ? base + '+' + lp[1] : lp[1];
        }
      }
    }
  }

  // Nivel de API por funcao (best-effort, do guia pt-BR)
  const guide = parseGuideLevels(GUIDE_PT);
  let withLevel = 0;
  for (const [objPath, obj] of Object.entries(parsed.objects)) {
    for (const fn of obj.fns) {
      const q = objPath + '.' + fn.name;
      const lv = guide.levels.get(q);
      if (lv) { fn.apiLevel = lv; withLevel++; }
    }
  }

  const warnings = parsed.warnings.concat(guide.warnings);
  if (Object.keys(parsed.objects).length === 0) warnings.push('nenhum namespace parseado — parser do manifest falhou?');

  return {
    apiLevel,
    objects: parsed.objects,
    globalConsts: parsed.globalConsts,
    warnings,
    counts: {
      functions: Object.values(parsed.objects).reduce((n, o) => n + o.fns.length, 0),
      withApiLevel: withLevel,
      arityInferred,
      bodiesFound,
      consts: parsed.globalConsts.length + Object.values(parsed.objects).reduce((n, o) => n + o.consts.length, 0),
    },
  };
}

// ============================================================
// Secao 2 — utilidades de AST
// ============================================================

function eachChild(node, fn) {
  for (const key in node) {
    if (key === 'loc' || key === 'start' || key === 'end' || key === 'range' || key === 'raw' || key === '__scope') continue;
    const v = node[key];
    if (Array.isArray(v)) {
      for (const c of v) if (c && typeof c.type === 'string') fn(c, key, node);
    } else if (v && typeof v.type === 'string') {
      fn(v, key, node);
    }
  }
}

function newScope(parent) { return { parent, vars: new Set() }; }

// Fase A: hoisting ES5 — var e function declaration sobem para o escopo da
// funcao; blocos nao criam escopo; catch cria escopo proprio para o param.
function hoist(node, scope) {
  if (node.type === 'VariableDeclaration') {
    for (const d of node.declarations) if (d.id.type === 'Identifier') scope.vars.add(d.id.name);
    // sem return: o inicializador pode conter funcoes (var cmds = { ls: function(){...} })
  }
  if (node.type === 'CatchClause') {
    const child = newScope(scope);
    if (node.param && node.param.type === 'Identifier') child.vars.add(node.param.name);
    node.__scope = child;
    hoist(node.body, child);
    return;
  }
  eachChild(node, (c) => {
    if (c.type === 'FunctionDeclaration' || c.type === 'FunctionExpression') {
      const child = newScope(scope);
      if (c.type === 'FunctionDeclaration' && c.id) scope.vars.add(c.id.name);
      if (c.type === 'FunctionExpression' && c.id) child.vars.add(c.id.name);
      for (const p of c.params) if (p.type === 'Identifier') child.vars.add(p.name);
      c.__scope = child;
      hoist(c.body, child);
    } else {
      hoist(c, scope);
    }
  });
}

function scopeHas(scope, name) {
  for (let s = scope; s; s = s.parent) if (s.vars.has(name)) return true;
  return false;
}

// Cadeia de membro nao-computada: System.gpio.servo -> {root:'System', parts:[...]}
function memberChain(me) {
  const parts = [];
  let n = me;
  while (n.type === 'MemberExpression' && !n.computed) {
    if (n.property.type !== 'Identifier') return null;
    parts.unshift(n.property.name);
    n = n.object;
  }
  if (n.type !== 'Identifier') return null;
  return { root: n.name, rootNode: n, parts };
}

function levenshtein(a, b) {
  const dp = Array.from({ length: a.length + 1 }, (_, i) => [i].concat(Array(b.length).fill(0)));
  for (let j = 0; j <= b.length; j++) dp[0][j] = j;
  for (let i = 1; i <= a.length; i++)
    for (let j = 1; j <= b.length; j++)
      dp[i][j] = Math.min(dp[i - 1][j] + 1, dp[i][j - 1] + 1, dp[i - 1][j - 1] + (a[i - 1] === b[j - 1] ? 0 : 1));
  return dp[a.length][b.length];
}

function suggest(name, candidates) {
  let best = null, bestD = Infinity;
  for (const c of candidates) {
    const d = levenshtein(name, c);
    if (d < bestD) { bestD = d; best = c; }
  }
  if (best !== null && (bestD <= 2 || best.startsWith(name) || name.startsWith(best))) return best;
  return null;
}

// ============================================================
// Secao 3 — checks de um main.js
// ============================================================

function loc(node) {
  if (!node || !node.loc) return { line: 0, column: 0 };
  return { line: node.loc.start.line, column: node.loc.start.column + 1 };
}

function lintSource(manifest, src, appInfo) {
  const diags = [];
  const d = (node, severity, rule, message) => {
    const l = loc(node);
    diags.push({ line: l.line, col: l.column, severity, rule, message });
  };

  let ast;
  try {
    ast = acorn.parse(src.replace(/^\uFEFF/, ''), { ecmaVersion: 5, locations: true });
  } catch (e) {
    const l = e.loc || { line: 1, column: 0 };
    diags.push({
      line: l.line, col: l.column + 1, severity: 'erro', rule: 'sintaxe',
      message: 'sintaxe invalida (ES5): ' + e.message.replace(/\s*\(\d+:\d+\)\s*$/, ''),
    });
    return diags;
  }

  const rootScope = newScope(null);
  hoist(ast, rootScope);

  // Estado coletado durante o walk
  const implicitGlobals = new Set();  // atribuidos sem var
  const typeofTargets = new Set();    // alvos de typeof (feature-detect)
  const permUses = new Map();         // perm -> {node, what}
  const apiFnUses = [];               // {qualified, entry, node}
  let celerLinkUse = null;            // node do primeiro uso de CelerLink
  const reported = new Set();         // chaves de diagnostico unicas

  const globalWhitelist = new Set(ES5_GLOBALS.concat(Object.keys(BANNED_GLOBALS)));
  for (const c of manifest.globalConsts) globalWhitelist.add(c.name);
  for (const n of NAMESPACE_ROOTS) globalWhitelist.add(n);
  // fns registradas direto no global (setTimeout/setInterval/... no init)
  for (const f of (manifest.objects.global && manifest.objects.global.fns) || [])
    globalWhitelist.add(f.name);

  // Valida a cadeia contra o manifest. `report` emite o erro de existencia;
  // `record` coleta uso de permissao/nivel (uma vez por cadeia mais externa).
  function checkMemberExistence(chain, node, propNode, report, record) {
    let objPath = chain.root;
    if (!manifest.objects[objPath]) return null;
    for (let i = 0; i < chain.parts.length; i++) {
      const obj = manifest.objects[objPath];
      const name = chain.parts[i];
      const childPath = objPath + '.' + name;
      if (manifest.objects[childPath]) { objPath = childPath; continue; }
      const entry = obj.fns.find((f) => f.name === name) || obj.consts.find((c) => c.name === name) || null;
      const last = i === chain.parts.length - 1;
      if (!entry) {
        if (report) {
          const candidates = obj.fns.map((f) => f.name).concat(obj.consts.map((c) => c.name));
          const s = suggest(name, candidates);
          d(propNode || node, 'erro', 'api', objPath + '.' + name + ' nao existe na API' + (s ? ' (voce quis dizer "' + s + '"?)' : ''));
        }
        return null;
      }
      if (!last) return null; // uso do resultado como objeto (System.theme().bg): para por aqui
      const full = objPath + '.' + name;
      if (record) {
        const perm = entry.perm || obj.perm;
        if (perm) permUses.set(perm, { node: propNode || node, what: full });
        if (entry.nargs !== undefined) apiFnUses.push({ qualified: full, entry, node: propNode || node });
      }
      return entry.nargs !== undefined ? { entry, qualified: full } : { const: entry };
    }
    // Cadeia inteira de objetos (ex.: System.gpio sozinho)
    if (record) {
      const obj = manifest.objects[objPath];
      if (obj && obj.perm) permUses.set(obj.perm, { node: propNode || node, what: objPath });
    }
    return { objectPath: objPath };
  }

  // O loop "cede" ao sistema se: chama System.delay/System.prompt (prompt e
  // bloqueante em C, com loop proprio) ou delega a uma funcao local declarada
  // (o delay tipicamente vive dentro dela — analise transitiva e caro demais).
  function loopYields(node) {
    let found = false;
    const visit = (n) => {
      if (found || !n || typeof n.type !== 'string') return;
      if (n.type === 'FunctionDeclaration' || n.type === 'FunctionExpression') return;
      if (n.type === 'CallExpression') {
        const c = n.callee;
        if (c.type === 'MemberExpression' && !c.computed) {
          const ch = memberChain(c);
          if (ch && ch.root === 'System' && ch.parts.length === 1 &&
              (ch.parts[0] === 'delay' || ch.parts[0] === 'prompt')) { found = true; return; }
        } else if (c.type === 'Identifier' && (scopeHas(scopeAt, c.name) || implicitGlobals.has(c.name))) {
          found = true; return;
        }
      }
      eachChild(n, visit);
    };
    visit(node);
    return found;
  }

  let scopeAt = rootScope;

  function walk(node, parent, key) {
    const prevScope = scopeAt;
    if (node.__scope) scopeAt = node.__scope;

    switch (node.type) {
      case 'Identifier': {
        const name = node.name;
        if (parent && parent.type === 'UnaryExpression' && parent.operator === 'typeof') {
          typeofTargets.add(name);
          break;
        }
        if (BANNED_GLOBALS[name]) {
          d(node, 'erro', 'global', BANNED_GLOBALS[name]);
          break;
        }
        const shadowed = scopeHas(scopeAt, name) || implicitGlobals.has(name);
        if (shadowed || globalWhitelist.has(name)) {
          if (name === 'CelerLink' && !celerLinkUse && !shadowed) celerLinkUse = node;
          break;
        }
        if (key === 'callee' || (parent && parent.type === 'NewExpression' && parent.callee === node)) {
          d(node, 'erro', 'funcao', "funcao '" + name + "' nao esta declarada neste app");
        } else if (!reported.has('undecl:' + name)) {
          reported.add('undecl:' + name);
          d(node, 'aviso', 'var', "variavel '" + name + "' nao esta declarada (typo? faltou var?)");
        }
        break;
      }
      case 'MemberExpression': {
        // So o passo mais externo da cadeia verifica existencia
        if (parent && parent.type === 'MemberExpression' && parent.object === node) break;
        if (node.computed) break;
        const chain = memberChain(node);
        if (!chain || NAMESPACE_ROOTS.indexOf(chain.root) < 0) break;
        if (scopeHas(scopeAt, chain.root) || implicitGlobals.has(chain.root)) break; // shadow local
        if (chain.root === 'CelerLink' && !celerLinkUse) celerLinkUse = node;
        checkMemberExistence(chain, node, node.property, true, true);
        break;
      }
      case 'AssignmentExpression':
      case 'UpdateExpression': {
        const t = node.type === 'AssignmentExpression' ? node.left : node.argument;
        if (t.type === 'Identifier' && !scopeHas(scopeAt, t.name) && !implicitGlobals.has(t.name) &&
            !globalWhitelist.has(t.name)) {
          implicitGlobals.add(t.name);
          if (!reported.has('igg:' + t.name)) {
            reported.add('igg:' + t.name);
            d(t, 'aviso', 'var', "atribuicao sem var: '" + t.name + "' vira global (use var)");
          }
        }
        break;
      }
      case 'CallExpression':
      case 'NewExpression': {
        if (node.callee.type === 'MemberExpression' && !node.callee.computed &&
            !node.arguments.some((a) => a.type === 'SpreadElement')) {
          const chain = memberChain(node.callee);
          if (chain && NAMESPACE_ROOTS.indexOf(chain.root) >= 0 &&
              !scopeHas(scopeAt, chain.root) && !implicitGlobals.has(chain.root)) {
            // report=false: a existencia e reportada no passo MemberExpression
            const r = checkMemberExistence(chain, node.callee, node.callee.property, false, false);
            if (r && r.entry) {
              const n = node.arguments.length;
              if (n > r.entry.nargs) {
                d(node, 'erro', 'aridade', 'chamada com ' + n + ' argumentos: ' + r.qualified +
                  ' aceita no maximo ' + r.entry.nargs + ' (extras sao descartados no aparelho)');
              } else if (n < r.entry.min) {
                d(node, 'aviso', 'aridade', 'faltam argumentos: ' + r.qualified + ' exige pelo menos ' +
                  r.entry.min + ' (TypeError no aparelho)');
              }
            }
          }
        }
        break;
      }
      case 'Literal': {
        if (typeof node.value === 'string') {
          for (const ch of node.value) {
            const cp = ch.codePointAt(0);
            const ok = (cp >= 0x20 && cp <= 0xFF) || cp === 0x09 || cp === 0x0A || cp === 0x0D;
            if (!ok) {
              d(node, 'aviso', 'charset', 'caractere U+' + cp.toString(16).toUpperCase().padStart(4, '0') +
                ' fora do Latin-1 na string: a fonte do aparelho nao cobre (sem em dash, aspas curvas, emoji)');
              break;
            }
          }
        }
        break;
      }
      case 'WhileStatement':
      case 'DoWhileStatement':
      case 'ForStatement': {
        const test = node.test;
        const infinite =
          node.type === 'ForStatement' ? test === null :
            !!(test && (
              (test.type === 'Literal' && (test.value === true || (typeof test.value === 'number' && test.value !== 0))) ||
              (test.type === 'UnaryExpression' && test.operator === '!' && test.argument.type === 'Literal' && test.argument.value === 0)
            ));
        if (infinite && !loopYields(node.body)) {
          d(node, 'aviso', 'loop', 'loop infinito sem System.delay() no corpo: o GC do Duktape roda dentro de delay; sem ele o app trava o aparelho');
        }
        break;
      }
    }

    eachChild(node, (c, k) => {
      if (node.type === 'Property' && k === 'key') return;
      if (node.type === 'MemberExpression' && k === 'property' && !node.computed) return;
      if ((node.type === 'FunctionDeclaration' || node.type === 'FunctionExpression') && (k === 'id' || k === 'params')) return;
      if (node.type === 'VariableDeclarator' && k === 'id') return;
      if (node.type === 'CatchClause' && k === 'param') return;
      if (k === 'label') return;
      walk(c, node, k);
    });
    scopeAt = prevScope;
  }

  walk(ast, null, null);

  // ---- Checks pos-walk que dependem do app.json ----
  if (appInfo) {
    const perms = Array.isArray(appInfo.permissions) ? appInfo.permissions : null;
    if (perms) {
      for (const [perm, u] of permUses) {
        for (const p of perm.split('+')) {
          if (perms.indexOf(p) < 0 && !reported.has('perm:' + p)) {
            reported.add('perm:' + p);
            d(u.node, 'aviso', 'perm', u.what + ' exige permissao "' + p + '" no app.json (sem ela a chamada e undefined -> TypeError no aparelho)');
          }
        }
      }
    }
    const appApi = typeof appInfo.api === 'number' ? appInfo.api : 1;
    for (const u of apiFnUses) {
      if (u.entry.apiLevel && u.entry.apiLevel > appApi && !reported.has('nivel:' + u.qualified)) {
        reported.add('nivel:' + u.qualified);
        d(u.node, 'aviso', 'nivel', u.qualified + ' exige API ' + u.entry.apiLevel + '; o app.json declara api ' + appApi);
      }
    }
  }

  if (celerLinkUse && !typeofTargets.has('CelerLink')) {
    d(celerLinkUse, 'aviso', 'feature', 'CelerLink e opcional (so placas com Bluetooth): proteja com typeof CelerLink !== "undefined" antes de usar');
  }

  diags.sort((a, b) => (a.line - b.line) || (a.col - b.col));
  return diags;
}

// ============================================================
// Secao 4 — checks de app.json e orcamento de app
// ============================================================

const REQUIRED_FIELDS = ['name', 'packageName', 'version', 'author', 'description'];
const RESERVED_FIELDS = ['size', 'md5', 'published_at', 'publisher'];
const VALID_PERMS = ['fs', 'net', 'gpio', 'system'];

function lintAppJson(dir, manifest) {
  const diags = [];
  const p = path.join(dir, 'app.json');
  let app = null;
  let text;
  try {
    text = fs.readFileSync(p, 'utf8');
  } catch (e) {
    diags.push({ file: 'app.json', line: 0, col: 0, severity: 'erro', rule: 'appjson', message: 'app.json inacessivel: ' + e.message });
    return { app, diags, entry: 'main.js' };
  }
  try {
    app = JSON.parse(text);
  } catch (e) {
    diags.push({ file: 'app.json', line: 0, col: 0, severity: 'erro', rule: 'appjson', message: 'app.json nao e JSON valido: ' + e.message });
    return { app: null, diags, entry: 'main.js' };
  }

  const d = (severity, rule, message) => diags.push({ file: 'app.json', line: 0, col: 0, severity, rule, message });

  for (const f of REQUIRED_FIELDS) {
    if (!app[f] || typeof app[f] !== 'string' || !app[f].trim()) d('erro', 'appjson', 'campo obrigatorio ausente ou vazio: ' + f);
  }
  if (app.version && !/^\d+\.\d+\.\d+$/.test(String(app.version))) d('erro', 'appjson', 'version deve ser semver x.y.z: "' + app.version + '"');
  if (app.packageName && !/^[a-z0-9]+(\.[a-z0-9]+)+$/.test(app.packageName)) d('erro', 'appjson', 'packageName invalido: ' + app.packageName);
  for (const f of RESERVED_FIELDS) {
    if (app[f] !== undefined) d('erro', 'appjson', 'campo "' + f + '" e computado pelo hub: nunca setar a mao');
  }
  if (app.api !== undefined) {
    if (typeof app.api !== 'number' || !Number.isInteger(app.api) || app.api < 1) {
      d('erro', 'appjson', 'api deve ser inteiro >= 1');
    } else if (app.api > manifest.apiLevel) {
      d('erro', 'appjson', 'api ' + app.api + ' maior que o CELEROS_API_LEVEL do firmware (' + manifest.apiLevel + ')');
    }
  }
  if (app.permissions !== undefined) {
    if (!Array.isArray(app.permissions)) d('erro', 'appjson', 'permissions deve ser array');
    else for (const pm of app.permissions) {
      if (VALID_PERMS.indexOf(pm) < 0) d('erro', 'appjson', 'permissao desconhecida: "' + pm + '" (validas: ' + VALID_PERMS.join(', ') + ')');
    }
  }

  const entry = typeof app.main === 'string' && app.main ? app.main : 'main.js';
  try {
    const st = fs.statSync(path.join(dir, entry));
    const kb = st.size / 1024;
    if (kb > MAX_MAIN_JS / 1024) d('erro', 'appjson', entry + ' tem ' + kb.toFixed(1) + 'KB: o hub recusa acima de ' + (MAX_MAIN_JS / 1024) + 'KB');
    else if (kb > STREAM_SAFE_MAIN_JS / 1024 && (typeof app.api !== 'number' || app.api < 6)) d('aviso', 'appjson', entry + ' tem ' + kb.toFixed(1) + 'KB: acima de ' + (STREAM_SAFE_MAIN_JS / 1024) + 'KB o hub exige api >= 6');
  } catch (e) {
    d('erro', 'appjson', 'arquivo de entrada ausente: ' + entry);
  }

  try {
    const st = fs.statSync(path.join(dir, 'icon.png'));
    if (st.size > 10 * 1024) d('aviso', 'appjson', 'icon.png tem ' + (st.size / 1024).toFixed(1) + 'KB (recomendado <= 10KB)');
  } catch (e) { /* icone e opcional */ }

  return { app, diags, entry };
}

// ============================================================
// Secao 5 — alvos, execucao e CLI
// ============================================================

function collectTargets(args) {
  const targets = [];
  const paths = args.length ? args : ['data/apps', 'hub_apps'];
  for (const p of paths) {
    const abs = path.resolve(ROOT, p);
    let st;
    try { st = fs.statSync(abs); } catch (e) {
      // caminho inexistente nao e erro do APP: o CI lista boards/*/data/apps
      // que podem ainda nao estar commitados (rodada em voo). Aviso e segue.
      console.error('  aviso: caminho inexistente (ignorado): ' + p);
      continue;
    }
    if (st.isFile()) {
      targets.push({ dir: path.dirname(abs), entryRel: path.relative(ROOT, abs), singleFile: true });
      continue;
    }
    if (fs.existsSync(path.join(abs, 'app.json'))) {
      targets.push({ dir: abs });
      continue;
    }
    for (const name of fs.readdirSync(abs).sort()) {
      const sub = path.join(abs, name);
      let sst;
      try { sst = fs.statSync(sub); } catch (e) { continue; }
      if (sst.isDirectory()) {
        if (fs.existsSync(path.join(sub, 'app.json'))) targets.push({ dir: sub });
      } else if (name.endsWith('.js')) {
        targets.push({ dir: abs, entryRel: path.relative(ROOT, sub), singleFile: true });
      }
    }
  }
  return targets;
}

function lintApp(manifest, target) {
  const diagnostics = [];

  if (target.error) {
    diagnostics.push({ file: '.', line: 0, col: 0, severity: 'erro', rule: 'cli', message: target.error });
    return { relDir: '?', diagnostics };
  }

  let appInfo = null;
  let entryAbs;
  let entryRel;

  if (target.singleFile) {
    entryRel = target.entryRel;
    entryAbs = path.join(ROOT, entryRel);
  } else {
    const r = lintAppJson(target.dir, manifest);
    appInfo = r.app;
    for (const dg of r.diags) diagnostics.push(dg);
    entryRel = path.join(target.relDir || path.relative(ROOT, target.dir), r.entry);
    entryAbs = path.join(target.dir, r.entry);
    if (!target.relDir) target.relDir = path.relative(ROOT, target.dir);
  }

  let src = null;
  try {
    src = fs.readFileSync(entryAbs, 'utf8');
  } catch (e) {
    diagnostics.push({ file: entryRel, line: 0, col: 0, severity: 'erro', rule: 'appjson', message: 'nao consegui ler ' + entryRel + ': ' + e.message });
  }
  if (src !== null) {
    for (const dg of lintSource(manifest, src, appInfo)) {
      diagnostics.push({ file: entryRel, line: dg.line, col: dg.col, severity: dg.severity, rule: dg.rule, message: dg.message });
    }
  }

  diagnostics.sort((a, b) => (a.line - b.line) || (a.col - b.col));
  return { relDir: target.singleFile ? entryRel : target.relDir, diagnostics };
}

function runLint(args, opts) {
  const manifest = buildManifest();
  const targets = collectTargets(args);
  const apps = targets.map((t) => lintApp(manifest, t));
  const totals = { apps: apps.length, errors: 0, warnings: 0 };
  for (const a of apps) {
    for (const dg of a.diagnostics) {
      if (dg.severity === 'erro') totals.errors++;
      else totals.warnings++;
    }
  }
  return { manifest, apps, totals, ok: totals.errors === 0 && (!opts.strict || totals.warnings === 0) };
}

// ---- modo check: drift codigo x docs x stubs do harness ----
function runCheck() {
  const manifest = buildManifest();
  const lines = [];
  let errors = 0;

  const codeFns = new Set();
  for (const [objPath, obj] of Object.entries(manifest.objects)) {
    for (const f of obj.fns) codeFns.add(objPath + '.' + f.name);
  }

  const pt = parseGuideLevels(GUIDE_PT);
  const en = parseGuideLevels(GUIDE_EN);

  lines.push('== Codigo (JSBindings.cpp) x docs ==');
  for (const name of pt.levels.keys()) {
    if (!codeFns.has(name)) { lines.push('  ERRO docs(pt-BR): ' + name + ' documentada mas nao existe no codigo'); errors++; }
  }
  for (const name of en.levels.keys()) {
    if (!codeFns.has(name)) { lines.push('  ERRO docs(EN): ' + name + ' documentada mas nao existe no codigo'); errors++; }
  }
  const undocumented = Array.from(codeFns).filter((n) => !pt.levels.has(n)).sort();
  if (undocumented.length) lines.push('  aviso: ' + undocumented.length + ' funcoes sem doc pt-BR: ' + undocumented.join(', '));
  for (const n of pt.levels.keys()) if (!en.levels.has(n)) lines.push('  aviso: ' + n + ' docs pt-BR sem equivalente EN');
  for (const n of en.levels.keys()) if (!pt.levels.has(n)) lines.push('  aviso: ' + n + ' docs EN sem equivalente pt-BR');

  lines.push('== Nivel de API ==');
  lines.push('  firmware (main/CMakeLists.txt): ' + manifest.apiLevel);
  for (const [label, gpath] of [['pt-BR', GUIDE_PT], ['EN', GUIDE_EN]]) {
    try {
      const hm = /API Level:\s*(\d+)/.exec(fs.readFileSync(gpath, 'utf8'));
      if (hm && +hm[1] !== manifest.apiLevel) lines.push('  aviso: guia ' + label + ' declara API Level ' + hm[1] + ' (firmware: ' + manifest.apiLevel + ')');
    } catch (e) { lines.push('  aviso: guia ' + label + ' ilegivel'); }
  }

  lines.push('== Stubs do harness (test/js_harness/run.js) ==');
  try {
    const harness = require(path.join(ROOT, 'test', 'js_harness', 'run.js'));
    const env = harness.makeEnv();
    for (const ns of NAMESPACE_ROOTS) {
      const stub = ns === 'System' ? env.System : env[ns];
      if (!stub) { lines.push('  aviso: harness nao expoe ' + ns); continue; }
      for (const [objPath, obj] of Object.entries(manifest.objects)) {
        if (objPath !== ns && !objPath.startsWith(ns + '.')) continue;
        const holder = objPath === ns ? stub : stub[objPath.slice(ns.length + 1)];
        if (!holder) { lines.push('  aviso: harness sem ' + objPath + ' (apps que usam falham no teste)'); continue; }
        const missing = obj.fns.filter((f) => typeof holder[f.name] !== 'function').map((f) => objPath + '.' + f.name);
        if (missing.length) lines.push('  aviso: ' + missing.length + ' fns sem stub em ' + objPath + ': ' + missing.join(', '));
      }
    }
  } catch (e) {
    lines.push('  aviso: harness nao exportavel (makeEnv): ' + e.message);
  }

  if (manifest.warnings.length) {
    lines.push('== Avisos do parser do manifest ==');
    for (const w of manifest.warnings) lines.push('  ' + w);
  }

  lines.push('');
  lines.push(errors ? 'check: ' + errors + ' erro(s) de drift' : 'check: sem erros de drift (avisos acima)');
  return { text: lines.join('\n'), errors };
}

// ---- CLI ----
function main() {
  const argv = process.argv.slice(2);
  const opts = { json: false, strict: false };
  const args = [];
  for (const a of argv) {
    if (a === '--json') opts.json = true;
    else if (a === '--strict') opts.strict = true;
    else if (a === '--dump-manifest') opts.dumpManifest = true;
    else if (a === '-h' || a === '--help') opts.help = true;
    else args.push(a);
  }

  if (opts.help) {
    console.log([
      'Uso: node tools/app_lint/lint.js [caminhos...] [opcoes]',
      '',
      'Caminhos: pasta de app (com app.json), pasta com varios apps (ex.:',
      'data/apps, hub_apps) ou um .js avulso. Padrao: data/apps hub_apps.',
      '',
      'Opcoes:',
      '  --json           saida em JSON (para celerhub/celerctl)',
      '  --strict         avisos contam como erro (exit 1)',
      '  --dump-manifest  imprime o manifest da API derivado do firmware e sai',
      '  check            modo drift: codigo x docs x stubs do harness',
      '  -h, --help       esta ajuda',
      '',
      'O manifest nunca e manual: e derivado de main/Runtime/JSBindings.cpp,',
      'main/CMakeLists.txt, corpos C++ (aridade minima) e JS_API_Guide.pt-BR.md.',
    ].join('\n'));
    process.exit(0);
  }

  if (args[0] === 'check') {
    const r = runCheck();
    console.log(r.text);
    process.exit(r.errors ? 1 : 0);
  }

  if (opts.dumpManifest) {
    try {
      console.log(JSON.stringify(buildManifest(), null, 2));
      process.exit(0);
    } catch (e) {
      console.error('app_lint: ' + e.message);
      process.exit(2);
    }
  }

  let result;
  try {
    result = runLint(args, opts);
  } catch (e) {
    if (opts.json) console.log(JSON.stringify({ ok: false, fatal: e.message }));
    else console.error('app_lint: ' + e.message);
    process.exit(2);
  }

  if (opts.json) {
    console.log(JSON.stringify({
      ok: result.ok,
      totals: result.totals,
      apps: result.apps.map((a) => ({
        app: a.relDir,
        ok: !a.diagnostics.some((dg) => dg.severity === 'erro'),
        diagnostics: a.diagnostics,
      })),
    }));
    process.exit(result.ok ? 0 : 1);
  }

  for (const a of result.apps) {
    const errs = a.diagnostics.filter((dg) => dg.severity === 'erro').length;
    const warns = a.diagnostics.length - errs;
    const status = errs ? errs + ' erro(s)' : 'OK';
    console.log(a.relDir + ': ' + status + (warns ? ' (' + warns + ' aviso(s))' : ''));
    for (const dg of a.diagnostics) {
      console.log('  ' + dg.file + ':' + dg.line + ':' + dg.col + ' ' + dg.severity + '[' + dg.rule + ']: ' + dg.message);
    }
  }
  console.log('');
  console.log('apps: ' + result.totals.apps + '  erros: ' + result.totals.errors + '  avisos: ' + result.totals.warnings +
    '  (API level ' + result.manifest.apiLevel + ', ' + result.manifest.counts.functions + ' funcoes no manifest)');
  process.exit(result.ok ? 0 : 1);
}

module.exports = { buildManifest, lintSource, lintAppJson, collectTargets, lintApp, runLint, runCheck, parseBindingsCpp, minArityFromBody };

if (require.main === module) main();
