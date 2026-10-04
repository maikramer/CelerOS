// modulo que requer modulo (nota -> audio) e troca module.exports inteiro
var notas = require("notas");
module.exports = {
    beep: function () { return "beep p/" + notas.tocar("x"); }
};
