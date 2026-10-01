// Physics Drop - Verlet Integration Sandbox
var SW = System.screenWidth();
var SH = System.screenHeight();

var T = System.theme();
var BLACK = T.bg;
var WHITE = T.text;
var RED = T.err;
var BLUE = T.accent;

// gravidade em presets (cicla no botao): normal, leve, lua, forte, invertida
var GRAVS = [[0.4, "normal"], [0.2, "leve"], [0.07, "lua"], [0.9, "forte"], [-0.3, "invertida"]];
var gravIdx = 0;

var points = [];
var sticks = [];

var MAX_POINTS = 150;

var friction = 0.999;
var bounce = 0.8;

var isDrawing = false;
var lastPointIdx = -1;

function addPoint(x, y) {
    if (points.length >= MAX_POINTS) return -1;
    var p = {
        x: x,
        y: y,
        oldx: x,
        oldy: y,
        pinned: false
    };
    points.push(p);
    return points.length - 1;
}

function spawnRope() {
    // corda pendurada no topo, com no inicial fixo
    var n = 14;
    var step = 14;
    var x0 = 40 + Math.random() * (SW - 80);
    var prev = -1;
    for (var i = 0; i < n; i++) {
        var idx = addPoint(x0, 24 + i * step);
        if (idx < 0) break;
        if (i === 0) points[idx].pinned = true;
        if (prev >= 0) addStick(prev, idx, step);
        prev = idx;
    }
}

function spawnCloth() {
    // pano 8x6 pendurado pelos tres pontos de cima
    var cols = 8, rows = 6, step = 16;
    var x0 = (SW - cols * step) / 2;
    for (var r = 0; r < rows; r++) {
        for (var c = 0; c < cols; c++) {
            var idx = addPoint(x0 + c * step, 30 + r * step);
            if (idx < 0) return;
            if (r === 0 && (c === 0 || c === Math.floor(cols / 2) || c === cols - 1)) {
                points[idx].pinned = true;
            }
            if (c > 0) addStick(idx - 1, idx, step);
            if (r > 0) addStick(idx - cols, idx, step);
        }
    }
}

function addStick(p0_idx, p1_idx, length) {
    var s = {
        p0: p0_idx,
        p1: p1_idx,
        length: length
    };
    sticks.push(s);
}

function getDistance(p0, p1) {
    var dx = p1.x - p0.x;
    var dy = p1.y - p0.y;
    return Math.sqrt(dx*dx + dy*dy);
}

function updatePoints() {
    for (var i = 0; i < points.length; i++) {
        var p = points[i];
        if (!p.pinned) {
            var vx = (p.x - p.oldx) * friction;
            var vy = (p.y - p.oldy) * friction;
            
            p.oldx = p.x;
            p.oldy = p.y;
            
            p.x += vx;
            p.y += vy;
            p.y += GRAVS[gravIdx][0];
        }
    }
}

function updateSticks() {
    for (var i = 0; i < sticks.length; i++) {
        var s = sticks[i];
        var p0 = points[s.p0];
        var p1 = points[s.p1];
        
        var dx = p1.x - p0.x;
        var dy = p1.y - p0.y;
        var dist = Math.sqrt(dx*dx + dy*dy);
        if (dist === 0) continue; 
        
        var diff = s.length - dist;
        var percent = (diff / dist) / 2;
        var offsetX = dx * percent;
        var offsetY = dy * percent;
        
        if (!p0.pinned) {
            p0.x -= offsetX;
            p0.y -= offsetY;
        }
        if (!p1.pinned) {
            p1.x += offsetX;
            p1.y += offsetY;
        }
    }
}

function constrainPoints() {
    for (var i = 0; i < points.length; i++) {
        var p = points[i];
        if (p.pinned) continue;
        
        var vx = (p.x - p.oldx) * friction;
        var vy = (p.y - p.oldy) * friction;
        
        if (p.x < 0) {
            p.x = 0;
            p.oldx = p.x + vx * bounce;
        } else if (p.x > SW) {
            p.x = SW;
            p.oldx = p.x + vx * bounce;
        }
        
        if (p.y < 0) {
            p.y = 0;
            p.oldy = p.y + vy * bounce;
        } else if (p.y > SH) {
            p.y = SH;
            p.oldy = p.y + vy * bounce;
        }
    }
}

var SLICE_HEIGHT = 40;
var useSprite = System.createSprite(SW, SLICE_HEIGHT);

function drawScene() {
    if (useSprite) {
        for (var slice = 0; slice < 8; slice++) {
            var startY = slice * SLICE_HEIGHT;
            System.bindSprite(true);
            System.fillScreen(BLACK);
            
            // Draw sticks
            for (var i = 0; i < sticks.length; i++) {
                var s = sticks[i];
                var p0 = points[s.p0];
                var p1 = points[s.p1];
                System.drawLine(Math.floor(p0.x), Math.floor(p0.y) - startY, Math.floor(p1.x), Math.floor(p1.y) - startY, WHITE);
            }
            
            // Draw points
            for (var j = 0; j < points.length; j++) {
                var p = points[j];
                System.fillRect(Math.floor(p.x) - 1, Math.floor(p.y) - 1 - startY, 3, 3, BLUE);
            }
            
            // Draw instructions
            if (points.length === 0) {
                System.setTextColor(WHITE, BLACK);
                System.drawString("desenhe linhas que caem!", 20, 160 - startY, 2);
            } else {
                System.setTextColor(WHITE, BLACK);
                System.drawString("pontos " + points.length + "/" + MAX_POINTS + "  g: " + GRAVS[gravIdx][1], 64, 10 - startY, 1);
            }
            
            // Draw clear button on top left
            System.fillRect(0, 0 - startY, 58, 30, RED);
            System.setTextColor(WHITE, RED);
            System.drawString("LIMPAR", 8, 10 - startY, 1);
            System.fillRect(62, 0 - startY, 58, 30, BLUE);
            System.setTextColor(WHITE, BLUE);
            System.drawString("CORDA", 72, 10 - startY, 1);
            System.fillRect(124, 0 - startY, 58, 30, WHITE);
            System.setTextColor(BLACK, WHITE);
            System.drawString("PANO", 136, 10 - startY, 1);
            System.fillRect(186, 0 - startY, 54, 30, T.warn);
            System.setTextColor(BLACK, T.warn);
            System.drawString("G: " + (gravIdx + 1) + "/" + GRAVS.length, 194, 10 - startY, 1);
            
            System.bindSprite(false);
            System.pushSprite(0, startY);
        }
    } else {
        // Fallback to direct rendering if sprite allocation failed
        System.fillScreen(BLACK);
        
        for (var i = 0; i < sticks.length; i++) {
            var s = sticks[i];
            var p0 = points[s.p0];
            var p1 = points[s.p1];
            System.drawLine(Math.floor(p0.x), Math.floor(p0.y), Math.floor(p1.x), Math.floor(p1.y), WHITE);
        }
        
        for (var j = 0; j < points.length; j++) {
            var p = points[j];
            System.fillRect(Math.floor(p.x) - 1, Math.floor(p.y) - 1, 3, 3, BLUE);
        }
        
        if (points.length === 0) {
            System.setTextColor(WHITE, BLACK);
            System.drawString("desenhe linhas que caem!", 20, 160, 2);
        } else {
            System.setTextColor(WHITE, BLACK);
            System.drawString("pontos " + points.length + "/" + MAX_POINTS + "  g: " + GRAVS[gravIdx][1], 64, 10, 1);
        }
        
        System.fillRect(0, 0, 58, 30, RED);
        System.setTextColor(WHITE, RED);
        System.drawString("LIMPAR", 8, 10, 1);
        System.fillRect(62, 0, 58, 30, BLUE);
        System.setTextColor(WHITE, BLUE);
        System.drawString("CORDA", 72, 10, 1);
        System.fillRect(124, 0, 58, 30, WHITE);
        System.setTextColor(BLACK, WHITE);
        System.drawString("PANO", 136, 10, 1);
        System.fillRect(186, 0, 54, 30, T.warn);
        System.setTextColor(BLACK, T.warn);
        System.drawString("G: " + (gravIdx + 1) + "/" + GRAVS.length, 194, 10, 1);
    }
}

// Initial draw
drawScene();

// Main Game Loop
while (true) {
    var t = System.getTouch();
    
    if (t.touched) {
        // Check for clear button (top-left)
        if (t.y < 30) {
            if (t.x < 58) {                 // LIMPAR
                points = [];
                sticks = [];
                isDrawing = false;
                lastPointIdx = -1;
            } else if (t.x < 120) {         // CORDA
                spawnRope();
            } else if (t.x < 182) {         // PANO
                spawnCloth();
            } else if (t.x >= 186) {        // G: cicla gravidade
                gravIdx = (gravIdx + 1) % GRAVS.length;
            }
            drawScene();
            System.delay(300); // debounce
        } else if (t.x < 200 || t.y > 40) { // Don't draw if touching OS close area
            if (!isDrawing) {
                isDrawing = true;
                lastPointIdx = addPoint(t.x, t.y);
                if (lastPointIdx !== -1) {
                    points[lastPointIdx].pinned = true; // Pin while drawing
                }
            } else {
                // If moved far enough from last point, add another point
                if (lastPointIdx !== -1) {
                    var lp = points[lastPointIdx];
                    var dist = getDistance({x: t.x, y: t.y}, lp);
                    if (dist > 15) { // spawn segment every 15 pixels to save memory
                        var newIdx = addPoint(t.x, t.y);
                        if (newIdx !== -1) {
                            addStick(lastPointIdx, newIdx, dist);
                            points[newIdx].pinned = true;
                            lp.pinned = false; // release previous point
                            lastPointIdx = newIdx;
                        }
                    } else {
                        // Just update the pinned point to follow finger
                        lp.x = t.x;
                        lp.y = t.y;
                    }
                }
            }
        }
    } else {
        if (isDrawing) {
            isDrawing = false;
            if (lastPointIdx !== -1) {
                points[lastPointIdx].pinned = false; // release the line
                lastPointIdx = -1;
            }
        }
    }
    
    updatePoints();
    // Relax sticks by doing multiple iterations for stiffness
    for (var iter = 0; iter < 4; iter++) {
        updateSticks();
        constrainPoints();
    }
    
    drawScene();
    
    System.delay(10); // GC and yield
}
