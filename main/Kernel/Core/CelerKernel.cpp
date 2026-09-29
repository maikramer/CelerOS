#include "CelerKernel.h"
#include "../../USBDevice/LogSink.h"
#include "../../Display/Layout.h"
#include "../../Runtime/JSBindings.h"
#include "../../FileSystem/FileSystem.h"
#include "../../Utils/StrUtils.h"
#include "../../Display/Theme.h"
#include "../../UI/Kui.h"
#include "../../Display/Icon.h"
#include "../../Hardware/BoardIO.h"
#include "../../Utils/JsStrip.h"
#include "../../Utils/I18n.h"
#include <vector>
#include "esp_heap_caps.h"
#include "esp_attr.h"
#include <cstring>
#include "esp_task_wdt.h"
#include "esp_debug_helpers.h"

duk_context *CelerKernel::ctx = nullptr;
CelerDisplay *CelerKernel::tftInstance = nullptr;
size_t CelerKernel::appLaunchFreeHeap = 0;

// ---------------------------------------------------------------------------
// Tela de erro do runtime (tema do OS): titulo, texto quebrado em linhas e
// "toque para voltar" — qualquer toque (fisico ou injetado) fecha. Antes era
// um fundo vermelho cru com um "X" que so respondia no canto superior.
// ---------------------------------------------------------------------------
static void wrapInto(CelerDisplay* tft, const std::string& text, const lgfx::IFont* f, int maxW,
                     std::vector<std::string>& out, size_t maxLines) {
    size_t start = 0;
    while (start <= text.size() && out.size() < maxLines) {
        size_t nl = text.find('\n', start);
        std::string para = text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        // quebra por largura (palavra a palavra; palavra gigante e cortada)
        std::string line;
        size_t i = 0;
        while (i <= para.size() && out.size() < maxLines) {
            size_t sp = para.find(' ', i);
            std::string word = para.substr(i, sp == std::string::npos ? std::string::npos : sp - i);
            std::string cand = line.empty() ? word : line + " " + word;
            if (!line.empty() && tft->textWidth(cand.c_str(), f) > maxW) {
                out.push_back(line);
                line = word;
            } else {
                line = cand;
            }
            while (tft->textWidth(line.c_str(), f) > maxW && line.size() > 1 && out.size() < maxLines) {
                size_t cut = line.size();
                while (cut > 1 && tft->textWidth(line.substr(0, cut).c_str(), f) > maxW) cut--;
                out.push_back(line.substr(0, cut));
                line = line.substr(cut);
            }
            if (sp == std::string::npos) break;
            i = sp + 1;
        }
        if (!line.empty() && out.size() < maxLines) out.push_back(line);
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
}

static void showRuntimeError(const char* title, const std::string& detail) {
    CelerDisplay* tft = CelerKernel::tftInstance;
    if (!tft) return;
    tft->clearClipRect();  // recorte do app (faixa/System.setClip) nao vale aqui
    const lgfx::IFont* body = kui::type::body();
    const lgfx::IFont* cap = kui::type::caption();

    tft->startWrite();
    tft->fillScreen(THEME_BG);
    int hdr = UI::sy(52);
    tft->fillRect(0, 0, UI::W, hdr, THEME_CARD);
    tft->fillRect(0, hdr - UI::sy(3), UI::W, UI::sy(3), THEME_ERR);
    tft->setTextDatum(ML_DATUM);
    tft->setTextColor(THEME_TEXT);
    tft->drawString(title, UI::sx(14), hdr / 2, kui::type::title());

    std::vector<std::string> lines;
    int lineH = tft->fontHeight(cap) + UI::sy(3);
    size_t maxLines = (size_t)((UI::H - hdr - UI::sy(70)) / lineH);
    wrapInto(tft, detail, cap, UI::W - UI::sx(28), lines, maxLines);
    tft->setTextDatum(TL_DATUM);
    tft->setTextColor(THEME_TEXT_DIM);
    int y = hdr + UI::sy(12);
    for (const auto& l : lines) {
        tft->drawString(l.c_str(), UI::sx(14), y, cap);
        y += lineH;
    }

    int bh = UI::sy(34);
    int by = UI::H - bh - UI::sy(12);
    tft->fillRoundRect(UI::sx(40), by, UI::W - UI::sx(80), bh, UI::sx(8), THEME_RAISED);
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(THEME_TEXT);
    tft->drawString(i18n::TR("Toque para voltar", "Touch to go back"), UI::cx(), by + bh / 2, body);
    tft->endWrite();

    // espera soltar (o toque que causou o erro), depois um tap completo —
    // usuario pode demorar: alimenta o watchdog da main task enquanto espera
    uint16_t tx, ty;
    while (kui::readTouch(&tx, &ty)) { esp_task_wdt_reset(); delay(20); }
    while (!kui::readTouch(&tx, &ty)) { esp_task_wdt_reset(); delay(20); }
    while (kui::readTouch(&tx, &ty)) { esp_task_wdt_reset(); delay(20); }
}

// dica de OOM no idioma configurado (primeiro uso trava o idioma)
static const char* oomHint() {
    return i18n::TR("O app precisa de mais memória do que esta placa tem livre. "
                    "Em placas sem PSRAM, prefira apps menores.",
                    "The app needs more memory than this board has free. "
                    "On boards without PSRAM, prefer smaller apps.");
}
#define kOomHint oomHint()

// ---------------------------------------------------------------------------
// Alocador do Duktape.
//
// O malloc do IDF manda todo bloco < 4 KB (SPIRAM_MALLOC_ALWAYSINTERNAL) para
// a RAM interna — e um script grande (o Settings compila ~1000 linhas) e
// quase so blocos pequenos: esgotava a interna e o WiFi abortava ao acordar
// do power-save (esp_timer_create -> ESP_ERR_NO_MEM em phy_track_pll_init).
// Com PSRAM: interna so enquanto sobrar folga para o sistema (WiFi/lwIP/
// timers); passou disso, PSRAM. Sem PSRAM (CYD): malloc padrao.
// ---------------------------------------------------------------------------
static constexpr size_t kInternalReserve = 72 * 1024;
// Sem PSRAM (CYD): o heap Duktape compete com WiFi/lwIP pela RAM interna
// (~70KB livres no boot). Sem piso, ele comia o heap ate o ultimo byte
// (heap_min de 48 BYTES medido no device) e o sistema abortava — agora
// nega alocacao abaixo do piso e o Duktape ve OOM (soft-error "Sem
// memoria"), nao um abort no meio da tela de erro.
static constexpr size_t kNoPsramFloor = 20 * 1024;

// Sem PSRAM (CYD): a DRAM 8-bit e do Duktape so enquanto sobrarem ~24KB
// para o sistema — o handshake TLS de um app com rede (verificacao do
// certificado, ~20KB de blocos pequenos) e o WiFi precisam dela. Passou
// disso, o Duktape TRANSBORDA para a IRAM acessivel a byte (unicore +
// CONFIG_ESP32_IRAM_AS_8BIT_ACCESSIBLE_MEMORY): mais lenta (acesso de 8/16
// bits vira excecao), mas o app segue em vez de OOM ou de sufocar o TLS.
static constexpr size_t kDramReserve = 24 * 1024;

// Destino preferido de um bloco e o alternativo (tentado se o primeiro falhar)
static void duk_caps(size_t size, uint32_t* first, uint32_t* second) {
    if (!Board::profile().hasPsram) {
        const bool dram = heap_caps_get_free_size(MALLOC_CAP_8BIT) > kDramReserve + size;
        *first = dram ? MALLOC_CAP_8BIT : MALLOC_CAP_IRAM_8BIT;
        *second = dram ? MALLOC_CAP_IRAM_8BIT : MALLOC_CAP_8BIT;
        return;
    }
    *second = MALLOC_CAP_8BIT;
    *first = heap_caps_get_free_size(MALLOC_CAP_INTERNAL) > kInternalReserve ? MALLOC_CAP_8BIT
                                                                             : MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
}

// Orçamento para ABRIR um app (piso só no create, nunca no run): sem PSRAM,
// exigir folga generosa — app que estourar vai de OOM fatal ao my_fatal, e
// o sistema recupera com a tela de erro.
// Heap base do Duktape + API inteira com builtins em ROM e bindings
// lightfunc (medido na CYD: ~7,6KB; eram ~50KB com builtins na RAM)
static constexpr size_t kDukBase = 8 * 1024;

// Heap que ainda falta com o fonte JA carregado: base + temporarios do
// compile (~0.75x o fonte enxuto) + folga do sistema
static size_t dukHeapNeed(size_t srcLen) {
    return kNoPsramFloor + kDukBase + srcLen * 3 / 4;
}

static bool dukHeapBudgetOk(size_t srcLen) {
    if (Board::profile().hasPsram) return true;
    // Recusa ANTES (mensagem limpa) quando claramente nao cabe — OOM no
    // meio do compile/run leva ao my_fatal (reinicio). Conta a IRAM de
    // transbordo (duk_caps) junto da DRAM.
    return heap_caps_get_free_size(MALLOC_CAP_8BIT) + heap_caps_get_free_size(MALLOC_CAP_IRAM_8BIT) >
           dukHeapNeed(srcLen);
}

// Le o main.js ENXUTO (sem comentarios/indentacao, linhas preservadas — ver
// Utils/JsStrip.h) num bloco exato: passada 1 conta, passada 2 preenche.
// Sem PSRAM o fonte inteiro precisa de um bloco contiguo durante o compile
// (App Store: 44KB -> ~31KB); o std::string de antes abortava (new sem
// excecao) quando o heap fragmentado nao tinha o bloco. nullptr = sem
// arquivo; *oom = true quando faltou memoria.
static char* loadAppSource(const char* path, size_t* lenOut, bool* oom) {
    *oom = false;
    FILE* f = fopen(path, "rb");
    if (f == nullptr) return nullptr;
    char chunk[512];
    size_t n;
    celer::JsStripper count;
    while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0) count.feed(chunk, n);
    const size_t len = count.finish();
    if (len == 0) {
        fclose(f);
        return nullptr;
    }
    // PSRAM quando ha; sem PSRAM, a IRAM livre acessivel a byte (CYD:
    // CONFIG_ESP32_IRAM_AS_8BIT_ACCESSIBLE_MEMORY, ~40KB que o heap 8-bit
    // nao usa) — o fonte sai do heap durante o pico do compile. Cada acesso
    // a byte la custa uma excecao (~167 ciclos): ~30ms num app de 30KB.
    char* buf = (char*)heap_caps_malloc(len + 1, Board::profile().hasPsram ? (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
                                                                            : MALLOC_CAP_IRAM_8BIT);
    if (buf == nullptr) buf = (char*)heap_caps_malloc(len + 1, MALLOC_CAP_8BIT);
    if (buf == nullptr) {
        fclose(f);
        *oom = true;
        return nullptr;
    }
    rewind(f);
    celer::JsStripper fill(buf);
    while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0) fill.feed(chunk, n);
    fill.finish();
    fclose(f);
    buf[len] = 0;
    *lenOut = len;
    return buf;
}

static void *my_alloc(void *udata, duk_size_t size) {
    (void)udata;
    if (size == 0) return nullptr;
    // NOTA: sem piso AQUI de proposito — negar alocacao no meio do run
    // derruba o Duktape no caminho de erro (intern -> throw error object ->
    // malloc -> spinlock corrupto, LoadStoreError medido no device). O piso
    // age so no duk_create_heap (dukHeapBudgetOk no runFile).
    uint32_t first, second;
    duk_caps(size, &first, &second);
    void *p = heap_caps_malloc(size, first);
    if (!p && second != first) p = heap_caps_malloc(size, second);
    if (!p) {
        static int oomLogs = 0;
        if (oomLogs++ < 1) {
            char b[80];
            snprintf(b, sizeof(b), "[duk-oom] size=%u free=%u", (unsigned)size,
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT));
            celer_log_println(b);
        }
    }
    return p;
}

static void *my_realloc(void *udata, void *ptr, duk_size_t size) {
    (void)udata;
    if (size == 0) {
        free(ptr);
        return nullptr;
    }
    uint32_t first, second;
    duk_caps(size, &first, &second);
    void *p = heap_caps_realloc(ptr, size, first);
    if (!p && second != first) p = heap_caps_realloc(ptr, size, second);
    if (!p) celer_log_println("out of memory");
    return p;
}

static void my_free(void *udata, void *ptr) {
    free(ptr);
}

// Motivo do ultimo fatal do Duktape: RAM RTC sobrevive ao ESP.restart (nao
// a um power-on). O fatal reinicia sem coredump e o logcat (RAM) se perde —
// sem isto o reinicio era mudo. Lido uma vez no boot (takeLastFatal).
RTC_NOINIT_ATTR static uint32_t s_fatalMagic;
RTC_NOINIT_ATTR static char s_fatalMsg[80];
static constexpr uint32_t kFatalMagic = 0xCE1EFA7Au;

const char* CelerKernel::takeLastFatal() {
    if (s_fatalMagic != kFatalMagic) return nullptr;
    s_fatalMagic = 0;
    s_fatalMsg[sizeof(s_fatalMsg) - 1] = 0;
    return s_fatalMsg;
}

// Dummy fatal error handler if duktape aborts
static void my_fatal(void *udata, const char *msg) {
    celer_log_print("Duktape fatal error: ");
    celer_log_println(msg ? msg : "no message");
    strncpy(s_fatalMsg, msg ? msg : "sem mensagem", sizeof(s_fatalMsg) - 1);
    s_fatalMsg[sizeof(s_fatalMsg) - 1] = 0;
    s_fatalMagic = kFatalMagic;

    // Tela estatica SEM alocacao: o fatal tipico e OOM ("alloc failed") e o
    // heap do sistema pode estar esgotado — std::string aqui era abort()
    // dentro do proprio handler (o device rebootava sem mostrar nada).
    CelerDisplay* tft = CelerKernel::tftInstance;
    if (tft) {
        tft->clearClipRect();
        tft->startWrite();
        tft->fillScreen(THEME_BG);
        int hdr = UI::sy(52);
        tft->fillRect(0, 0, UI::W, hdr, THEME_CARD);
        tft->fillRect(0, hdr - UI::sy(3), UI::W, UI::sy(3), THEME_ERR);
        tft->setTextDatum(ML_DATUM);
        tft->setTextColor(THEME_TEXT);
        tft->drawString(i18n::TR("Erro fatal do runtime", "Runtime fatal error"),
                        UI::sx(14), hdr / 2, kui::type::title());
        tft->setTextDatum(TL_DATUM);
        tft->setTextColor(THEME_TEXT_DIM);
        bool oom = (msg && strstr(msg, "alloc"));
        tft->drawString(oom ? i18n::TR("O app ficou sem memória.", "The app ran out of memory.")
                            : i18n::TR("O app travou o runtime.", "The app broke the runtime."),
                        UI::sx(14), hdr + UI::sy(16), kui::type::body());
        tft->drawString(i18n::TR("O sistema vai reiniciar...", "The system will restart..."),
                        UI::sx(14), hdr + UI::sy(34), kui::type::body());
        tft->endWrite();
    }
    delay(2500);
    ESP.restart(); // Reboot when they close it
}

void CelerKernel::init(CelerDisplay *tft) {
    tftInstance = tft;
    // Duktape heap is no longer initialized here to save 60-80KB of RAM for the WebServer/WiFi.
    // It will be allocated on-demand in runFile() and checkSyntax().
    celer_log_println("CelerKernel initialized successfully.");
}

void CelerKernel::checkJSError(duk_context *ctx, duk_int_t result) {
    if (result != 0) {
        // Saida limpa: erro marcado com a propriedade celerExit (throwAppExit
        // do JSBindings) ou a string "OS_EXIT" exata lancada por apps legado
        // (ex. Terminal relanca a que recebe). Antes comparava substring:
        // qualquer erro contendo "OS_EXIT" fechava o app em silencio.
        if (duk_is_error(ctx, -1)) {
            duk_get_prop_string(ctx, -1, "celerExit");
            bool marked = duk_is_boolean(ctx, -1) && duk_get_boolean(ctx, -1);
            duk_pop(ctx);
            if (marked) {
                duk_pop(ctx);
                return;
            }
        } else if (duk_is_string(ctx, -1) &&
                   strcmp(duk_safe_to_string(ctx, -1), "OS_EXIT") == 0) {
            duk_pop(ctx);
            return;
        }

        std::string errorMsg;
        if (duk_is_error(ctx, -1)) {
            duk_get_prop_string(ctx, -1, "stack");
            errorMsg = duk_safe_to_string(ctx, -1);
            duk_pop(ctx);
        } else {
            errorMsg = duk_safe_to_string(ctx, -1);
        }

        // Intercept OOM signals
        if (errorMsg.find("alloc") != std::string::npos || errorMsg.find("out of memory") != std::string::npos) {
            showRuntimeError(i18n::TR("Sem memória", "Out of memory"), kOomHint);
            duk_pop(ctx);
            // This is a soft-error (not Duktape fatal), so we can just return safely to Launcher
            return;
        }

        celer_log_print("JS Execution Error: ");
        celer_log_println(errorMsg.c_str());

        showRuntimeError(i18n::TR("Erro no app", "App error"), errorMsg);
    }
    duk_pop(ctx); // pop result or error
}

void CelerKernel::executeJS(const char* jsCode) {
    if (!ctx) return;
    
    duk_int_t rc = duk_peval_string(ctx, jsCode);
    checkJSError(ctx, rc);
}

// Struct to pass data to the syntax check task
struct SyntaxCheckParams {
    const char* jsCode;
    std::string result;
    bool done;
};

static void syntaxCheckTask(void* param) {
    SyntaxCheckParams* p = (SyntaxCheckParams*)param;
    
    duk_context *tempCtx = duk_create_heap(my_alloc, my_realloc, my_free, nullptr, nullptr);
    if (!tempCtx) {
        p->result = "Out of Memory allocating JS heap";
        p->done = true;
        vTaskDelete(NULL);
        return;
    }
    
    duk_int_t rc = duk_pcompile_string(tempCtx, 0, p->jsCode);
    if (rc != 0) {
        p->result = duk_safe_to_string(tempCtx, -1);
        celer_log_printf("Syntax Error: %s\n", p->result.c_str());
    } else {
        p->result = "";
    }
    duk_pop(tempCtx);
    duk_destroy_heap(tempCtx);
    
    p->done = true;
    vTaskDelete(NULL);
}

std::string CelerKernel::checkSyntax(const char* jsCode) {
    SyntaxCheckParams params;
    params.jsCode = jsCode;
    params.result = "";
    params.done = false;
    
    // Run in a dedicated task with 16KB stack to avoid overflowing loopTask
    BaseType_t created = xTaskCreatePinnedToCore(
        syntaxCheckTask,
        "syntaxChk",
        16384,        // 16KB stack just for this task
        &params,
        1,            // Low priority
        NULL,
        portNUM_PROCESSORS - 1  // core 1 (APP) no dual-core; 0 na CYD unicore
    );
    
    if (created != pdPASS) {
        return "Failed to create syntax check task";
    }
    
    // Block until the task finishes
    while (!params.done) {
        delay(10);
    }
    
    return params.result;
}

void CelerKernel::runFile(const char* filePath, const char* appTitle, bool topbarFixed,
                         const char* appPkg, uint32_t perms) {
    if (ctx) {
        duk_destroy_heap(ctx);
        ctx = nullptr;
    }

    // Sem PSRAM: o app e dono do vidro — buffer do Canvas (16KB) e caches
    // de icone (~10KB cada) voltam ao heap para o Duktape; o launcher
    // realoca/recarrega sozinho ao voltar
    if (!Board::profile().hasPsram) {
        kui::releaseCanvasBuffer();
        Icon::releaseAll();
    }
    // heap que o app pode usar: DRAM + IRAM de transbordo (duk_caps)
    appLaunchFreeHeap = heap_caps_get_free_size(MALLOC_CAP_8BIT) + heap_caps_get_free_size(MALLOC_CAP_IRAM_8BIT);

    const uint32_t t0 = millis();
    // Fonte PRIMEIRO: o bloco contiguo sai do heap ainda pouco fragmentado
    // (antes do heap Duktape espalhar alocacoes pequenas)
    size_t srcLen = 0;
    bool srcOom = false;
    char* src = loadAppSource(filePath, &srcLen, &srcOom);
    if (src == nullptr) {
        celer_log_printf(srcOom ? "sem bloco para o fonte: %s (maior bloco %u)\n" : "Failed to read JS file: %s\n",
                         filePath, (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
        if (srcOom) showRuntimeError(i18n::TR("Sem memória", "Out of memory"), kOomHint);
        return;
    }

    if (!dukHeapBudgetOk(srcLen)) {
        celer_log_printf("heap baixo p/ app JS: livre=%u precisa=%u (fonte %u)\n",
                         (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT), (unsigned)dukHeapNeed(srcLen),
                         (unsigned)srcLen);
        free(src);
        showRuntimeError(i18n::TR("Sem memória", "Out of memory"), kOomHint);
        return; // Soft exit back to OS
    }

    celer_log_printf("[duk] fonte lido: %u ms\n", (unsigned)(millis() - t0));
    const size_t freeBefore = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    ctx = duk_create_heap(my_alloc, my_realloc, my_free, nullptr, my_fatal);
    if (!ctx) {
        free(src);
        celer_log_println("Failed to create Duktape heap for app.");
        showRuntimeError(i18n::TR("Sem memória", "Out of memory"), kOomHint);
        return; // Soft exit back to OS
    }

    JSBindings::init(ctx, tftInstance, appTitle, topbarFixed, appPkg, perms);
    celer_log_printf("[duk] heap base+API: %u B (livre %u, iram %u)\n",
                     (unsigned)(freeBefore - heap_caps_get_free_size(MALLOC_CAP_8BIT)),
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_IRAM_8BIT));
    
    {
        duk_push_string(ctx, filePath);
        duk_int_t rc = duk_pcompile_lstring_filename(ctx, 0, src, srcLen);
        free(src);  // o bytecode ja esta no heap: o fonte volta antes do app rodar
        celer_log_printf("[duk] compilado: livre %u (fonte %u B, %u ms)\n",
                         (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT), (unsigned)srcLen,
                         (unsigned)(millis() - t0));
        if (rc != 0) {
            checkJSError(ctx, rc);
            duk_destroy_heap(ctx);
            ctx = nullptr;
            return;
        }
    }

    duk_int_t rc = duk_pcall(ctx, 0);
    // o recorte do display e do app (faixa/System.setClip): o launcher e as
    // telas de erro desenham na tela inteira
    if (tftInstance) tftInstance->clearClipRect();
    BoardIO::ledOff();  // LED e estado do app: nao fica aceso depois que ele sai
    checkJSError(ctx, rc);
    
    // Destroy heap after app exits to free RAM
    duk_destroy_heap(ctx);
    ctx = nullptr;
}

void CelerKernel::loop() {
    
}
