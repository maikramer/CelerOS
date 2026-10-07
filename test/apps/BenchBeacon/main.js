// HandoffNow — cirurgico: toca 6 s e passa a festa pro quadro (4848).
function N(s) { System.notify("hop", s); }
if (typeof CelerNet === "undefined" || typeof Pack === "undefined") {
    N("sem BT");
    System.exitApp();
}
if (!CelerNet.status().active) CelerNet.start({ name: "Bench-dog" });
System.keepAwake(90000);
var song = { bpm: 132, loops: 16, tracks: [
    { wave: "sq",  vol: 80, notes: [[64,2],[67,2],[71,2],[72,2],[71,2],[67,2],[64,4],[0,2],[64,2],[69,2],[71,4],[67,4]] },
    { wave: "tri", vol: 70, notes: [[40,4],[47,4],[40,4],[47,4],[45,4],[40,4],[43,4],[47,4]] },
    { drum: true, vol: 90, notes: [[36,4],[42,2],[42,2],[38,4],[42,2],[42,2]] }
]};
N("playMusic " + System.playMusic(song));
var t0 = System.millis();
while (System.millis() - t0 < 6000) {
    System.delay(200);
    var f = UI.begin();
    if (UI.header("Festa", { back: true })) System.exitApp();
    UI.text("tocando " + System.musicPos() + " ms", 10, 60);
    UI.end();
}
var pos = System.musicPos();
N("pos " + pos + " handoff " + Pack.handoffMusic("Celer-AB3E"));
var t1 = System.millis();
while (System.millis() - t1 < 10000) {
    System.delay(300);
    var f2 = UI.begin();
    if (UI.header("Festa", { back: true })) System.exitApp();
    UI.text("local: " + (System.musicPlaying() ? "tocando" : "parado"), 10, 60);
    UI.end();
}
N("fim local=" + System.musicPlaying());
System.exitApp();
