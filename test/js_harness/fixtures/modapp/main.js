// Fixture de modulos: require com exports, alias, troca de module.exports,
// modulo que requer modulo, cache (contagem de carga), ciclo com exports
// parcial e erro de sintaxe propagado (capturavel).
var notas = require("notas");
var audio = require("audio");
var contagem = require("contagem");

System.print("nota: " + notas.tocar("alerta"));
System.print("dobra: " + contagem.dobra(21));
System.print("beep: " + audio.beep());
System.print("loads: " + contagem.loads());

// troca inteira de module.exports (deep.js)
var deep = require("deep");
System.print("deep: " + deep.nome);

// cache: segunda chamada nao recarrega (loads continua 1)
require("contagem");
System.print("loads pos-cache: " + contagem.loads());

// ciclo: ciculo_a requer ciculo_b e vice-versa — b ve o exports PARCIAL de a
var a = require("ciculo_a");
System.print("ciclo: " + a.valorDeB());

// erro de sintaxe no modulo propaga e e capturavel
try {
    require("quebrado");
    System.print("quebrado: NAO lancou");
} catch (e) {
    System.print("quebrado capturado");
}

// modulo inexistente
try {
    require("fantasma");
} catch (e) {
    System.print("fantasma capturado");
}

System.exitApp();
