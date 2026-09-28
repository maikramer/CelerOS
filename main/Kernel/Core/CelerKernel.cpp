#include "CelerKernel.h"
#include "../../USBDevice/LogSink.h"
#include "../../Display/Layout.h"
#include "../../Runtime/JSBindings.h"
#include "../../FileSystem/FileSystem.h"
#include "../../Utils/StrUtils.h"
#include "../../Display/Theme.h"
#include "../../UI/Kui.h"
#include <vector>
#include "esp_heap_caps.h"
#include "esp_task_wdt.h"

duk_context *CelerKernel::ctx = nullptr;
CelerDisplay *CelerKernel::tftInstance = nullptr;

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
    tft->drawString("Toque para voltar", UI::cx(), by + bh / 2, body);
    tft->endWrite();

    // espera soltar (o toque que causou o erro), depois um tap completo —
    // usuario pode demorar: alimenta o watchdog da main task enquanto espera
    uint16_t tx, ty;
    while (kui::readTouch(&tx, &ty)) { esp_task_wdt_reset(); delay(20); }
    while (!kui::readTouch(&tx, &ty)) { esp_task_wdt_reset(); delay(20); }
    while (kui::readTouch(&tx, &ty)) { esp_task_wdt_reset(); delay(20); }
}

static const char* kOomHint =
    "O app ficou sem memoria. Feche outros recursos (servidor web, WiFi) "
    "e tente de novo.";

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

static uint32_t duk_caps() {
    if (!Board::profile().hasPsram) return MALLOC_CAP_8BIT;
    if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) > kInternalReserve) return MALLOC_CAP_8BIT;
    return MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
}

static void *my_alloc(void *udata, duk_size_t size) {
    (void)udata;
    if (size == 0) return nullptr;
    uint32_t caps = duk_caps();
    void *p = heap_caps_malloc(size, caps);
    if (!p && caps != MALLOC_CAP_8BIT) p = heap_caps_malloc(size, MALLOC_CAP_8BIT);
    if (!p) celer_log_println("out of memory");
    return p;
}

static void *my_realloc(void *udata, void *ptr, duk_size_t size) {
    (void)udata;
    if (size == 0) {
        free(ptr);
        return nullptr;
    }
    uint32_t caps = duk_caps();
    void *p = heap_caps_realloc(ptr, size, caps);
    if (!p && caps != MALLOC_CAP_8BIT) p = heap_caps_realloc(ptr, size, MALLOC_CAP_8BIT);
    if (!p) celer_log_println("out of memory");
    return p;
}

static void my_free(void *udata, void *ptr) {
    free(ptr);
}

// Dummy fatal error handler if duktape aborts
static void my_fatal(void *udata, const char *msg) {
    celer_log_print("Duktape fatal error: ");
    celer_log_println(msg ? msg : "no message");
    
    std::string detail = (msg && strstr(msg, "alloc")) ? std::string(kOomHint) : std::string(msg ? msg : "erro fatal");
    showRuntimeError("Erro fatal do runtime", detail + "\n\nO sistema vai reiniciar.");

    if (msg && strstr(msg, "alloc")) {
        celer_log_println("out of memory");
    }
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
            showRuntimeError("Sem memoria", kOomHint);
            duk_pop(ctx);
            // This is a soft-error (not Duktape fatal), so we can just return safely to Launcher
            return;
        }

        celer_log_print("JS Execution Error: ");
        celer_log_println(errorMsg.c_str());

        showRuntimeError("Erro no app", errorMsg);
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
        1             // Run on Core 1
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

void CelerKernel::runFile(const char* filePath, const char* appTitle, bool topbarFixed) {
    if (ctx) {
        duk_destroy_heap(ctx);
        ctx = nullptr;
    }

    ctx = duk_create_heap(my_alloc, my_realloc, my_free, nullptr, my_fatal);
    if (!ctx) {
        celer_log_println("Failed to create Duktape heap for app.");
        showRuntimeError("Sem memoria", kOomHint);
        return; // Soft exit back to OS
    }

    JSBindings::init(ctx, tftInstance, appTitle, topbarFixed);
    
    {
        std::string content = FileSystem::readTextFile(filePath);
        if (content.length() == 0) {
            celer_log_print("Failed to read JS file: ");
            celer_log_println(filePath);
            duk_destroy_heap(ctx);
            ctx = nullptr;
            return;
        }

        duk_push_string(ctx, filePath);
        duk_int_t rc = duk_pcompile_string_filename(ctx, 0, content.c_str());
        if (rc != 0) {
            checkJSError(ctx, rc);
            duk_destroy_heap(ctx);
            ctx = nullptr;
            return;
        }
    } // `content` String is destroyed here, freeing 50KB+ of RAM before the app runs

    duk_int_t rc = duk_pcall(ctx, 0);
    checkJSError(ctx, rc);
    
    // Destroy heap after app exits to free RAM
    duk_destroy_heap(ctx);
    ctx = nullptr;
}

void CelerKernel::loop() {
    
}
