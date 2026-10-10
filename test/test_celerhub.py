#!/usr/bin/env python3
"""Testes do celerhub.py (lado host, sem rede): leitor de versao das deps,
relatorio de drift repo x hub, pre-flight do publish-dep e o teto de
compile contado pelo tamanho ENXUTO.

    python3 -m unittest test.test_celerhub -v
"""

import contextlib
import io
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
import celerhub as H  # noqa: E402


def die_saida(fn):
    """Roda fn esperando o SystemExit do die(); devolve a mensagem (stderr)."""
    err = io.StringIO()
    with contextlib.redirect_stderr(err):
        try:
            fn()
        except SystemExit:
            return err.getvalue()
    raise AssertionError("esperava SystemExit (die)")


class TestReadDepVersion(unittest.TestCase):
    def test_aspas_simples_e_duplas(self):
        self.assertEqual(H.read_dep_version("var E = { version: '1.2.3' };"), "1.2.3")
        self.assertEqual(H.read_dep_version('var E = { version: "1.2.3" };'), "1.2.3")

    def test_sem_versao_morre(self):
        with self.assertRaises(SystemExit):
            H.read_dep_version("var E = {};")

    def test_versoes_distintas_morre(self):
        # antes: a primeira `version: '...'` vencia calada
        msg = die_saida(lambda: H.read_dep_version(
            "var a = { version: '1.0.0' }; var E = { version: '1.2.3' };"))
        self.assertIn("distintas", msg)

    def test_repetida_igual_passa(self):
        src = "// version: '1.2.3' no comentario\nvar E = { version: '1.2.3' };"
        self.assertEqual(H.read_dep_version(src), "1.2.3")


class TestDepsStatusReport(unittest.TestCase):
    def test_vereditos(self):
        local = {"celeros.engine": "1.2.2", "celeros.physics": "1.4.0",
                 "celeros.nova": "0.1.0", "celeros.sfx": "1.0.1"}
        hub = {"celeros.engine": {"1.1.0": {}, "1.2.0": {}, "1.2.1": {}},
               "celeros.physics": {"1.4.0": {}, "1.5.0": {}},
               "celeros.sfx": {"1.0.1": {}},
               "celeros.antiga": {"2.0.0": {}}}
        linhas, drift = H.deps_status_report(local, hub)
        texto = "\n".join(linhas)
        self.assertEqual(drift, 3)  # engine + physics + nova
        self.assertIn("celeros.engine         1.2.2    1.2.1    REPO NAO PUBLICADO", texto)
        self.assertIn("celeros.physics        1.4.0    1.5.0    REPO ATRASADO", texto)
        self.assertIn("celeros.nova           0.1.0    -        NAO PUBLICADA", texto)
        self.assertIn("celeros.sfx            1.0.1    1.0.1    ok", texto)
        self.assertIn("celeros.antiga         -        2.0.0    sem fonte local", texto)

    def test_conteudo_diverge(self):
        local = {"celeros.engine": "1.2.2"}
        hub = {"celeros.engine": {"1.2.2": {"md5": "abc"}}}
        linhas, drift = H.deps_status_report(local, hub, divergent={"celeros.engine"})
        self.assertEqual(drift, 1)
        self.assertIn("CONTEUDO DIVERGE", "\n".join(linhas))

    def test_tudo_ok_zero_drift(self):
        local = {"celeros.engine": "1.2.2"}
        hub = {"celeros.engine": {"1.2.2": {"md5": "x"}}}
        _, drift = H.deps_status_report(local, hub)
        self.assertEqual(drift, 0)


class TestPublishDepPreflight(unittest.TestCase):
    """O pre-flight de mesma-versao morre com a receita, nao com o 409."""

    def _mod(self, td, versao="1.2.3", corpo=None):
        p = Path(td) / "celeros.teste.js"
        p.write_text(corpo or f"var T = {{ version: '{versao}' }};\nmodule.exports = T;\n",
                     encoding="utf-8")
        return p

    def test_dry_roda_offline(self):
        with tempfile.TemporaryDirectory() as td:
            p = self._mod(td)
            args = H.argparse.Namespace(
                dry=True, force=False, file=str(p), name=None, version=None,
                min_api=1, dep=None, no_strip=False, hub=None, token=None)
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                H.cmd_publish_dep(args)
            self.assertIn("[dry] dep celeros.teste v1.2.3", out.getvalue())
            self.assertIn("md5", out.getvalue())

    def test_versao_ambigua_morre_antes_do_upload(self):
        with tempfile.TemporaryDirectory() as td:
            p = self._mod(td, corpo="var a = { version: '9.9.9' };\n"
                                    "var T = { version: '1.2.3' };\nmodule.exports = T;\n")
            args = H.argparse.Namespace(
                dry=True, force=False, file=str(p), name=None, version=None,
                min_api=1, dep=None, no_strip=False, hub=None, token=None)
            with self.assertRaises(SystemExit):
                H.cmd_publish_dep(args)

    def test_sintaxe_invalida_morre(self):
        with tempfile.TemporaryDirectory() as td:
            p = self._mod(td, corpo="var T = { version: '1.2.3' }}};\nmodule.exports = T;\n")
            args = H.argparse.Namespace(
                dry=True, force=False, file=str(p), name=None, version=None,
                min_api=1, dep=None, no_strip=False, hub=None, token=None)
            msg = die_saida(lambda: H.cmd_publish_dep(args))
            self.assertIn("sintaxe invalida", msg)


class TestTetoPosStrip(unittest.TestCase):
    """A soma do teto conta o ENXUTO: main.js gordo de comentarios que
    enxuga abaixo do teto passa (antes morria com mensagem errada antes do
    strip rodar)."""

    def _app(self, td, api=30, requires=None):
        d = Path(td) / "Gordo"
        d.mkdir()
        (d / "app.json").write_text(
            '{"name":"Gordo","packageName":"celeros.gordo","version":"1.0.0",'
            '"api":%d,"author":"t","description":"d","permissions":["fs"]%s}' %
            (api, ',"requires":["psram"]' if requires else ""), encoding="utf-8")
        # ~70KB crus de comentario + uma linha de codigo (enxuga a ~30B)
        corpo = ("// " + "x" * 96 + "\n") * 700 + "System.delay(100);\n"
        (d / "main.js").write_text(corpo, encoding="utf-8")
        return d

    def test_cru_gordo_enxuto_magro_passa(self):
        with tempfile.TemporaryDirectory() as td:
            d = self._app(td)
            meta, avisos, js_sum, extras = H.validate(d, strip=True)
            cru = (d / "main.js").stat().st_size
            self.assertGreater(cru, H.MAX_MAIN_JS)          # ~70KB cru
            self.assertLess(js_sum, H.MAX_MAIN_JS)          # enxuto passa
            self.assertEqual(meta["packageName"], "celeros.gordo")

    def test_sem_strip_o_mesmo_app_morre(self):
        with tempfile.TemporaryDirectory() as td:
            d = self._app(td)
            msg = die_saida(lambda: H.validate(d, strip=False))
            self.assertIn("psram", msg)


if __name__ == "__main__":
    unittest.main()
