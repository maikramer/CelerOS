// CelerOS Cubo 3D — renderizador 3D em JavaScript.
// Cubo solido (algoritmo do pintor) ou em arestas, rotacao por arrasto,
// giro automatico e contador de FPS. Render por fatias em sprite (RAM 16-bit).

var T = System.theme();
var SW = System.screenWidth();
var SH = System.screenHeight();

var STATE_MENU = 0;
var STATE_PLAYING = 1;
var state = STATE_MENU;
var wireframe = false;        // false = faces solidas | true = so arestas

// vertices do cubo (x, y, z)
var vertices = [
    [-1, -1, -1], [1, -1, -1], [1, 1, -1], [-1, 1, -1],
    [-1, -1, 1], [1, -1, 1], [1, 1, 1], [-1, 1, 1]
];

// faces com cores proprias (igual ao benchmark)
var faces = [
    { indices: [0, 1, 2, 3], color: RED },     // frente
    { indices: [5, 4, 7, 6], color: GREEN },   // tras
    { indices: [4, 5, 1, 0], color: BLUE },    // topo
    { indices: [3, 2, 6, 7], color: YELLOW },  // base
    { indices: [4, 0, 3, 7], color: MAGENTA }, // esquerda
    { indices: [1, 5, 6, 2], color: CYAN }     // direita
];

// arestas (indices de vertices) para o modo wireframe
var edges = [
    0, 1, 1, 2, 2, 3, 3, 0,
    4, 5, 5, 6, 6, 7, 7, 4,
    0, 4, 1, 5, 2, 6, 3, 7
];

var angleX = 0;
var angleY = 0;

var lastTouchX = 0;
var lastTouchY = 0;
var isDragging = false;

// FPS: conta quadros numa janela de 500 ms
var frames = 0;
var fpsWin = System.millis();
var fps = 0;

function ctext(s, cx, y, f, col, bg) {
    System.setTextColor(col, bg || T.bg);
    System.drawString(s, cx - (System.textWidth(s, f) >> 1), y, f);
}

function drawMenu() {
    System.fillScreen(T.bg);

    System.drawRect(10, 10, SW - 20, SH - 20, T.accent);
    System.drawRect(12, 12, SW - 24, SH - 24, T.stroke);

    ctext("Cubo 3D", SW / 2, 56, 4, T.text);
    ctext("renderizador interativo", SW / 2, 100, 2, T.textDim);

    System.fillRoundRect(45, 132, 150, 34, 8, T.card);
    System.drawRoundRect(45, 132, 150, 34, 8, T.stroke);
    ctext(wireframe ? "modo: arestas" : "modo: solido", SW / 2, 141, 1, T.accent, T.card);

    System.fillRoundRect(50, 180, 140, 50, 10, T.accent);
    ctext("GIRAR", SW / 2, 196, 4, T.onAccent, T.accent);

    ctext("arraste o dedo para girar o cubo", SW / 2, 266, 1, T.textDim);
}

function draw3DFrame() {
    var sinX = Math.sin(angleX); var cosX = Math.cos(angleX);
    var sinY = Math.sin(angleY); var cosY = Math.cos(angleY);

    // transforma e projeta os vertices uma vez por quadro
    var proj = [];
    for (var i = 0; i < 8; i++) {
        var x = vertices[i][0];
        var y = vertices[i][1];
        var z = vertices[i][2];

        var x1 = x * cosY - z * sinY;      // rota em Y
        var z1 = x * sinY + z * cosY;
        var y2 = y * cosX - z1 * sinX;     // rota em X
        var z2 = y * sinX + z1 * cosX;

        var fov = 150;
        var dist = 3.5;
        var zDist = z2 + dist;
        if (zDist < 0.1) zDist = 0.1;

        proj.push({
            px: Math.floor(SW / 2 + (x1 * fov) / zDist),
            py: Math.floor(SH / 2 + (y2 * fov) / zDist),
            pz: zDist
        });
    }

    var sliceH = 32;   // 10 fatias de 32 px (cabem na RAM de 16 bits)
    var numSlices = Math.ceil(SH / sliceH);

    System.createSprite(SW, sliceH);
    for (var slice = 0; slice < numSlices; slice++) {
        var sliceY = slice * sliceH;
        System.bindSprite(true);
        System.fillScreen(T.bg);

        if (slice === 0) {
            System.fillRect(0, 0, SW, 30, T.card);
            System.fillRect(0, 30, SW, 2, T.accent);
            System.setTextColor(T.accent, T.card);
            System.drawString("Cubo 3D", 10, 8, 2);
            System.setTextColor(T.textDim, T.card);
            var st = (wireframe ? "arestas" : "solido") + "  " + fps + " fps";
            System.drawString(st, 230 - System.textWidth(st, 1), 10, 1);
        }

        if (wireframe) {
            for (var e = 0; e < edges.length; e += 2) {
                var a = proj[edges[e]];
                var b = proj[edges[e + 1]];
                if ((a.py < sliceY && b.py < sliceY) ||
                    (a.py >= sliceY + sliceH && b.py >= sliceY + sliceH)) continue;
                System.drawLine(a.px, a.py - sliceY, b.px, b.py - sliceY, T.accent);
            }
        } else {
            // faces ordenadas por profundidade (algoritmo do pintor)
            var pf = [];
            for (var fi = 0; fi < faces.length; fi++) {
                var f = faces[fi];
                var p0 = proj[f.indices[0]];
                var p1 = proj[f.indices[1]];
                var p2 = proj[f.indices[2]];
                var p3 = proj[f.indices[3]];
                pf.push({ p0: p0, p1: p1, p2: p2, p3: p3, color: f.color,
                          z: (p0.pz + p1.pz + p2.pz + p3.pz) / 4.0 });
            }
            pf.sort(function (a, b) { return b.z - a.z; });

            for (var i2 = 0; i2 < pf.length; i2++) {
                var g = pf[i2];
                var minY = Math.min(g.p0.py, g.p1.py, g.p2.py, g.p3.py);
                var maxY = Math.max(g.p0.py, g.p1.py, g.p2.py, g.p3.py);
                if (maxY >= sliceY && minY < sliceY + sliceH) {
                    System.fillTriangle(g.p0.px, g.p0.py - sliceY, g.p1.px, g.p1.py - sliceY,
                                        g.p2.px, g.p2.py - sliceY, g.color);
                    System.fillTriangle(g.p0.px, g.p0.py - sliceY, g.p2.px, g.p2.py - sliceY,
                                        g.p3.px, g.p3.py - sliceY, g.color);
                }
            }
        }

        System.bindSprite(false);
        System.pushSprite(0, sliceY);
    }
    System.deleteSprite();

    frames++;
    var now = System.millis();
    if (now - fpsWin >= 500) {
        fps = Math.round(frames * 1000 / (now - fpsWin));
        frames = 0;
        fpsWin = now;
    }
}

drawMenu();

while (true) {
    var t = System.getTouch();

    if (t.touched && t.x < 200) {          // X da faixa fecha sozinho
        if (state === STATE_MENU) {
            if (t.y >= 132 && t.y <= 166 && t.x >= 45 && t.x <= 195) {
                wireframe = !wireframe;
                drawMenu();
                System.delay(250);
            } else if (t.y >= 180 && t.y <= 230 && t.x >= 50 && t.x <= 190) {
                state = STATE_PLAYING;
                System.delay(200);
            }
        } else if (state === STATE_PLAYING) {
            if (!isDragging) {
                isDragging = true;
                lastTouchX = t.x;
                lastTouchY = t.y;
            } else {
                var dx = t.x - lastTouchX;
                var dy = t.y - lastTouchY;
                if (Math.abs(dx) < 80 && Math.abs(dy) < 80) {
                    angleY += dx * 0.02;
                    angleX += dy * 0.02;
                }
                lastTouchX = t.x;
                lastTouchY = t.y;
            }
        }
    } else if (t.y < 30 && state === STATE_PLAYING) {
        // toque no titulo: volta ao menu
        state = STATE_MENU;
        isDragging = false;
        drawMenu();
    } else {
        isDragging = false;
    }

    if (state === STATE_PLAYING) {
        if (!isDragging) {                 // gira sozinho sem arrasto
            angleX += 0.04;
            angleY += 0.03;
        }
        var PI2 = Math.PI * 2;             // normaliza (precisao do float)
        angleX = ((angleX % PI2) + PI2) % PI2;
        angleY = ((angleY % PI2) + PI2) % PI2;

        draw3DFrame();
    }

    System.delay(10);
}
