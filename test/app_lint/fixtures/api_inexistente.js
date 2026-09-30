// Fixture: chamadas a funcoes/constantes que nao existem na API.
System.fillScreen(T.bg);
System.drawPixl(1, 2);            // typo: drawPixel
Net.postX("http://x", {});        // nao existe em Net
System.gpio.digitalWrit(1, 1);    // typo: digitalWrite
var c = MAGNETA;                  // typo: MAGENTA
System.keypaddPoll();             // typo: keypadPoll
