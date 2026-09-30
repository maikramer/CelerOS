#!/usr/bin/env python3
"""Ferramenta de publicacao no CelerOS Hub (os.celer.tec.br).

Subcomandos:
  publish  <pasta> [pasta...]  valida e publica pacotes (app.json+main.js[+icon.png])
  list                         lista o catalogo do hub e compara com o repo
  delete   <packageName>      remove um app do hub
  whoami                       confere o token/escopo

Autenticacao: CELER_HUB_TOKEN no ambiente (veja CelerOS-Server/env/env.sh).
Hub: --hub ou CELER_HUB (padrao https://os.celer.tec.br).

Exemplos:
  source ../CelerOS-Server/env/env.sh
  python3 tools/celerhub.py publish data/apps/Terminal hub_apps/2048
  python3 tools/celerhub.py list
  python3 tools/celerhub.py delete com.kryonos.minhaapp --yes
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
MAX_MAIN_JS = 48 * 1024
STREAM_SAFE_MAIN_JS = 30 * 1024
REQUIRED = ("name", "packageName", "version", "author", "description")
PKG_RE = re.compile(r"^[a-z0-9]+(\.[a-z0-9]+)+$")
VER_RE = re.compile(r"^\d+\.\d+\.\d+$")


def vtuple(v):
    return tuple(int(p) for p in v.split("."))


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
    req.add_header("User-Agent", "celeroshub-cli/1.0 (kryonos tools)")
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


def validate(folder: Path):
    """Confere o pacote localmente; devolve (meta, avisos). Devolve erros via die."""
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
    api = int(meta.get("api") or 1)
    if api > MAX_API_LEVEL:
        die(f"{folder}: api {api} > {MAX_API_LEVEL} (device nao instala)")
    if api == 5 and "keypad" not in code_path.read_text(encoding="utf-8", errors="replace"):
        avisos.append("declara api 5 mas nao usa keypad*")

    # Lint estatico (ES5 real + API do firmware): erros bloqueiam o publish
    avisos.extend(run_app_lint(folder))

    size = code_path.stat().st_size
    if size > MAX_MAIN_JS:
        die(f"{folder}: main.js tem {size}B (max {MAX_MAIN_JS}B)")
    if size > STREAM_SAFE_MAIN_JS and api < 6:
        die(f"{folder}: main.js > {STREAM_SAFE_MAIN_JS}B exige api >= 6 no "
            f"app.json (firmware antigo trunca o download em 32KB)")
    if icon_path.is_file():
        isz = icon_path.stat().st_size
        if isz > 10 * 1024:
            avisos.append(f"icon.png grande ({isz}B); o pipeline gera ~2-5KB")
    else:
        avisos.append("sem icon.png (o launcher usa gradiente+inicial)")
    return meta, avisos, size


# ----------------------------------------------------------------- publish -
def cmd_publish(args):
    tok = "" if args.dry else token_or_die(args)  # dry roda offline
    # versao publicada no hub: base do anti-downgrade local (o hub reforca)
    hub_ver = {}
    if not args.dry:
        _, data = http("GET", f"{hub_url(args)}/store/all.json")
        hub_ver = {pkg: a.get("version", "0.0.0")
                   for pkg, a in data.get("apps", {}).items()}
    rc = 0
    for folder in args.folders:
        folder = Path(folder).expanduser().resolve()
        meta, avisos, size = validate(folder)
        for a in avisos:
            print(f"aviso: {folder.name}: {a}")
        hv = hub_ver.get(meta["packageName"])
        if hv and vtuple(meta["version"]) <= vtuple(hv) and not args.force:
            die(f"{folder}: v{meta['version']} <= publicada no hub (v{hv}); "
                f"suba a version ou use --force")
        if args.dry:
            print(f"[dry] {meta['packageName']} v{meta['version']} ({size}B) ok")
            continue

        with tempfile.TemporaryDirectory() as td:
            zpath = Path(td) / "app.zip"
            with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED) as zf:
                zf.write(folder / "app.json", "app.json")
                zf.write(folder / "main.js", "main.js")
                if (folder / "icon.png").is_file():
                    zf.write(folder / "icon.png", "icon.png")
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
