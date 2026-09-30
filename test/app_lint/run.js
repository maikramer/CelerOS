#!/usr/bin/env node
// Testes do app_lint (tools/app_lint/lint.js): cada fixture deve produzir
// exatamente o conjunto esperado de diagnosticos (severidade + regra).
// Estilo do test/cpp/run_tests.cpp: zero framework, exit 1 em falha.

'use strict';

const fs = require('fs');
const path = require('path');

const ROOT = path.resolve(__dirname, '..', '..');
const FIX = path.join(__dirname, 'fixtures');
const linter = require(path.join(ROOT, 'tools', 'app_lint', 'lint.js'));

let failures = 0;

function check(name, cond, detail) {
  console.log((cond ? '  PASS  ' : '  FAIL  ') + name + (cond ? '' : '  << ' + detail));
  if (!cond) failures++;
}

function ruleCounts(diags) {
  const counts = {};
  for (const dg of diags) {
    const k = dg.severity + ':' + dg.rule;
    counts[k] = (counts[k] || 0) + 1;
  }
  return counts;
}

function fmt(counts) { return JSON.stringify(counts); }

function lintFixture(file, appInfo) {
  return linter.lintSource(linter.buildManifest(), fs.readFileSync(path.join(FIX, file), 'utf8'), appInfo || null);
}

const CASES = [
  { name: 'ok.js limpo', file: 'ok.js', expect: {} },
  { name: 'es6 rejeitado (sintaxe)', file: 'es6.js', expect: { 'erro:sintaxe': 1 } },
  {
    name: 'funcoes/constantes inexistentes', file: 'api_inexistente.js',
    expect: { 'erro:api': 4, 'aviso:var': 2 },  // T e MAGNETA nao declarados no fixture
  },
  {
    name: 'aridades erradas', file: 'aridade.js',
    expect: { 'erro:aridade': 3, 'aviso:aridade': 1, 'erro:api': 1 },
  },
  {
    name: 'globals fora do Duktape lean / Node-isms', file: 'globals.js',
    expect: { 'erro:global': 6 },
  },
  {
    name: 'funcao/variavel nao declarada', file: 'sem_declaracao.js',
    expect: { 'erro:funcao': 1, 'aviso:var': 2 },
  },
  {
    name: 'loop sem ceder + charset', file: 'loop_charset.js',
    expect: { 'aviso:loop': 2, 'aviso:charset': 1 },
  },
  {
    name: 'API gated: permissao/nivel/feature',
    file: 'gated.js',
    appInfo: { api: 2, permissions: ['fs'] },
    expect: { 'aviso:perm': 2, 'aviso:nivel': 1, 'aviso:feature': 1 },
  },
];

console.log('== Testes do app_lint (fixtures) ==');
for (const c of CASES) {
  const got = ruleCounts(lintFixture(c.file, c.appInfo));
  const pass = Object.keys(c.expect).length === Object.keys(got).length &&
    Object.keys(c.expect).every((k) => c.expect[k] === got[k]);
  check(c.name, pass, 'esperado ' + fmt(c.expect) + ', obtido ' + fmt(got));
}

// Sugestao de nome parecido no erro de API
{
  const diags = lintFixture('api_inexistente.js');
  const d = diags.find((x) => x.message.indexOf('drawPixl') >= 0);
  check('sugestao de typo (drawPixl -> drawPixel)', !!d && d.message.indexOf('drawPixel') >= 0,
    d ? d.message : 'diagnostico ausente');
}

// app.json quebrado (campos, semver, api, size, permissao invalida)
{
  const r = linter.lintApp(linter.buildManifest(), { dir: path.join(FIX, 'badjson') });
  const got = ruleCounts(r.diagnostics);
  check('app.json invalido', got['erro:appjson'] === 7, 'esperado 7 erros appjson, obtido ' + fmt(got));
}

// Manifest derivado do firmware: sanidade contra o fonte (o numero exato de
// funcoes varia com WIP do firmware — os invariantes abaixo e que importam)
{
  const m = linter.buildManifest();
  check('manifest: API level 10', m.apiLevel === 10, 'apiLevel=' + m.apiLevel);
  check('manifest: >=100 funcoes parseadas', m.counts.functions >= 100, 'functions=' + m.counts.functions);
  check('manifest: todo corpo C++ encontrado', m.counts.bodiesFound === m.counts.functions,
    'bodiesFound=' + m.counts.bodiesFound + ' functions=' + m.counts.functions);
  check('manifest: namespaces', ['System', 'System.gpio', 'Net', 'FS', 'CelerLink'].every((n) => m.objects[n]),
    Object.keys(m.objects).join(','));
  check('manifest: sem warnings do parser', m.warnings.length === 0, fmt(m.warnings));
  const sys = m.objects['System.gpio'];
  check('manifest: gpio com permissoes e gate', sys.perm === 'gpio' && sys.kcfg.indexOf('CONFIG_CELEROS_JS_GPIO') >= 0,
    'perm=' + sys.perm + ' kcfg=' + fmt(sys.kcfg));
}

// Dogfood: os apps do repo tem que passar limpos (e o exit code respeta)
{
  const r = linter.runLint([], {});
  check('dogfood data/apps + hub_apps', r.ok && r.totals.errors === 0, fmt(r.totals));
}

console.log('');
if (failures) {
  console.log('FALHAS: ' + failures);
  process.exit(1);
}
console.log('OK: todos os testes passaram');
