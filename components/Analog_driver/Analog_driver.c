#include <stdio.h>
#include "BOARD.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/adc.h"
#include "driver/temp_sensor.h"
#include "esp_adc_cal.h"
#include "Analog_driver.h"

//--------- ULP -----------
#include "driver/rtc_io.h"
#include "esp32s3/ulp.h"
#include "esp32s3/ulp_riscv.h"
#include "esp32s3/ulp_riscv_adc.h"
#include "hal/adc_ll.h"
#include "hal/adc_hal.h"
#include "main_ulp_adc.h"
extern const uint8_t ulp_main_bin_start[] asm("_binary_main_ulp_adc_bin_start");
extern const uint8_t ulp_main_bin_end[]   asm("_binary_main_ulp_adc_bin_end");
static esp_err_t ulp_init_analog();
static esp_err_t ulp_run_analog();
static esp_err_t ulp_execute_and_wait();
static void ulp_riscv_reset();

#define GET_UNIT(x)        ((x>>3) & 0x1)

static const char* TAG = "Analog";

uint32_t ADC_CHANNELS[ADC_CHANNELS_NUM] = {ADC_CHANNELS_LIST};
static esp_adc_cal_characteristics_t  adc_chars;
static uint32_t ign_det_thr = 50;

static float    filter_coeff	 = 0.6f;
static float    filter_coeff_ign = 0.5f;
static uint32_t voltage_ign[IGN_NUM] = {0};
static uint32_t voltage_vbat = 0;
static float	mcu_temp	 = 0.0f;
static uint32_t vbat_mV_raw = 0;


uint32_t Analog_getIGN(uint32_t ign_num, uint32_t vbat);
uint32_t Analog_getVBAT();

esp_err_t Analog_init(uint32_t ign_det_thr_val, float filter)
{
	ign_det_thr = ign_det_thr_val;
	filter_coeff = filter;

	//Check if TP is burned into eFuse
	if (esp_adc_cal_check_efuse(ESP_ADC_CAL_VAL_EFUSE_TP) == ESP_OK) {
		ESP_LOGI(TAG, "eFuse Two Point: Supported");
	} else {
		ESP_LOGI(TAG, "eFuse Two Point: NOT supported");
	}
	//Check Vref is burned into eFuse
	if (esp_adc_cal_check_efuse(ESP_ADC_CAL_VAL_EFUSE_VREF) == ESP_OK) {
		ESP_LOGI(TAG, "eFuse Vref: Supported");
	} else {
		ESP_LOGI(TAG, "eFuse Vref: NOT supported");
	}

	//Check TP+Vref is burned into eFuse
	if (esp_adc_cal_check_efuse(ESP_ADC_CAL_VAL_EFUSE_TP_FIT) == ESP_OK) {
		ESP_LOGI(TAG, "eFuse Two Point+Vref: Supported");
	} else {
		ESP_LOGI(TAG, "eFuse Point+Vref: NOT supported");
	}

	//Characterize ADC
	esp_adc_cal_value_t val_type = esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_2_5, ADC_WIDTH_BIT_12, 1100, &adc_chars);
	ESP_LOGI(TAG, "ADC calibration type: %i, Vref: %u", (int)val_type, adc_chars.vref);

	// Init ADC for ULP
	if(ulp_init_analog() != ESP_OK){
		ESP_LOGE(TAG, "ULP ADC init failed!");
		return ESP_FAIL;
	}

	//temp_sensor_read_celsius(&mcu_temp);	// Not implemented for ULP yet

	return ESP_OK;
}

uint32_t Analog_getIGN(uint32_t ign_num, uint32_t vbat){
	uint32_t voltage = esp_adc_cal_raw_to_voltage((&ulp_IGN_RAW)[ign_num], &adc_chars);
	ESP_LOGV(TAG, "IGN1 voltage: %dmV", voltage);

    voltage_ign[ign_num] = filter_coeff_ign * voltage + (1-filter_coeff_ign) * voltage_ign[ign_num];

    return voltage_ign[ign_num];
}

uint32_t Analog_getVBAT(){
	uint32_t voltage = esp_adc_cal_raw_to_voltage(ulp_VBAT_RAW, &adc_chars);
	vbat_mV_raw = voltage * 11.0f * 1.024f;

    voltage_vbat = filter_coeff * vbat_mV_raw + (1-filter_coeff) * voltage_vbat;

    ESP_LOGV(TAG, "Raw: %i,   voltage: %i	Vbat: %imV", ulp_VBAT_RAW, voltage, voltage_vbat);
    return voltage_vbat;
}

float Analog_getTempMCU(){
	float result = 0.0f;
//	temp_sensor_read_celsius(&result);
	mcu_temp = filter_coeff * result + (1-filter_coeff) * mcu_temp;
//	ESP_LOGV(TAG, "MCU temp: %.2f", mcu_temp);

	return mcu_temp;
}

void Analog_update(Analog_meas_t * meas){
	// Execute ULP program and wait for results
	ulp_execute_and_wait();

	meas->vbat_mV = Analog_getVBAT();

	//TODO support variable threshold and fuse check
	if(vbat_mV_raw > 3200){
		ign_det_thr = (meas->vbat_mV*12 - 12619)/1000;
		for(uint8_t i=0; i<IGN_NUM; i++){
			meas->IGN_det[i] = (Analog_getIGN(i, vbat_mV_raw) < ign_det_thr);
		}
	} else if(meas->vbat_mV < 3200){
		for(uint8_t i=0; i<IGN_NUM; i++){
			meas->IGN_det[i] = -1;
		}
	}

	meas->temp = Analog_getTempMCU();
}

int8_t Analog_getIGNstate(Analog_meas_t * meas, uint8_t ign_no){
	if(ign_no > IGN_NUM)
		return -1;

	if(meas == NULL)
		return -1;

	return meas->IGN_det[ign_no];
}

//---------------------------------- ULP ---------------------------------
static esp_err_t ulp_init_analog()
{

//	ulp_riscv_adc_cfg_t cfg = {
//		.channel = ADC1_CHANNEL_7,
//		.width   = ADC_WIDTH_BIT_12,
//		.atten   = ADC_ATTEN_DB_2_5,
//	};
//	ESP_ERROR_CHECK(ulp_riscv_adc_init(&cfg));
//	ESP_LOGI(TAG, "ADC ULP init done");


	esp_err_t err = ESP_OK;
	// Init ADC (modified version of ulp_riscv_adc_init() with multi-channel support)
	adc1_config_width(ADC_WIDTH_BIT_12);

	for(uint8_t i=0; i<ADC_CHANNELS_NUM; i++){
		adc1_config_channel_atten(ADC_CHANNELS[i], ADC_ATTEN_DB_2_5);
	}

	//Calibrate the ADC
	extern uint32_t get_calibration_offset(adc_unit_t adc_n, adc_channel_t chan);
	uint32_t cal_val = get_calibration_offset(ADC_NUM_1, ADC_CHANNELS[0]);
	adc_hal_set_calibration_param(ADC_NUM_1, cal_val);

	//Temp sensor init
//	temp_sensor_config_t temp_sensor = TSENS_CONFIG_DEFAULT();
//	temp_sensor.dac_offset = TSENS_DAC_L2;  //TSENS_DAC_L2 is default   L4(-40℃ ~ 20℃), L2(-10℃ ~ 80℃) L1(20℃ ~ 100℃) L0(50℃ ~ 125℃)
//	temp_sensor_set_config(temp_sensor);
//	temp_sensor_start();

	// Handle ADC access to RTC controller
	extern esp_err_t adc1_rtc_mode_acquire(void);
	err = adc1_rtc_mode_acquire();

	return err;
}

static esp_err_t ulp_run_analog(){
	// Load ULP RISCV program
	ESP_LOGV(TAG, "Load ULP code. Program size: %i B", (ulp_main_bin_end - ulp_main_bin_start));
	esp_err_t err = ulp_riscv_load_binary(ulp_main_bin_start, (ulp_main_bin_end - ulp_main_bin_start));
	ESP_ERROR_CHECK(err);

	/* Start the program */
	ulp_set_wakeup_period(0, 20000);
	err = ulp_riscv_run();
	ESP_ERROR_CHECK(err);

	return err;
}

static esp_err_t ulp_execute_and_wait(){
	//Run ADC ULP program
	ulp_riscv_reset();
	ulp_run_analog();

	// Wait max 1000 ticks for ADC results
	for(uint8_t i=0; i<5; i++){
		if(ulp_READY == 1){
			return ESP_OK;
		}
		vTaskDelay(10);
	}

	ESP_LOGW(TAG, "ULP to slow...");
	return ESP_FAIL;
}

static void ulp_riscv_reset()
{
    CLEAR_PERI_REG_MASK(RTC_CNTL_COCPU_CTRL_REG, RTC_CNTL_COCPU_SHUT | RTC_CNTL_COCPU_DONE);
    CLEAR_PERI_REG_MASK(RTC_CNTL_COCPU_CTRL_REG, RTC_CNTL_COCPU_SHUT_RESET_EN);
    esp_rom_delay_us(20);
    SET_PERI_REG_MASK(RTC_CNTL_COCPU_CTRL_REG, RTC_CNTL_COCPU_SHUT | RTC_CNTL_COCPU_DONE);
    SET_PERI_REG_MASK(RTC_CNTL_COCPU_CTRL_REG, RTC_CNTL_COCPU_SHUT_RESET_EN);
}
