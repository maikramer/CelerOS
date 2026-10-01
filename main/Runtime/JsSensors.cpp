// Sensors.* (API 13) — leitura do IMU da placa (hooks BoardProfile::imu*,
// hoje so o watch Waveshare: QMI8658). Leituras abertas (sem permissao):
// sao sensores do proprio aparelho, no padrao dos demais (bateria, luz).

#include "JSBindings.h"
#include "JsInternal.h"
#include "../Boards/Board.h"

duk_ret_t JSBindings::js_sensorsAccel(duk_context* ctx) {
    const BoardProfile& bp = Board::profile();
    if (bp.imuAccel == nullptr) {
        duk_push_null(ctx);
        return 1;
    }
    float x = 0, y = 0, z = 0;
    if (!bp.imuAccel(&x, &y, &z)) {
        duk_push_null(ctx);
        return 1;
    }
    duk_push_object(ctx);
    duk_push_number(ctx, x);
    duk_put_prop_string(ctx, -2, "x");
    duk_push_number(ctx, y);
    duk_put_prop_string(ctx, -2, "y");
    duk_push_number(ctx, z);
    duk_put_prop_string(ctx, -2, "z");
    return 1;
}

duk_ret_t JSBindings::js_sensorsSteps(duk_context* ctx) {
    const BoardProfile& bp = Board::profile();
    if (bp.imuSteps == nullptr) {
        duk_push_int(ctx, -1);
        return 1;
    }
    duk_push_int(ctx, (duk_int_t)bp.imuSteps());
    return 1;
}

duk_ret_t JSBindings::js_sensorsTemp(duk_context* ctx) {
    const BoardProfile& bp = Board::profile();
    float c = 0;
    if (bp.imuTemp == nullptr || !bp.imuTemp(&c)) {
        duk_push_number(ctx, -255);   // sem sensor (bateria/IMU ausente)
        return 1;
    }
    duk_push_number(ctx, c);
    return 1;
}
