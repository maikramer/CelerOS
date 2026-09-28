# wiki/ — fonte da wiki do GitHub

Esta pasta é a fonte da verdade da
[wiki do repositório](https://github.com/maikramer/CelerOS/wiki). O
workflow [.github/workflows/wiki.yml](../.github/workflows/wiki.yml) roda o
gerador a cada push no `main` (ou PR) que mexa em documentação e publica o
resultado no repo da wiki (`maikramer/CelerOS.wiki.git`) com force-push.
**Edições feitas pela web são sobrescritas** — sempre edite aqui.

## Estrutura

```
wiki/
├── wiki.json     # manifesto: repo, branch, paginas (fonte -> pagina) e sidebar
└── pages/*.md    # paginas escritas a mao (Home, Arquitetura, ...)
```

As demais páginas são reaproveitadas de arquivos que já existem no repo
(`Documentation/JS_API_Guide*.md`, `tools/README_OTA*.md`, ...) por caminho,
via manifesto.

## Gerando localmente

```bash
python3 tools/wiki/generate.py            # saida em build/wiki
python3 tools/wiki/generate.py --out /tmp/wiki
```

O gerador é Python puro (stdlib). Links relativos que apontam para arquivos
do repo são reescritos na saída:

* imagens → copiadas para `assets/` dentro da wiki;
* arquivo que também é página → link interno da wiki;
* qualquer outro arquivo/pasta → URL absoluta do GitHub (`blob`/`tree`).

Links absolutos, âncoras e caminhos começando com `/` (links internos da
wiki) passam intactos. Links quebrados viram aviso no console.

## Como adicionar uma página

1. Escreva `wiki/pages/Nome-Da-Pagina.md` (ou aponte o `src` para um
   arquivo existente do repo no `wiki.json`).
2. Acrescente a entrada em `pages` e a seção certa em `sidebar` no
   `wiki.json`.
3. Rode o gerador para conferir e commit — o CI publica no merge.
