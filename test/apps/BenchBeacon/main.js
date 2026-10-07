// BenchBeacon v3 — desempate frag x unicast: broadcasts GRANDES
// (fragmentados) e unicast minimo real (12 bytes = 1 DATA inteiro).
function N(s) { System.notify("beacon", s); }
if (typeof CelerNet === "undefined" || typeof Pack === "undefined") {
    N("sem BT");
    System.exitApp();
}
if (!CelerNet.status().active) CelerNet.start({ name: "Bench-dog" });
System.keepAwake(120000);
var stB = 0, stU = 0;
for (var i = 0; i < 4; i++) {
    if (CelerNet.broadcast("GRANDE-" + i + "-abcdefghijklmnopqrstuvwxyz-0123456789-ABCDEFG")) stB++;
    System.delay(3000);
}
N("grandes (frag) enviados " + stB + "/4");
for (var j = 0; j < 4; j++) {
    if (Pack.send("Bench-watch", { p: j })) stU++;
    System.delay(3000);
}
var st = CelerNet.status();
N("unicasts 12B enviados " + stU + "/4 (noAr " + st.txStarted + " drop " +
  st.txDropped + ")");
System.exitApp();
