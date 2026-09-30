// Fixture: aridades erradas.
System.delay(10, 20);       // 2 > nargs 1: extras descartados no aparelho
System.millis(1);           // 1 > nargs 0
System.drawRect(1, 2, 3, 4, 5, 6);  // 6 > nargs 5
System.gpio.servo(4);       // 1 < min 2: TypeError no aparelho
System.fillText("x", 1, 2); // nem existe: fillRect x drawString
