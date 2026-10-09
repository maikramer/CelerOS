// Verlet nativo (API 31): o step pesado do P.verlet — integracao,
// relaxacao de vinculos e bounds com bounce — em C++ float (FPU single do
// S3; double e soft-fp e custa 5-10x). O JS cria/pinta por indice: os
// pontos NAO vivem no heap Duktape, o mundo inteiro e um buffer nativo
// malloc'ado sob demanda (PSRAM nas placas que tem, RAM interna senao).
//
// Por que so o verlet e nativo: e dado homogeneu sem callbacks — o P.world
// dispara onCollide por par e cada volta ao JS comeria o ganho da
// fronteira. O caminho JS da dep continua valendo (feature-detect): a
// celeros.physics expoe P.verletFast, que cai para o P.verlet interpretado
// em firmware sem este binding.
//
// Estado por app: os mundos saem no JSBindings::init do proximo app
// (jsPhysicsReset) — o app nunca ve mundo de outro, e o malloc nao vaza
// entre execucoes. Sem finalizer: um app usa 1-2 mundos pela vida inteira.

#include "JsInternal.h"
#include "../Kernel/Core/CelerKernel.h"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include "esp_heap_caps.h"

namespace {

constexpr int kMaxWorlds = 4;
constexpr int kMaxPoints = 256;
constexpr int kMaxSticks = 640;

struct VerletPoint { float x, y, px, py; bool pin; };
struct VerletStick { int16_t a, b; float len; };

struct VWorld {
    int nPts, nSticks, iterations;
    VerletPoint pts[kMaxPoints];
    VerletStick sticks[kMaxSticks];
};

VWorld* s_worlds[kMaxWorlds] = { nullptr, nullptr, nullptr, nullptr };

VWorld* worldAt(int id) {
    if (id < 1 || id > kMaxWorlds) return nullptr;
    return s_worlds[id - 1];
}

void stepWorld(VWorld* w, float dt, float gx, float gy, float damp,
               float minx, float miny, float maxx, float maxy, float bounce) {
    // mesma matematica do P.verlet (celeros.physics.js), em float
    const float dt2 = dt * dt;
    for (int i = 0; i < w->nPts; i++) {
        VerletPoint& p = w->pts[i];
        if (p.pin) { p.px = p.x; p.py = p.y; continue; }
        float vx = (p.x - p.px) * damp, vy = (p.y - p.py) * damp;
        p.px = p.x; p.py = p.y;
        p.x += vx + gx * dt2;
        p.y += vy + gy * dt2;
    }
    for (int k = 0; k < w->iterations; k++) {
        for (int i = 0; i < w->nSticks; i++) {
            const VerletStick& s = w->sticks[i];
            VerletPoint& A = w->pts[s.a];
            VerletPoint& B = w->pts[s.b];
            float dx = B.x - A.x, dy = B.y - A.y;
            float d = sqrtf(dx * dx + dy * dy);
            if (d < 1e-6f) continue;
            float ma = A.pin ? 0.0f : 1.0f, mb = B.pin ? 0.0f : 1.0f;
            float tot = ma + mb;
            if (tot == 0.0f) continue;
            float f = (d - s.len) / d / tot;
            A.x += dx * f * ma;
            A.y += dy * f * ma;
            B.x -= dx * f * mb;
            B.y -= dy * f * mb;
        }
    }
    // bounds opcionais (maxx > minx ativa); mesmo esquema de rebote do JS
    if (maxx > minx) {
        for (int i = 0; i < w->nPts; i++) {
            VerletPoint& p = w->pts[i];
            if (p.pin) continue;
            if (p.x < minx) { float vx = p.x - p.px; p.x = minx; p.px = p.x + vx * bounce; }
            if (p.x > maxx) { float vx = p.x - p.px; p.x = maxx; p.px = p.x + vx * bounce; }
            if (p.y < miny) { float vy = p.y - p.py; p.y = miny; p.py = p.y + vy * bounce; }
            if (p.y > maxy) { float vy = p.y - p.py; p.y = maxy; p.py = p.y + vy * bounce; }
        }
    }
}

}  // namespace

void jsPhysicsReset() {
    // JSBindings::init do proximo app: mundos do anterior saem (malloc
    // incluso). Roda tambem no boot — s_worlds ja nasce zero.
    for (int i = 0; i < kMaxWorlds; i++) {
        if (s_worlds[i]) {
            free(s_worlds[i]);
            s_worlds[i] = nullptr;
        }
    }
}

duk_ret_t JSBindings::js_verletNew(duk_context* ctx) {
    int iters = duk_is_number(ctx, 0) ? duk_get_int(ctx, 0) : 4;
    if (iters < 1) iters = 1;
    if (iters > 16) iters = 16;
    for (int i = 0; i < kMaxWorlds; i++) {
        if (s_worlds[i]) continue;
        // PSRAM primeiro (os jogos de fisica sao as placas S3); sem ela o
        // mundo vive na RAM interna — 10KB cabem no Physics Drop sem PSRAM
        void* mem = heap_caps_malloc(sizeof(VWorld), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!mem) mem = heap_caps_malloc(sizeof(VWorld), MALLOC_CAP_8BIT);
        if (!mem) { duk_push_int(ctx, -1); return 1; }
        memset(mem, 0, sizeof(VWorld));
        ((VWorld*)mem)->iterations = iters;
        s_worlds[i] = (VWorld*)mem;
        duk_push_int(ctx, i + 1);
        return 1;
    }
    duk_push_int(ctx, -1);
    return 1;
}

duk_ret_t JSBindings::js_verletFree(duk_context* ctx) {
    VWorld* w = worldAt(duk_require_int(ctx, 0));
    if (!w) return 0;
    for (int i = 0; i < kMaxWorlds; i++) {
        if (s_worlds[i] == w) s_worlds[i] = nullptr;
    }
    free(w);
    return 0;
}

duk_ret_t JSBindings::js_verletAddPoint(duk_context* ctx) {
    VWorld* w = worldAt(duk_require_int(ctx, 0));
    float x = (float)duk_require_number(ctx, 1);
    float y = (float)duk_require_number(ctx, 2);
    if (!w || w->nPts >= kMaxPoints) { duk_push_int(ctx, -1); return 1; }
    VerletPoint& p = w->pts[w->nPts];
    p.x = p.px = x;
    p.y = p.py = y;
    p.pin = false;
    duk_push_int(ctx, w->nPts++);
    return 1;
}

duk_ret_t JSBindings::js_verletStick(duk_context* ctx) {
    VWorld* w = worldAt(duk_require_int(ctx, 0));
    int a = duk_require_int(ctx, 1);
    int b = duk_require_int(ctx, 2);
    if (!w || a < 0 || b < 0 || a >= w->nPts || b >= w->nPts || w->nSticks >= kMaxSticks) {
        duk_push_boolean(ctx, 0);
        return 1;
    }
    VerletStick& s = w->sticks[w->nSticks++];
    s.a = (int16_t)a;
    s.b = (int16_t)b;
    if (duk_is_number(ctx, 3)) {
        s.len = (float)duk_get_number(ctx, 3);
    } else {
        float dx = w->pts[b].x - w->pts[a].x, dy = w->pts[b].y - w->pts[a].y;
        s.len = sqrtf(dx * dx + dy * dy);
    }
    duk_push_boolean(ctx, 1);
    return 1;
}

duk_ret_t JSBindings::js_verletPin(duk_context* ctx) {
    VWorld* w = worldAt(duk_require_int(ctx, 0));
    int idx = duk_require_int(ctx, 1);
    bool on = !duk_is_boolean(ctx, 2) || duk_get_boolean(ctx, 2);
    if (!w || idx < 0 || idx >= w->nPts) { duk_push_boolean(ctx, 0); return 1; }
    w->pts[idx].pin = on;
    duk_push_boolean(ctx, 1);
    return 1;
}

duk_ret_t JSBindings::js_verletSet(duk_context* ctx) {
    // move um ponto (o dedo puxando): NAO toca em px/py — a velocidade
    // implicita nasce na diferenca, igual ao caminho JS
    VWorld* w = worldAt(duk_require_int(ctx, 0));
    int idx = duk_require_int(ctx, 1);
    float x = (float)duk_require_number(ctx, 2);
    float y = (float)duk_require_number(ctx, 3);
    if (!w || idx < 0 || idx >= w->nPts) { duk_push_boolean(ctx, 0); return 1; }
    w->pts[idx].x = x;
    w->pts[idx].y = y;
    duk_push_boolean(ctx, 1);
    return 1;
}

duk_ret_t JSBindings::js_verletStep(duk_context* ctx) {
    VWorld* w = worldAt(duk_require_int(ctx, 0));
    if (!w) { duk_push_boolean(ctx, 0); return 1; }
    float dt = (float)duk_require_number(ctx, 1);
    float gx = (float)duk_get_number(ctx, 2);
    float gy = (float)duk_get_number(ctx, 3);
    float damp = duk_is_number(ctx, 4) ? (float)duk_get_number(ctx, 4) : 1.0f;
    float minx = (float)duk_get_number(ctx, 5);
    float miny = (float)duk_get_number(ctx, 6);
    float maxx = (float)duk_get_number(ctx, 7);
    float maxy = (float)duk_get_number(ctx, 8);
    float bounce = duk_is_number(ctx, 9) ? (float)duk_get_number(ctx, 9) : 0.5f;
    CelerKernel::noteAppYield();  // step pesado renova o exec-timeout (padrao 4b5b9ce)
    stepWorld(w, dt, gx, gy, damp, minx, miny, maxx, maxy, bounce);
    duk_push_boolean(ctx, 1);
    return 1;
}

duk_ret_t JSBindings::js_verletXY(duk_context* ctx) {
    // array plano [x0, y0, x1, y1, ...] — UMA alocacao por frame; o desenho
    // do app itera sobre numeros puros (leitura de array JS e rapida)
    VWorld* w = worldAt(duk_require_int(ctx, 0));
    int n = w ? w->nPts : 0;
    duk_push_array(ctx);
    for (int i = 0; i < n; i++) {
        duk_push_number(ctx, w->pts[i].x);
        duk_put_prop_index(ctx, -2, (duk_uarridx_t)(i * 2));
        duk_push_number(ctx, w->pts[i].y);
        duk_put_prop_index(ctx, -2, (duk_uarridx_t)(i * 2 + 1));
    }
    return 1;
}

duk_ret_t JSBindings::js_verletSticks(duk_context* ctx) {
    // array plano [a0, b0, a1, b1, ...] — indices dos pontos de cada vinculo
    VWorld* w = worldAt(duk_require_int(ctx, 0));
    int n = w ? w->nSticks : 0;
    duk_push_array(ctx);
    for (int i = 0; i < n; i++) {
        duk_push_int(ctx, w->sticks[i].a);
        duk_put_prop_index(ctx, -2, (duk_uarridx_t)(i * 2));
        duk_push_int(ctx, w->sticks[i].b);
        duk_put_prop_index(ctx, -2, (duk_uarridx_t)(i * 2 + 1));
    }
    return 1;
}

duk_ret_t JSBindings::js_verletCount(duk_context* ctx) {
    VWorld* w = worldAt(duk_require_int(ctx, 0));
    duk_push_int(ctx, w ? w->nPts : 0);
    return 1;
}
