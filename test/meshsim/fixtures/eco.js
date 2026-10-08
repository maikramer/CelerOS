// Fixture da suite da malha: escreve tudo que chega pelo CelerNet.poll.
while (true) {
    var m;
    while ((m = CelerNet.poll()) !== null) {
        System.drawString("rx " + m.msg + " de " + m.fromName + " h" + m.hops + (m.unicast ? " uni" : ""), 0, 0);
    }
    System.delay(33);
}
