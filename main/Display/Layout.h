#ifndef CELEROS_LAYOUT_H
#define CELEROS_LAYOUT_H

#include "Boards/Board.h"

// ============================================================================
// Layout adaptativo do CelerOS.
//
// O design de origem e 240x320 (retrato). Todas as telas derivam suas
// coordenadas daqui para se adaptar a resolucao real do display:
//   - metricas horizontais: proporcionais a W (240 -> W)
//   - metricas verticais:   proporcionais a H (320 -> H)
//   - fontes numericas:     mapeadas para a fonte maior mais proxima em
//                           telas largas (480x480 usa fontes 2x)
//
// Em 240x320 os valores reproduzem exatamente o layout original.
//
// Uso: UI::init(tft.width(), tft.height()) no boot; depois UI::W, UI::sx(v),
// UI::font(2), UI::LIST_Y, UI::ITEM_H etc. Desenho e hit-test usam sempre as
// mesmas constantes.
// ============================================================================

namespace UI {

inline int W = 240;
inline int H = 320;

// Tela "grande" (usa fontes maiores)
inline bool big = false;

// Regioes padrao do design 240x320
inline int HEADER_H = 30;        // faixa do titulo
inline int LIST_Y = 45;          // topo da area de lista
inline int ITEM_H = 30;          // altura de linha de item
inline int ITEMS_PER_PAGE = 7;   // itens visiveis por pagina
inline int FOOTER_Y = 285;       // topo do rodape de botoes
inline int FOOTER_TOUCH_Y = 280; // inicio da area de toque do rodape
inline int SETTINGS_TOUCH_Y1 = 240;  // faixa de botoes do settings (topo)
inline int SETTINGS_TOUCH_Y2 = 275;  // faixa de botoes do settings (fim)

// ---- Conversores de coordenada (design 240x320 -> tela atual) -------------
inline int sx(int v) { return v * W / 240; }
inline int sy(int v) { return v * H / 320; }
inline int cx()      { return W / 2; }
inline int cy()      { return H / 2; }

inline void init(int w, int h) {
    W = w;
    H = h;
    big = (w >= 400);

    HEADER_H        = sy(30);
    LIST_Y          = sy(45);
    ITEM_H          = sy(30);
    FOOTER_TOUCH_Y  = sy(280);
    FOOTER_Y        = sy(285);
    ITEMS_PER_PAGE  = (FOOTER_TOUCH_Y - LIST_Y) / ITEM_H;
    if (ITEMS_PER_PAGE < 1) ITEMS_PER_PAGE = 1;
    SETTINGS_TOUCH_Y1 = sy(240);
    SETTINGS_TOUCH_Y2 = sy(275);
}

// ---- Fonte numerica (1/2/4) escalada ---------------------------------------
inline int font(int f) {
    if (!big) return f;
    switch (f) {
        case 1: return 2;
        case 2: return 4;
        case 4: return 6;
        default: return f;
    }
}

// ---- Botao de sair de apps JS (canto superior direito) ---------------------
inline int exitX() { return W - sx(40); }
inline int exitY() { return 0; }
inline int exitW() { return sx(40); }
inline int exitH() { return sy(30); }
inline bool hitExit(int x, int y) { return x >= exitX() && y <= sy(40); }

}  // namespace UI

#endif  // CELEROS_LAYOUT_H
