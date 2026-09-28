//
// Created by maikeu on 30/12/2021.
//

#ifndef PROJETO_BETA_BATTERY_H
#define PROJETO_BETA_BATTERY_H

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include <esp_log.h>

static constexpr uint32_t READ2VBAT(uint32_t READ) { return READ * 2; }

static constexpr int DEFAULT_VREF = 1100;
static const int NO_OF_SAMPLES = 4;
static constexpr adc_atten_t ATTENUATION = ADC_ATTEN_DB_11;
static constexpr adc_unit_t ADC_UNIT = ADC_UNIT_1;
static constexpr adc_bitwidth_t ADC_WIDTH = ADC_BITWIDTH_12;

static bool adc_calibration_init(adc_unit_t unit, adc_atten_t atten, adc_cali_handle_t *out_handle);

//static void adc_calibration_deinit(adc_cali_handle_t handle);

class Battery {
public:
    Battery(adc_channel_t channel) {
        //-------------ADC1 Init---------------//

        adc_oneshot_unit_init_cfg_t init_config1 = {
                .unit_id = ADC_UNIT,
                .ulp_mode = ADC_ULP_MODE_DISABLE
        };
        ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &_oneshotUnitHandle));

        //-------------ADC1 Config---------------//
        adc_oneshot_chan_cfg_t config = {
                .atten = ATTENUATION,
                .bitwidth = ADC_WIDTH,
        };
        ESP_ERROR_CHECK(adc_oneshot_config_channel(_oneshotUnitHandle, channel, &config));
        ESP_ERROR_CHECK(adc_oneshot_config_channel(_oneshotUnitHandle, channel, &config));

        //-------------ADC1 Calibration Init---------------//
        _calibrationSuccess = adc_calibration_init(ADC_UNIT_1, ADC_ATTEN_DB_11,
                                                   &_adcHandle);
        _channel = channel;

    }


    uint32_t GetVoltage() {
        if (!_calibrationSuccess) {
            ESP_LOGE(__FUNCTION__, "Calibração inválida para o ADC");
            return 0;
        }
        int adc_reading = 0;
        for (int i = 0; i < NO_OF_SAMPLES; i++) {
            int adc_raw;
            ESP_ERROR_CHECK(adc_oneshot_read(_oneshotUnitHandle, _channel, &adc_raw));
            adc_reading += adc_raw;
        }
        adc_reading /= NO_OF_SAMPLES;
        //Convert adc_reading to voltage in mV
        int voltage;
        ESP_ERROR_CHECK(adc_cali_raw_to_voltage(_adcHandle, adc_reading, &voltage));
        return READ2VBAT(voltage);
    };

private:
    adc_channel_t _channel;
    adc_cali_handle_t _adcHandle;
    bool _calibrationSuccess;
    adc_oneshot_unit_handle_t _oneshotUnitHandle;
};

static bool
adc_calibration_init(adc_unit_t unit, adc_atten_t atten, adc_cali_handle_t *out_handle) {
    const char *TAG = __FUNCTION__;
    adc_cali_handle_t handle = NULL;
    esp_err_t ret = ESP_FAIL;
    bool calibrated = false;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    if (!calibrated) {
        ESP_LOGI(TAG, "calibration scheme version is %s", "Curve Fitting");
        adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = unit,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_curve_fitting(&cali_config, &handle);
        if (ret == ESP_OK) {
            calibrated = true;
        }
    }
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    if (!calibrated) {
        ESP_LOGI(TAG, "calibration scheme version is %s", "Line Fitting");
        adc_cali_line_fitting_config_t cali_config = {
                .unit_id = unit,
                .atten = atten,
                .bitwidth = ADC_BITWIDTH_DEFAULT,
                .default_vref= DEFAULT_VREF,
        };
        ret = adc_cali_create_scheme_line_fitting(&cali_config, &handle);
        if (ret == ESP_OK) {
            calibrated = true;
        }
    }
#endif

    *out_handle = handle;
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Calibration Success");
    } else if (ret == ESP_ERR_NOT_SUPPORTED || !calibrated) {
        ESP_LOGW(TAG, "eFuse not burnt, skip software calibration");
    } else {
        ESP_LOGE(TAG, "Invalid arg or no memory");
    }

    return calibrated;
}

//static void adc_calibration_deinit(adc_cali_handle_t handle) {
//    const char *TAG = __FUNCTION__;
//#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
//    ESP_LOGI(TAG, "deregister %s calibration scheme", "Curve Fitting");
//    ESP_ERROR_CHECK(adc_cali_delete_scheme_curve_fitting(handle));
//
//#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
//    ESP_LOGI(TAG, "deregister %s calibration scheme", "Line Fitting");
//    ESP_ERROR_CHECK(adc_cali_delete_scheme_line_fitting(handle));
//#endif
//}

#endif //PROJETO_BETA_BATTERY_H