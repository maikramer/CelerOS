// conta quantas vezes foi carregado: o cache tem que manter em 1
var loads = 1;
exports.dobra = function (n) { return n * 2; };
exports.loads = function () { return loads; };
