#ifndef TOUCH_CALIBRATOR_H
#define TOUCH_CALIBRATOR_H

#include "../Display/Display.h"

class TouchCalibrator {
public:
    static void init(KryonDisplay *tft);
    static void runCalibration();

private:
    static KryonDisplay *tftInstance;
};

#endif // TOUCH_CALIBRATOR_H
