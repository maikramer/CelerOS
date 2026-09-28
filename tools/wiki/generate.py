#!/usr/bin/env python3
"""Gera as paginas da wiki do GitHub a partir das fontes do repositorio.

Fontes de verdade:
- wiki/wiki.json  : manifesto (repo, branch, paginas, sidebar)
- wiki/pages/*.md : paginas escritas a mao
- demais caminhos : guias/READMEs do repo reaproveitados como pagina

Reescrita de links relativos que apontam para arquivos do repo:
- imagens                       -> copiadas para assets/ dentro da wiki
- arquivo que tambem e pagina   -> link interno da wiki
- qualquer outro arquivo/pasta  -> URL absoluta do GitHub (blob/tree)

Links absolutos (http/https, comecando com '/'), ancoras (#...) e links de
wiki ([[Pagina]]) passam intactos.

Uso: python3 tools/wiki/generate.py [--out DIR] [--repo-root DIR]
Saida default: build/wiki (build*/ ja esta no .gitignore).
"""

import argparse
import json
import re
import shutil
import sys
from pathlib import Path, PurePosixPath

IMG_EXT = {".png", ".jpg", ".jpeg", ".gif", ".svg", ".webp", ".bmp"}
# [label](alvo) ou ![label](alvo), com titulo opcional
MD_LINK = re.compile(r"(!?)\[([^\]]*)\]\(([^)\s]+)(?:\s+\"[^\"]*\")?\)")
# <img src="..."> e afins no HTML embutido
HTML_SRC = re.compile(r"(src=\")([^\"]+)(\")")


def die(msg):
    print("ERRO: %s" % msg, file=sys.stderr)
    sys.exit(1)


def rel_posix(path, root):
    """Caminho relativo normalizado (posix) de `path` sob `root`, ou None."""
    try:
        rp = path.resolve().relative_to(root.resolve())
    except ValueError:
        return None
    return rp.as_posix()


def normalize_href(href, src_posix):
    """Resolve `href` relativo ao arquivo-fonte; devolve caminho posix do repo."""
    base = PurePosixPath(src_posix).parent
    joined = (base / href).as_posix()
    # colapsa ./ e ../
    parts = []
    for seg in PurePosixPath(joined).parts:
        if seg == ".":
            continue
        if seg == "..":
            if parts:
                parts.pop()
            continue
        parts.append(seg)
    return PurePosixPath(*parts).as_posix() if parts else "."


class WikiGen:
    def __init__(self, repo_root, out_dir, manifest):
        self.root = repo_root
        self.out = out_dir
        self.assets_dir = out_dir / "assets"
        self.manifest = manifest
        self.repo_url = manifest["repo_url"].rstrip("/")
        self.branch = manifest["branch"]
        self.slug = self.repo_url.split("github.com/", 1)[1].rstrip("/")
        # caminho-fonte (posix) -> nome da pagina na wiki
        self.page_of_src = {p["src"]: p["page"] for p in manifest["pages"]}
        self.copied = {}  # basename -> caminho de origem (deteccao de colisao)
        self.warnings = []

    def asset_url(self, repo_posix):
        """Copia a imagem para assets/ e devolve a URL dentro da wiki."""
        src = self.root / repo_posix
        base = PurePosixPath(repo_posix).name
        if base in self.copied and self.copied[base] != repo_posix:
            die("conflito de assets: dois arquivos com o basename %r (%s e %s)"
                % (base, self.copied[base], repo_posix))
        self.assets_dir.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, self.assets_dir / base)
        self.copied.setdefault(base, repo_posix)
        return "assets/" + base

    def file_url(self, repo_posix):
        target = self.root / repo_posix
        kind = "tree" if target.is_dir() else "blob"
        return "%s/%s/%s/%s" % (self.repo_url, kind, self.branch, repo_posix)

    def rewrite_target(self, href, src_posix):
        """Classifica um alvo relativo; devolve a URL reescrita ou None."""
        if href.startswith(("http://", "https://", "mailto:", "#", "/", "tel:",
                            "data:")):
            return None
        path, _, frag = href.partition("#")
        if not path:
            return None
        repo_posix = normalize_href(path, src_posix)
        target = self.root / repo_posix
        if not target.exists():
            # paginas de wiki/pages/ podem citar caminhos direto da raiz do repo
            alt = PurePosixPath(path).as_posix()
            if path not in ("", ".") and (self.root / alt).exists():
                repo_posix, target = alt, self.root / alt
            else:
                self.warnings.append("link quebrado em %s: %s" % (src_posix, href))
                return None
        if target.is_file() and PurePosixPath(repo_posix).suffix.lower() in IMG_EXT:
            return self.asset_url(repo_posix) + (("#" + frag) if frag else "")
        if repo_posix in self.page_of_src:
            return "/%s/wiki/%s%s" % (self.slug, self.page_of_src[repo_posix],
                                      ("#" + frag) if frag else "")
        return self.file_url(repo_posix) + (("#" + frag) if frag else "")

    def rewrite_md(self, text, src_posix):
        def repl(m):
            bang, label, href = m.group(1), m.group(2), m.group(3)
            new = self.rewrite_target(href, src_posix)
            return "%s[%s](%s)" % (bang, label, new) if new else m.group(0)
        return MD_LINK.sub(repl, text)

    def rewrite_html(self, text, src_posix):
        def repl(m):
            new = self.rewrite_target(m.group(2), src_posix)
            return "%s%s%s" % (m.group(1), new, m.group(3)) if new else m.group(0)
        return HTML_SRC.sub(repl, text)

    def build_page(self, entry):
        src_posix = entry["src"]
        src = self.root / src_posix
        if not src.is_file():
            die("fonte da pagina '%s' nao existe: %s" % (entry["page"], src_posix))
        text = src.read_text(encoding="utf-8")
        text = self.rewrite_html(self.rewrite_md(text, src_posix), src_posix)
        out = self.out / (entry["page"] + ".md")
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(text, encoding="utf-8")
        return entry["page"], out

    def build_sidebar(self):
        lines = []
        for section in self.manifest["sidebar"]:
            title = section.get("section")
            if title:
                lines.append("**%s**" % title)
            for page in section["pages"]:
                lines.append("- [%s](/%s/wiki/%s)"
                             % (page.replace("-", " "), self.slug, page))
            lines.append("")
        (self.out / "_Sidebar.md").write_text("\n".join(lines) + "\n",
                                              encoding="utf-8")

    def build_footer(self):
        text = ("---\n_Esta wiki e gerada automaticamente pelo CI a partir de "
                "[wiki/](%s/tree/%s/wiki) no repositorio. "
                "Edicoes feitas pela web serao sobrescritas._\n"
                % (self.repo_url, self.branch))
        (self.out / "_Footer.md").write_text(text, encoding="utf-8")

    def run(self):
        if self.out.exists():
            shutil.rmtree(self.out)
        self.out.mkdir(parents=True)
        pages = [self.build_page(e) for e in self.manifest["pages"]]
        self.build_sidebar()
        self.build_footer()
        print("wiki gerada em %s: %d paginas + sidebar/footer, %d assets"
              % (self.out, len(pages), len(self.copied)))
        for name, path in pages:
            print("  %-28s <- %s" % (name, rel_posix(path, self.root) or path))
        for w in self.warnings:
            print("AVISO: %s" % w)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    default_root = Path(__file__).resolve().parents[2]
    ap.add_argument("--repo-root", type=Path, default=default_root,
                    help="raiz do repositorio (default: autodetectado)")
    ap.add_argument("--out", type=Path, default=None,
                    help="diretorio de saida (default: <raiz>/build/wiki)")
    args = ap.parse_args()
    out = args.out or (args.repo_root / "build" / "wiki")

    manifest_path = args.repo_root / "wiki" / "wiki.json"
    if not manifest_path.is_file():
        die("manifesto nao encontrado: %s" % manifest_path)
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))

    names = [p["page"] for p in manifest["pages"]]
    if len(names) != len(set(names)):
        die("paginas duplicadas no manifesto")
    for section in manifest["sidebar"]:
        for page in section["pages"]:
            if page not in names:
                die("pagina da sidebar nao consta do manifesto: %s" % page)

    WikiGen(args.repo_root, out, manifest).run()


if __name__ == "__main__":
    main()
