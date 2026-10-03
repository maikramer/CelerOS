var cfg = { nome: "bench", passos: [1, 2, 3], cor: 0xF800 };
var total = 0;

function soma(lista) {
    var acc = 0;
    for (var i = 0; i < lista.length; i++) {
        acc += lista[i];
    }
    return acc;
}

function passo(n) {
    var ponto = { x: n * 10, y: n * 5 };
    total += soma(cfg.passos) + ponto.x;
    if (n === 6) {
        var quebrado = null;
        return quebrado.campo;
    }
    return total;
}

for (var n = 1; ; n++) {
    System.fillScreen(0);
    System.drawString("passo " + n + " total " + passo(n), 20, 120, 2);
    System.delay(200);
}
