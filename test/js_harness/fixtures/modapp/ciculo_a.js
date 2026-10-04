// ciclo: b e requerido ANTES de a exportar — b so ve o exports parcial
exports.meio = "A";
var b = require("ciculo_b");
exports.valorDeB = function () { return b.doA === "A" ? "parcial-ok" : "quebrado"; };
