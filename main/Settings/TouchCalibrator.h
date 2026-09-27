#ifndef TOUCH_CALIBRATOR_H
#define TOUCH_CALIBRATOR_H

#include "../Boards/Board.h"

class TouchCalibrator {
public:
    static void init(CelerDisplay *tft);
    static void runCalibration();

private:
    static CelerDisplay *tftInstance;
};

#endif // TOUCH_CALIBRATOR_H
