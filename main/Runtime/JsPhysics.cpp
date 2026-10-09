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
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include "esp_heap_caps.h"

namespace {

constexpr int kMaxWorlds = 4;
constexpr int kMaxPoints = 256;
constexpr int kMaxSticks = 640;
// bitmask de pares vinculados (a*256+b): a colisao ponto-ponto nao separa
// quem ja tem um stick controlando a distancia — 8KB por mundo
constexpr int kLinkBytes = kMaxPoints * kMaxPoints / 8;

struct VerletPoint { float x, y, px, py; bool pin; };
struct VerletStick { int16_t a, b; float len; };

struct VWorld {
    int nPts, nSticks, iterations;
    float radius;        // > 0: colisao ponto-ponto com este raio
    uint8_t* linked;     // kLinkBytes (dentro do malloc do mundo)
    VerletPoint pts[kMaxPoints];
    VerletStick sticks[kMaxSticks];
};

VWorld* s_worlds[kMaxWorlds] = { nullptr, nullptr, nullptr, nullptr };

VWorld* worldAt(int id) {
    if (id < 1 || id > kMaxWorlds) return nullptr;
    return s_worlds[id - 1];
}

inline void linkSet(VWorld* w, int a, int b) {
    int idx = a * kMaxPoints + b;
    w->linked[idx >> 3] |= (uint8_t)(1u << (idx & 7));
}

inline bool linked(VWorld* w, int a, int b) {
    int idx = a * kMaxPoints + b;
    return (w->linked[idx >> 3] >> (idx & 7)) & 1;
}

// recomputa a bitmask apos delPoint (indices mudaram) — O(sticks), raro
void relinkAll(VWorld* w) {
    memset(w->linked, 0, kLinkBytes);
    for (int i = 0; i < w->nSticks; i++) {
        linkSet(w, w->sticks[i].a, w->sticks[i].b);
        linkSet(w, w->sticks[i].b, w->sticks[i].a);
    }
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
    // colisao ponto-ponto (radius > 0): pares NAO vinculados mais perto que
    // 2r separam metade cada (prensados nao mexem). O(n^2) em float — no
    // interpretado isso nao caberia; aqui sao ~1ms para 120 pontos
    if (w->radius > 0.0f) {
        const float rr = w->radius * 2.0f, rr2 = rr * rr;
        for (int i = 0; i < w->nPts; i++) {
            VerletPoint& A = w->pts[i];
            for (int j = i + 1; j < w->nPts; j++) {
                if (linked(w, i, j)) continue;
                VerletPoint& B = w->pts[j];
                float dx = B.x - A.x, dy = B.y - A.y;
                float d2 = dx * dx + dy * dy;
                if (d2 >= rr2 || d2 < 1e-9f) continue;
                float d = sqrtf(d2);
                float push = (rr - d) / d * 0.5f;
                float ox = dx * push, oy = dy * push;
                // px/py junto do x/y: a separacao e reposicionamento, nao
                // impulso — senao cada step injeta energia e os nos ejetam
                if (!A.pin) { A.x -= ox; A.y -= oy; A.px -= ox; A.py -= oy; }
                if (!B.pin) { B.x += ox; B.y += oy; B.px += ox; B.py += oy; }
            }
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
    // radius > 0 liga a colisao ponto-ponto (raio de cada no)
    float radius = duk_is_number(ctx, 1) ? (float)duk_get_number(ctx, 1) : 0.0f;
    if (radius < 0.0f) radius = 0.0f;
    for (int i = 0; i < kMaxWorlds; i++) {
        if (s_worlds[i]) continue;
        // PSRAM primeiro (os jogos de fisica sao as placas S3); sem ela o
        // mundo vive na RAM interna — ~18KB cabem no Physics Drop sem PSRAM
        void* mem = heap_caps_malloc(sizeof(VWorld) + kLinkBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!mem) mem = heap_caps_malloc(sizeof(VWorld) + kLinkBytes, MALLOC_CAP_8BIT);
        if (!mem) { duk_push_int(ctx, -1); return 1; }
        memset(mem, 0, sizeof(VWorld) + kLinkBytes);
        VWorld* w = (VWorld*)mem;
        w->iterations = iters;
        w->radius = radius;
        w->linked = (uint8_t*)mem + sizeof(VWorld);
        s_worlds[i] = w;
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
    linkSet(w, a, b);
    linkSet(w, b, a);
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

duk_ret_t JSBindings::js_verletDelStick(duk_context* ctx) {
    // swap-remove do vinculo i (o ultimo entra no lugar). O JS re-le
    // sticks() a cada corte, então índice só e' valido entre leituras —
    // para varios cortes, delete do MAIOR índice para o menor.
    VWorld* w = worldAt(duk_require_int(ctx, 0));
    int i = duk_require_int(ctx, 1);
    if (!w || i < 0 || i >= w->nSticks) { duk_push_boolean(ctx, 0); return 1; }
    w->sticks[i] = w->sticks[w->nSticks - 1];
    w->nSticks--;
    duk_push_boolean(ctx, 1);
    return 1;
}

duk_ret_t JSBindings::js_verletDelPoint(duk_context* ctx) {
    // remove o ponto idx: vinculos ligados a ele saem, o ULTIMO ponto entra
    // no lugar (indices > idx descem 1 — chame do maior para o menor) e a
    // bitmask de pares e' recompute em O(sticks).
    VWorld* w = worldAt(duk_require_int(ctx, 0));
    int idx = duk_require_int(ctx, 1);
    if (!w || idx < 0 || idx >= w->nPts) { duk_push_boolean(ctx, 0); return 1; }
    int last = w->nPts - 1;
    int j = 0;
    for (int i = 0; i < w->nSticks; i++) {
        const VerletStick& s = w->sticks[i];
        if (s.a == idx || s.b == idx) continue;
        VerletStick& keep = w->sticks[j++];
        keep = s;
        if (s.a == last) keep.a = (int16_t)idx;
        if (s.b == last) keep.b = (int16_t)idx;
    }
    w->nSticks = j;
    if (idx != last) w->pts[idx] = w->pts[last];
    w->nPts = last;
    relinkAll(w);
    duk_push_boolean(ctx, 1);
    return 1;
}

duk_ret_t JSBindings::js_verletPins(duk_context* ctx) {
    // estado dos pinos por ponto (0/1) — para destacar e alternar no app
    VWorld* w = worldAt(duk_require_int(ctx, 0));
    int n = w ? w->nPts : 0;
    duk_push_array(ctx);
    for (int i = 0; i < n; i++) {
        duk_push_int(ctx, w->pts[i].pin ? 1 : 0);
        duk_put_prop_index(ctx, -2, (duk_uarridx_t)i);
    }
    return 1;
}
