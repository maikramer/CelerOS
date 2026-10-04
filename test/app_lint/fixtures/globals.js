// Fixture: globals que nao existem no Duktape lean / Node-isms.
// (require NAO esta aqui: e global do firmware desde a API 23)
var p = Promise.resolve(1);
console.log("oi");
var b = new Uint8Array(4);
process.exit(1);
var m = new Map();
System.print(Symbol());
