#!/usr/bin/env python3
"""Ferramenta de publicacao no CelerOS Hub (os.celer.tec.br).

Subcomandos:
  publish      <pasta> [pasta...]      valida e publica pacotes (app.json+main.js[+icon.png])
  publish-dep  <arquivo.js>            publica dependencia JS no repo do hub
                                       (nome = arquivo; versao lida do modulo)
  list                               lista o catalogo do hub e compara com o repo
  delete       <packageName>           remove um app do hub
  whoami                              confere o token/escopo

Autenticacao: CELER_HUB_TOKEN no ambiente (veja CelerOS-Server/env/env.sh).
Hub: --hub ou CELER_HUB (padrao https://os.celer.tec.br).

Exemplos:
  source ../CelerOS-Server/env/env.sh
  python3 tools/celerhub.py publish data/apps/Terminal hub_apps/2048
  python3 tools/celerhub.py publish-dep tools/sdk/engine/celeros.engine.js --min-api 28
  python3 tools/celerhub.py list
  python3 tools/celerhub.py delete celeros.minhaapp --yes
"""

import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path
from urllib import error, request


def _read_api_level():
    # API level maximo que os devices entendem — fonte unica: main/CMakeLists.txt
    cmake = Path(__file__).resolve().parent.parent / "main" / "CMakeLists.txt"
    m = re.search(r"CELEROS_API_LEVEL=(\d+)", cmake.read_text(encoding="utf-8"))
    if not m:
        die(f"{cmake}: CELEROS_API_LEVEL nao encontrado")
    return int(m.group(1))


MAX_API_LEVEL = _read_api_level()
# Teto do hub (download streaming via Net.download, sem limite de 32KB).
# Acima de STREAM_SAFE o app precisa declarar api >= 6: firmwares antigos
# instalavam via Net.get, que trunca o corpo em 32KB.
# Teto em 2 niveis: sem PSRAM o main.js inteiro (mais o heap Duktape e os
# temporarios do compile) tem que caber na RAM interna — medido na CYD
# (ENGINE_NOTES rodada 4): 61KB roda, 82KB nao compila. Declarando
# "psram" em requires o app sobe para MAX_MAIN_JS_PSRAM: as placas S3
# com PSRAM (smartdisplay, dog, watch) compilam sem limite runtime.
MAX_MAIN_JS = 48 * 1024
MAX_MAIN_JS_PSRAM = 128 * 1024
STREAM_SAFE_MAIN_JS = 30 * 1024
VALID_REQUIRES = ("psram",)
# Pacote multi-arquivo (modulos .js + assets): flat, sem subpastas; mesmas
# regras do servidor (CelerOS-Server app.py). O hub computa o campo gerenciado
# "files" {nome: {size, md5}} no publish — nunca setar a mao. Modulos contam
# na SOMA do teto de compile; assets (nao-.js) tem tetos proprios.
ASSET_EXTS = (".js", ".png", ".wav", ".qoa", ".json", ".bin")
FILE_NAME_RE = re.compile(r"^[A-Za-z0-9._-]{1,64}$")
MAX_ASSET_FILE = 128 * 1024
MAX_ASSETS_TOTAL = 256 * 1024
MAX_EXTRA_FILES = 16
REQUIRED = ("name", "packageName", "version", "author", "description")
PKG_RE = re.compile(r"^[a-z0-9]+(\.[a-z0-9]+)+$")
VER_RE = re.compile(r"^\d+\.\d+\.\d+$")
# Dependencias compartilhadas (mesmas regras do servidor): nome com ponto
# tipo celeros.engine, range "^x.y.z" (major) ou versao exata.
DEP_NAME_RE = re.compile(r"^[a-z0-9]+(\.[a-z0-9-]+)+$")
DEP_RANGE_RE = re.compile(r"^\^?\d+\.\d+\.\d+$")
MAX_APP_DEPS = 8


def vtuple(v):
    return tuple(int(p) for p in v.split("."))


def range_satisfies(rng, version):
    """'^x.y.z' = mesma major, >= base; sem '^' = exata."""
    if not DEP_RANGE_RE.match(rng) or not VER_RE.match(version):
        return False
    if not rng.startswith("^"):
        return rng == version
    b, v = vtuple(rng[1:]), vtuple(version)
    return v[0] == b[0] and v >= b


def resolve_dep_ranges(app_deps, hub_deps):
    """nome -> versao resolvida (maior satisfazendo TODOS os ranges que
    chegam ao nome, diretos ou transitivos). die() se algo nao resolve."""
    wanted, seen, queue = {}, set(), list(app_deps.items())
    while queue:
        name, rng = queue.pop(0)
        if (name, rng) in seen:
            continue
        seen.add((name, rng))
        wanted.setdefault(name, []).append(rng)
        versions = hub_deps.get(name) or {}
        ok = [v for v in versions
              if all(range_satisfies(r, v) for r in wanted[name])]
        if not ok:
            die(f"dep '{name}' sem versao que satisfaca {' / '.join(wanted[name])} no hub")
        for d, r in (versions[max(ok, key=vtuple)].get("deps") or {}).items():
            queue.append((str(d), str(r)))
    return {n: max((v for v in hub_deps.get(n, {}) if all(range_satisfies(r, v) for r in rs)),
                   key=vtuple) for n, rs in wanted.items()}


def die(msg, code=1):
    print(f"erro: {msg}", file=sys.stderr)
    sys.exit(code)


def hub_url(args):
    return (args.hub or os.environ.get("CELER_HUB") or "https://os.celer.tec.br").rstrip("/")


def token_or_die(args):
    tok = args.token or os.environ.get("CELER_HUB_TOKEN") or ""
    if not tok:
        die("sem token: export CELER_HUB_TOKEN=... (CelerOS-Server/env/env.sh)")
    return tok


def http(method, url, token=None, data=None, headers=None, timeout=30):
    req = request.Request(url, data=data, method=method)
    if token:
        req.add_header("Authorization", f"Bearer {token}")
    # Cloudflare na frente do hub bloqueia o UA default do urllib (erro 1010)
    req.add_header("User-Agent", "celeroshub-cli/1.0 (celeros tools)")
    for k, v in (headers or {}).items():
        req.add_header(k, v)
    try:
        with request.urlopen(req, timeout=timeout) as resp:
            body = resp.read().decode()
            try:
                return resp.status, json.loads(body)
            except ValueError:
                return resp.status, body
    except error.HTTPError as e:
        detail = e.read().decode(errors="replace")
        try:
            detail = json.loads(detail)
        except ValueError:
            pass
        die(f"servidor recusou ({e.code}): {detail}")
    except error.URLError as e:
        die(f"falha de rede: {e.reason}")


# --------------------------------------------------------------- validacao -
def run_app_lint(folder: Path):
    """Lint estatico do app (tools/app_lint): parser ES5 de verdade + checagem
    da API do firmware (funcoes, aridades, permissoes, niveis). Erros bloqueiam.
    Requer Node no PATH (mesmo do harness). Devolve avisos como strings."""
    lint = Path(__file__).resolve().parent / "app_lint" / "lint.js"
    if not lint.is_file():
        die("tools/app_lint/lint.js ausente")
    try:
        out = subprocess.run(
            ["node", str(lint), "--json", str(folder)],
            capture_output=True, text=True, timeout=120,
        )
    except FileNotFoundError:
        die("node nao encontrado no PATH: necessario para o lint do app")
    except subprocess.TimeoutExpired:
        die("lint do app demorou demais (120s)")
    if out.returncode == 2:
        die(f"lint falhou: {(out.stderr or out.stdout).strip()}")
    try:
        r = json.loads(out.stdout)
    except ValueError:
        die(f"saida inesperada do lint: {out.stdout[:200]!r}")
    diags = [d for a in r.get("apps", []) for d in a.get("diagnostics", [])]
    erros = [d for d in diags if d.get("severity") == "erro"]
    if erros:
        for d in erros:
            print(f"erro: {d['file']}:{d['line']}:{d['col']} {d['message']}", file=sys.stderr)
        die(f"{folder}: {len(erros)} erro(s) no lint — node tools/app_lint/lint.js {folder}")
    return [f"lint: {d['file']}:{d['line']} {d['message']}" for d in diags]


def package_files(folder: Path):
    """Extras do pacote (modulos .js + assets), FLAT: tudo na raiz da pasta
    que passa nome/extensao e nao e dev-only ou lixo de editor. Devolve
    {nome: Path}. app.json/main.js/icon.png ficam de fora (tem tratamento
    proprio); o celerctl apps install e o app_lint excluem os mesmos nomes."""
    out = {}
    for p in sorted(folder.iterdir()):
        n = p.name
        if not p.is_file() or n in ("app.json", "main.js", "icon.png", "test.js"):
            continue
        # artefatos do scaffold do SDK (celer.js new): dev-only
        if n in ("README.md", "jsconfig.json") or n.endswith(".d.ts"):
            continue
        if n.startswith(".") or n.endswith((".dev", ".part", ".new", "~", ".swp")):
            continue
        out[n] = p
    return out


def validate(folder: Path):
    """Confere o pacote localmente; devolve (meta, avisos, size, extras).
    Devolve erros via die."""
    avisos = []
    meta_path, code_path, icon_path = (folder / "app.json", folder / "main.js", folder / "icon.png")
    if not meta_path.is_file() or not code_path.is_file():
        die(f"{folder}: precisa conter app.json e main.js")
    try:
        meta = json.loads(meta_path.read_text(encoding="utf-8"))
    except ValueError as e:
        die(f"{folder}: app.json invalido: {e}")
    for f in REQUIRED:
        if not str(meta.get(f) or "").strip():
            die(f"{folder}: app.json sem campo obrigatorio: {f}")
    if not PKG_RE.match(meta["packageName"]):
        die(f"{folder}: packageName invalido (ex.: celeros.meuapp)")
    if not VER_RE.match(meta["version"]):
        die(f"{folder}: version deve ser semver x.y.z")
    if "files" in meta:
        die(f"{folder}: campo \"files\" e computado pelo hub: nunca setar a mao")
    api = int(meta.get("api") or 1)
    if api > MAX_API_LEVEL:
        die(f"{folder}: api {api} > {MAX_API_LEVEL} (device nao instala)")
    if api == 5 and "keypad" not in code_path.read_text(encoding="utf-8", errors="replace"):
        avisos.append("declara api 5 mas nao usa keypad*")

    # Lint estatico (ES5 real + API do firmware): erros bloqueiam o publish
    avisos.extend(run_app_lint(folder))

    # Requisitos de hardware declarados (o app_lint e o servidor validam os
    # mesmos valores; a loja usa o campo p/ badge "Requer PSRAM" + bloqueio).
    requires = meta.get("requires") or []
    if not isinstance(requires, list) or any(r not in VALID_REQUIRES for r in requires):
        die(f"{folder}: requires invalido (valores: {', '.join(VALID_REQUIRES)})")

    # Teto em 2 niveis pela SOMA dos .js (main.js + modulos): e a soma que
    # ocupa a RAM de compile no device. Assets (nao-.js) tem tetos proprios.
    extras = package_files(folder)
    if len(extras) > MAX_EXTRA_FILES:
        die(f"{folder}: {len(extras)} arquivos extras (max {MAX_EXTRA_FILES})")
    js_sum = code_path.stat().st_size
    assets_total = 0
    for n, p in extras.items():
        if not FILE_NAME_RE.match(n) or p.suffix.lower() not in ASSET_EXTS:
            die(f"{folder}: arquivo extra invalido: {n} "
                f"(nome [A-Za-z0-9._-], extensoes: {', '.join(ASSET_EXTS)})")
        size_n = p.stat().st_size
        if size_n > MAX_ASSET_FILE:
            die(f"{folder}: {n} tem {size_n}B (max {MAX_ASSET_FILE}B)")
        if n.endswith(".js"):
            js_sum += size_n
            if size_n > STREAM_SAFE_MAIN_JS and api < 6:
                die(f"{folder}: {n} > {STREAM_SAFE_MAIN_JS}B exige api >= 6 "
                    f"(firmware antigo trunca o download em 32KB)")
        else:
            assets_total += size_n
    if assets_total > MAX_ASSETS_TOTAL:
        die(f"{folder}: assets somam {assets_total}B (max {MAX_ASSETS_TOTAL}B)")

    # Dependencias compartilhadas (API 30): formato local; existencia no hub
    # e a soma das deps no teto sao checadas no cmd_publish com o indice
    # real (o servidor refaz a validacao no upload).
    deps = meta.get("deps")
    if deps is not None:
        if not isinstance(deps, dict) or not deps:
            die(f"{folder}: deps deve ser objeto {{nome: \"^x.y.z\"}} nao-vazio")
        if api < 30:
            die(f"{folder}: deps exige api >= 30 no app.json (o require so "
                f"resolve dependencia na API 30)")
        if len(deps) > MAX_APP_DEPS:
            die(f"{folder}: deps com {len(deps)} entradas (max {MAX_APP_DEPS})")
        for d, r in deps.items():
            if not DEP_NAME_RE.match(str(d)):
                die(f"{folder}: dep com nome invalido: {d} (use prefixo.nome)")
            if not DEP_RANGE_RE.match(str(r)):
                die(f"{folder}: dep {d}: versao deve ser \"^1.0.0\" ou \"1.0.0\"")
            if f"{d}.js" in extras:
                avisos.append(f"deps declara {d} mas {d}.js esta na pasta: "
                              f"a copia local vence no require (vendoring desnecessario)")

    if js_sum > MAX_MAIN_JS_PSRAM:
        die(f"{folder}: soma dos .js ({js_sum}B) acima do teto absoluto "
            f"({MAX_MAIN_JS_PSRAM}B)")
    if js_sum > MAX_MAIN_JS and "psram" not in requires:
        die(f"{folder}: soma dos .js ({js_sum}B): acima de {MAX_MAIN_JS}B exige "
            f"\"psram\" em requires no app.json (sem PSRAM a RAM interna "
            f"nao fecha o compile)")
    if code_path.stat().st_size > STREAM_SAFE_MAIN_JS and api < 6:
        die(f"{folder}: main.js > {STREAM_SAFE_MAIN_JS}B exige api >= 6 no "
            f"app.json (firmware antigo trunca o download em 32KB)")
    if icon_path.is_file():
        isz = icon_path.stat().st_size
        if isz > 10 * 1024:
            avisos.append(f"icon.png grande ({isz}B); o pipeline gera ~2-5KB")
    else:
        avisos.append("sem icon.png (o launcher usa gradiente+inicial)")
    return meta, avisos, js_sum, extras


# ----------------------------------------------------------------- publish -
def cmd_publish(args):
    tok = "" if args.dry else token_or_die(args)  # dry roda offline
    # versao publicada no hub: base do anti-downgrade local (o hub reforca)
    hub_ver = {}
    hub_deps = None
    if not args.dry:
        _, data = http("GET", f"{hub_url(args)}/store/all.json")
        hub_ver = {pkg: a.get("version", "0.0.0")
                   for pkg, a in data.get("apps", {}).items()}
    rc = 0
    for folder in args.folders:
        folder = Path(folder).expanduser().resolve()
        meta, avisos, js_sum, extras = validate(folder)
        deps = meta.get("deps") or {}
        # deps: resolve contra o indice do hub (existencia + teto com a soma
        # das deps — o mesmo calculo que o servidor faz no upload)
        if deps:
            if args.dry:
                for a in [f"deps {d} {r}: existencia no hub nao checada (dry)"
                          for d, r in deps.items()]:
                    avisos.append(a)
            else:
                if hub_deps is None:
                    _, idx = http("GET", f"{hub_url(args)}/store/deps.json")
                    hub_deps = idx.get("deps", {})
                resolved = resolve_dep_ranges(deps, hub_deps)
                for dname, dver in resolved.items():
                    dmeta = hub_deps[dname][dver]
                    js_sum += int(dmeta.get("size") or 0)
                    dmin = int(dmeta.get("minApi") or 1)
                    if int(meta.get("api") or 1) < dmin:
                        die(f"{folder}: dep {dname}@{dver} exige api >= {dmin} "
                            f"(app declara {meta.get('api')})")
        requires = meta.get("requires") or []
        if js_sum > MAX_MAIN_JS_PSRAM:
            die(f"{folder}: soma dos .js + deps ({js_sum}B) acima do teto "
                f"absoluto ({MAX_MAIN_JS_PSRAM}B)")
        if js_sum > MAX_MAIN_JS and "psram" not in requires:
            die(f"{folder}: soma dos .js + deps ({js_sum}B): acima de "
                f"{MAX_MAIN_JS}B exige \"psram\" em requires no app.json")
        for a in avisos:
            print(f"aviso: {folder.name}: {a}")
        hv = hub_ver.get(meta["packageName"])
        if hv and vtuple(meta["version"]) <= vtuple(hv) and not args.force:
            die(f"{folder}: v{meta['version']} <= publicada no hub (v{hv}); "
                f"suba a version ou use --force")
        if args.dry:
            nextra = f" + {len(extras)} extra(s)" if extras else ""
            ndeps = f" + {len(deps)} dep(s)" if deps else ""
            print(f"[dry] {meta['packageName']} v{meta['version']} "
                  f"({js_sum}B de .js{ndeps}{nextra}) ok")
            continue

        with tempfile.TemporaryDirectory() as td:
            zpath = Path(td) / "app.zip"
            with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED) as zf:
                zf.write(folder / "app.json", "app.json")
                zf.write(folder / "main.js", "main.js")
                if (folder / "icon.png").is_file():
                    zf.write(folder / "icon.png", "icon.png")
                for n, p in extras.items():
                    zf.write(p, n)
            blob = zpath.read_bytes()

        boundary = "----celeroshub7d1f2c"
        part = (
            f"--{boundary}\r\n"
            "Content-Disposition: form-data; name=\"file\"; filename=\"app.zip\"\r\n"
            "Content-Type: application/zip\r\n\r\n"
        ).encode()
        force_part = (
            f"--{boundary}\r\n"
            "Content-Disposition: form-data; name=\"force\"\r\n\r\n"
            f"{'1' if args.force else '0'}\r\n"
        ).encode()
        _, out = http("POST", f"{hub_url(args)}/admin/apps", token=tok,
                      data=part + blob + force_part + f"--{boundary}--\r\n".encode(),
                      headers={"Content-Type": f"multipart/form-data; boundary={boundary}"})
        print(f"ok: {out.get('package')} v{out.get('version')} publicado "
              f"({out.get('size', '?')}B, md5 {str(out.get('md5', '?'))[:8]}..., "
              f"{out.get('store', {}).get('apps')} apps no catalogo)")
    sys.exit(rc)


# ------------------------------------------------------------- publish-dep -
def cmd_publish_dep(args):
    """Publica um modulo JS como dependencia compartilhada do hub.

    Nome = nome do arquivo sem .js (tools/sdk/engine/celeros.engine.js ->
    dep "celeros.engine"); versao lida do `version: 'x.y.z'` do modulo;
    --min-api e o CELEROS_API_LEVEL minimo do device que roda a dep.
    Transitivas: --dep nome=range (repetivel)."""
    tok = token_or_die(args)
    src = Path(args.file).expanduser().resolve()
    if not src.is_file():
        die(f"{src}: arquivo nao encontrado")
    name = args.name or src.stem
    if not DEP_NAME_RE.match(name):
        die(f"nome de dep invalido: {name} (use prefixo.nome, ex: celeros.engine)")
    text = src.read_text(encoding="utf-8")
    m = re.search(r"version:\s*'([^']+)'", text)
    version = args.version or (m.group(1) if m else "")
    if not VER_RE.match(version or ""):
        die(f"versao invalida: {version!r} (declare no modulo "
            f"version: 'x.y.z' ou use --version)")
    sub = {}
    for pair in args.dep or []:
        if "=" not in pair:
            die(f"--dep deve ser nome=range (veio {pair!r})")
        d, r = pair.split("=", 1)
        if not DEP_NAME_RE.match(d) or not DEP_RANGE_RE.match(r):
            die(f"--dep invalida: {pair}")
        sub[d] = r

    with tempfile.TemporaryDirectory() as td:
        zpath = Path(td) / "dep.zip"
        with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED) as zf:
            zf.writestr("dep.json", json.dumps({
                "name": name, "version": version,
                "minApi": args.min_api, "deps": sub,
            }))
            zf.write(src, f"{name}.js")
            blob = zpath.read_bytes()

    boundary = "----celeroshub7d1f2c"
    part = (
        f"--{boundary}\r\n"
        "Content-Disposition: form-data; name=\"file\"; filename=\"dep.zip\"\r\n"
        "Content-Type: application/zip\r\n\r\n"
    ).encode()
    force_part = (
        f"--{boundary}\r\n"
        "Content-Disposition: form-data; name=\"force\"\r\n\r\n"
        f"{'1' if args.force else '0'}\r\n"
    ).encode()
    _, out = http("POST", f"{hub_url(args)}/admin/deps", token=tok,
                  data=part + blob + force_part + f"--{boundary}--\r\n".encode(),
                  headers={"Content-Type": f"multipart/form-data; boundary={boundary}"})
    print(f"ok: dep {out.get('dep')} v{out.get('version')} publicada "
          f"({out.get('size', '?')}B, md5 {str(out.get('md5', '?'))[:8]}...)")
    print(f"    {out.get('url')}")


# -------------------------------------------------------------------- list -
def local_packages():
    out = {}
    root = Path(__file__).resolve().parent.parent
    for base in ("data/apps", "hub_apps"):
        d = root / base
        if not d.is_dir():
            continue
        for pkg in sorted(d.iterdir()):
            mp = pkg / "app.json"
            if not mp.is_file():
                continue
            try:
                meta = json.loads(mp.read_text(encoding="utf-8"))
                out[meta["packageName"]] = meta["version"]
            except (ValueError, KeyError):
                continue
    return out


def cmd_list(args):
    _, cats = http("GET", f"{hub_url(args)}/store/index.json")
    _, data = http("GET", f"{hub_url(args)}/store/all.json")
    _, info = http("GET", f"{hub_url(args)}/api/info")
    apps = data.get("apps", {})
    downloads = info.get("downloads", {})
    local = local_packages()
    print(f"CelerOS Hub ({hub_url(args)}) - {len(apps)} apps em "
          f"{len(cats.get('categories', {}))} categorias\n")
    print(f"{'pacote':40} {'v':8} {'api':4} {'tam':7} {'down':5} {'categoria':12} local")
    for pkg in sorted(apps):
        a = apps[pkg]
        lv = local.get(pkg)
        hv = a.get("version", "?")
        if lv is None:
            mark = "-"
        elif lv == hv:
            mark = "igual"
        elif vtuple(lv) > vtuple(hv):
            mark = f"local v{lv} (hub atrasado)"
        else:
            mark = f"hub v{hv} (device tem update)"
        size = a.get("size")
        size_s = f"{size // 1024}KB" if size else "-"
        print(f"{pkg:40} {hv:8} {a.get('api', '?'):4} {size_s:7} "
              f"{downloads.get(pkg, 0):<5} {a.get('category', '?'):12} {mark}")
    # pacotes locais que ainda nao estao no hub
    for pkg, lv in sorted(local.items()):
        if pkg not in apps:
            print(f"{pkg:40} {'-':8} {'-':4} {'-':7} {'-':5} {'-':12} "
                  f"local v{lv} (nao publicado)")


# ------------------------------------------------------------------- delete -
def cmd_delete(args):
    tok = token_or_die(args)
    if not args.yes:
        die(f"delete e destrutivo: confirme com --yes (alvo: {args.package})")
    _, out = http("DELETE", f"{hub_url(args)}/admin/apps/{args.package}", token=tok)
    print(f"ok: {out.get('deleted', args.package)} removido "
          f"({out.get('store', {}).get('apps')} apps no catalogo)")


# ------------------------------------------------------------------ whoami -
def cmd_whoami(args):
    tok = token_or_die(args)
    _, out = http("GET", f"{hub_url(args)}/admin/whoami", token=tok)
    print(f"agente: {out.get('name')}  escopos: {', '.join(out.get('scopes', []))}")


# -------------------------------------------------------------------- main -
def main():
    ap = argparse.ArgumentParser(description="publicacao no CelerOS Hub")
    ap.add_argument("--hub", default=None, help="URL do hub (ou CELER_HUB)")
    ap.add_argument("--token", default=None, help="bearer (ou CELER_HUB_TOKEN)")
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("publish", help="publica um ou mais pacotes")
    p.add_argument("folders", nargs="+", help="pastas com app.json + main.js")
    p.add_argument("--dry", action="store_true", help="so valida, nao envia")
    p.add_argument("--force", action="store_true",
                   help="republica mesmo com version <= a do hub")
    p.set_defaults(fn=cmd_publish)

    p = sub.add_parser("publish-dep", help="publica dependencia JS no repo do hub")
    p.add_argument("file", help="arquivo <nome>.js da dep (ex: tools/sdk/engine/celeros.engine.js)")
    p.add_argument("--name", default=None, help="nome da dep (default: arquivo sem .js)")
    p.add_argument("--version", default=None, help="versao semver (default: lida do modulo)")
    p.add_argument("--min-api", type=int, default=1, dest="min_api",
                   help="CELEROS_API_LEVEL minimo do device (default: 1)")
    p.add_argument("--dep", action="append", metavar="NOME=RANGE",
                   help="dep transitiva (repetivel)")
    p.add_argument("--force", action="store_true", help="republica a versao")
    p.set_defaults(fn=cmd_publish_dep)

    p = sub.add_parser("list", help="lista o catalogo e compara com o repo")
    p.set_defaults(fn=cmd_list)

    p = sub.add_parser("delete", help="remove um app do hub")
    p.add_argument("package", help="packageName")
    p.add_argument("--yes", action="store_true")
    p.set_defaults(fn=cmd_delete)

    p = sub.add_parser("whoami", help="confere token/escopo")
    p.set_defaults(fn=cmd_whoami)

    args = ap.parse_args()
    args.fn(args)


if __name__ == "__main__":
    main()
