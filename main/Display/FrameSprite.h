#pragma once

#include "Display.h"

/**
 * @brief Quadro automatico do app (PSRAM) com caixa suja.
 *
 * O present() empurrava o quadro INTEIRO ao vidro a cada mudanca: no watch
 * (410x502 por QSPI + framebuffer do painel) eram ~400 KB de memcpy PSRAM e
 * de flush por segundo so para o Watchface trocar a barra de segundos. Aqui
 * todo desenho no quadro passa por um painel que acumula o retangulo
 * tocado, e o present() empurra so ele.
 *
 * Como: o LGFX_Sprite desenha pelo ponteiro _panel e gerencia o buffer pelo
 * membro _panel_sprite. O DirtyPanel e um Panel_Sprite apontando para o
 * MESMO buffer (setBuffer: pre-alocado, nunca liberado por ele) que entra no
 * lugar do _panel; createSprite/pushSprite seguem no _panel_sprite.
 *
 * Invariantes: rotacao 0 e sem recriar o buffer depois do createFrame()
 * (setColorDepth/createSprite/deleteSprite no quadro desligariam o buffer
 * compartilhado) — o quadro e criado uma vez e reaproveitado entre apps.
 */
class FrameSprite : public CelerSprite {
public:
    explicit FrameSprite(lgfx::LovyanGFX* parent) : CelerSprite(parent) {}
    ~FrameSprite() override { _panel = &_panel_sprite; }

    /// Cria o buffer (PSRAM, 16 bits) e liga o rastreio. nullptr = sem memoria.
    void* createFrame(int32_t w, int32_t h) {
        _panel = &_panel_sprite;
        setPsram(true);
        setColorDepth(16);
        void* buf = createSprite(w, h);
        if (buf == nullptr) return nullptr;
        _dirty.setColorDepth(_write_conv.depth);
        _dirty.setBuffer(buf, w, h, &_write_conv);
        _panel = &_dirty;
        _dirty.markAll();
        return buf;
    }

    /// Retangulo sujo desde a ultima chamada (e zera). false = nada mudou.
    bool takeDirty(int32_t* x, int32_t* y, int32_t* w, int32_t* h) { return _dirty.take(x, y, w, h); }

    /// Tudo sujo (vidro foi sujado por fora do quadro).
    void markAllDirty() { _dirty.markAll(); }

private:
    struct DirtyPanel : public lgfx::Panel_Sprite {
        int32_t l = 0, t = 0, r = -1, b = -1;  // vazio: r < l

        void add(int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
            if (_rotation != 0) {  // fora do invariante: nao arrisca
                markAll();
                return;
            }
            if (r < l) {
                l = x0; t = y0; r = x1; b = y1;
                return;
            }
            if (x0 < l) l = x0;
            if (y0 < t) t = y0;
            if (x1 > r) r = x1;
            if (y1 > b) b = y1;
        }
        void markAll() {
            l = 0;
            t = 0;
            r = (int32_t)_panel_width - 1;
            b = (int32_t)_panel_height - 1;
        }
        bool take(int32_t* x, int32_t* y, int32_t* w, int32_t* h) {
            if (r < l) return false;
            const int32_t pw = (int32_t)_panel_width, ph = (int32_t)_panel_height;
            int32_t x0 = l < 0 ? 0 : l, y0 = t < 0 ? 0 : t;
            int32_t x1 = r >= pw ? pw - 1 : r, y1 = b >= ph ? ph - 1 : b;
            l = 0;
            t = 0;
            r = -1;
            b = -1;
            if (x1 < x0 || y1 < y0) return false;
            *x = x0;
            *y = y0;
            *w = x1 - x0 + 1;
            *h = y1 - y0 + 1;
            return true;
        }

        // Pontos de escrita do Panel_Sprite: writeBlock/writePixels escrevem
        // dentro da janela do setWindow; o alpha/effect do IPanel cai no
        // writeImage.
        void setWindow(uint_fast16_t xs, uint_fast16_t ys, uint_fast16_t xe, uint_fast16_t ye) override {
            lgfx::Panel_Sprite::setWindow(xs, ys, xe, ye);
            add(xs, ys, xe, ye);
        }
        void drawPixelPreclipped(uint_fast16_t x, uint_fast16_t y, uint32_t rawcolor) override {
            lgfx::Panel_Sprite::drawPixelPreclipped(x, y, rawcolor);
            add(x, y, x, y);
        }
        void writeFillRectPreclipped(uint_fast16_t x, uint_fast16_t y, uint_fast16_t w, uint_fast16_t h,
                                     uint32_t rawcolor) override {
            lgfx::Panel_Sprite::writeFillRectPreclipped(x, y, w, h, rawcolor);
            add(x, y, x + w - 1, y + h - 1);
        }
        void writeImage(uint_fast16_t x, uint_fast16_t y, uint_fast16_t w, uint_fast16_t h,
                        lgfx::pixelcopy_t* param, bool use_dma) override {
            lgfx::Panel_Sprite::writeImage(x, y, w, h, param, use_dma);
            add(x, y, x + w - 1, y + h - 1);
        }
        void writeImageARGB(uint_fast16_t x, uint_fast16_t y, uint_fast16_t w, uint_fast16_t h,
                            lgfx::pixelcopy_t* param) override {
            lgfx::Panel_Sprite::writeImageARGB(x, y, w, h, param);
            add(x, y, x + w - 1, y + h - 1);
        }
        void copyRect(uint_fast16_t dst_x, uint_fast16_t dst_y, uint_fast16_t w, uint_fast16_t h,
                      uint_fast16_t src_x, uint_fast16_t src_y) override {
            lgfx::Panel_Sprite::copyRect(dst_x, dst_y, w, h, src_x, src_y);
            add(dst_x, dst_y, dst_x + w - 1, dst_y + h - 1);
        }
    };

    DirtyPanel _dirty;
};
