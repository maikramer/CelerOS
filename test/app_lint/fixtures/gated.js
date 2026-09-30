// Fixture: uso de API gated (permissao/nivel/feature) — os avisos dependem do
// app.json simulado passado pelo runner (api 2, permissions ["fs"]).
FS.listDir("/local");            // ok: fs concedida
System.gpio.pinMode(2, System.gpio.OUTPUT);  // exige permissao gpio
System.gpio.servo(4, 90);        // exige API 10, app declara 2
System.factoryReset(0);          // exige permissao system
CelerLink.send("x");             // opcional: exige feature-detect
