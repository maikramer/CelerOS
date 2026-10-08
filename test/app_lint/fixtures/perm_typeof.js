// Fixture: chamada com permissao que o app FAZ feature-detect (typeof do
// mesmo membro) nao exige a permissao — a ausencia ja e tratada. O resto
// segue exigindo: System.factoryReset (system) sem typeof gera aviso.
function led(r, g, b) { if (typeof System.led === "function") System.led(r, g, b); }
led(255, 0, 0);
System.factoryReset(0);
