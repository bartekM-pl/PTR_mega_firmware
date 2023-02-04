#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"

#include "esp_log.h"
#include "esp_system.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "driver/gpio.h"

//----------- Our includes --------------
#include "SPI_driver.h"
#include "LED_driver.h"
#include "LORA_driver.h"
#include "GNSS_driver.h"
#include "Analog_driver.h"
#include "Storage_driver.h"
#include "Sensors.h"
#include "AHRS_driver.h"
#include "FlightStateDetector.h"
#include "web_driver.h"
#include "DataManager.h"
#include "SysMgr.h"

//----------- Our defines --------------
#define ESP_CORE_0 0
#define ESP_CORE_1 1

static const char *TAG = "KP-PTR";

//----------- Queues etc ---------------
QueueHandle_t queue_AnalogToMain;
QueueHandle_t queue_MainToTelemetry;

// periodic task with timer https://www.esp32.com/viewtopic.php?t=10280

void task_kpptr_main(void *pvParameter){
	TickType_t xLastWakeTime = 0;
	TickType_t prevTickCountRF = 0;
	DataPackage_t *  DataPackage_ptr = NULL;
	DataPackageRF_t  DataPackageRF_d;
	gps_t gps_d;
	Analog_meas_t Analog_meas;
	struct timeval tv_now;
	struct timeval tv_tic;
	struct timeval tv_toc;
	struct timeval tv_comp;
	gettimeofday(&tv_now, NULL);
	int64_t time_us = (int64_t)tv_now.tv_sec * 1000000L + (int64_t)tv_now.tv_usec;

	esp_err_t status = ESP_FAIL;
	while(status != ESP_OK){
		status  = ESP_OK;
		status |= Sensors_init();
		status |= GPS_init();
		//status |= Detector_init();
		status |= AHRS_init(time_us);
		ESP_LOGE(TAG, "Main task - failed to prepare main task");
		SysMgr_checkout(checkout_main, check_fail);
		vTaskDelay(pdMS_TO_TICKS( 1000 ));
	}

	SysMgr_checkout(checkout_main, check_ready);
	ESP_LOGI(TAG, "Task Main - ready!");

	xLastWakeTime = xTaskGetTickCount ();
	while(1){				//<<----- TODO zrobi� wyzwalanie z timera
		vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS( 100 ));

		//----- Tic ----------
		gettimeofday(&tv_tic, NULL);
		//--------------------

		gettimeofday(&tv_now, NULL);
		int64_t time_us = (int64_t)tv_now.tv_sec * 1000000L + (int64_t)tv_now.tv_usec;

		Sensors_update();
		AHRS_compute(time_us, Sensors_get());
		//Detector_detect();


		//LED_blinkWS(0, COLOUR_AQUA, 20, 100, 1, 1);

		GPS_getData(&gps_d, 0);

		xQueueReceive(queue_AnalogToMain, &Analog_meas, 0);

		if(DM_getFreePointerToMainRB(&DataPackage_ptr) == ESP_OK){
			if(DataPackage_ptr != NULL){
				DM_collectFlash(DataPackage_ptr, time_us, Sensors_get(), &gps_d, AHRS_getData(), NULL, NULL, &Analog_meas);
//				ESP_LOGV(TAG, "Added T=%lli", time_us);
				DM_addToMainRB(&DataPackage_ptr);
			} else {
				ESP_LOGE(TAG, "Main RB pointer = NULL!");
			}
		} else {
			ESP_LOGE(TAG, "Main RB error!");
		}

		//send data to RF every 500ms
		if(((prevTickCountRF + pdMS_TO_TICKS( 500 )) <= xLastWakeTime)){
			prevTickCountRF = xLastWakeTime;
			DM_collectRF(&DataPackageRF_d, time_us, Sensors_get(), &gps_d, AHRS_getData(), NULL, NULL);
			xQueueOverwrite(queue_MainToTelemetry, (void *)&DataPackageRF_d); // add to telemetry queue
		}

		//------- Toc ---------
		gettimeofday(&tv_toc, NULL);
		//---------------------

		//------- Comp ---------
		gettimeofday(&tv_comp, NULL);
		//---------------------

		//---------- Tic Toc analysis --------------
		int64_t tic_toc_dt =   ((int64_t)tv_toc.tv_sec  * 1000000L + (int64_t)tv_toc.tv_usec)
							 - ((int64_t)tv_tic.tv_sec  * 1000000L + (int64_t)tv_tic.tv_usec);
		int64_t tic_toc_comp = ((int64_t)tv_comp.tv_sec * 1000000L + (int64_t)tv_comp.tv_usec)
							 - ((int64_t)tv_toc.tv_sec  * 1000000L + (int64_t)tv_toc.tv_usec);
		//ESP_LOGI(TAG, "TicToc dt = %lli us, compensation = %lli us", tic_toc_dt, tic_toc_comp);
		//------------------------------------

	}
	vTaskDelete(NULL);
}

void task_kpptr_telemetry(void *pvParameter){
	DataPackageRF_t DataPackageRF_d;

	while(LORA_init() != ESP_OK){
		ESP_LOGE(TAG, "Telemetry task - failed to prepare Lora");
		SysMgr_checkout(checkout_lora, check_fail);
		vTaskDelay(pdMS_TO_TICKS( 1000 ));
	}

	SysMgr_checkout(checkout_lora, check_ready);
	while(1){
		if(xQueueReceive(queue_MainToTelemetry, &DataPackageRF_d, 100)){
			LORA_sendPacketLoRa((uint8_t *)&DataPackageRF_d, sizeof(DataPackageRF_t), LORA_TX_NO_WAIT);
		}
	}
}

void task_kpptr_storage(void *pvParameter){
	while(Storage_init(Storage_filesystem_littlefs, 0xAABBCCDD) != ESP_OK){
		ESP_LOGE(TAG, "Storage task - failed to prepare storage");
		SysMgr_checkout(checkout_storage, check_void);
		vTaskDelay(pdMS_TO_TICKS( 1000 ));
	}

	DataPackage_t * DataPackage_ptr;
	uint32_t write_error_cnt = 0;

	vTaskDelay(pdMS_TO_TICKS( 2000 ));
	ESP_LOGI(TAG, "Task Storage - ready!");
	SysMgr_checkout(checkout_storage, check_ready);

	while(1){
		if(1){	//(flightstate >= Launch) && (flightstate < Landed_delay)
			if(DM_getUsedPointerFromMainRB_wait(&DataPackage_ptr) == ESP_OK){	//wait max 100ms for new data
				if(write_error_cnt < 1000){
					if(Storage_writePacket((void*)DataPackage_ptr, sizeof(DataPackage_t)) != ESP_OK){
						ESP_LOGE(TAG, "Storage task - packet write fail");
						write_error_cnt++;
					} else {
						write_error_cnt = 0;	// Reset error counter if write successful
					}
				}
				DM_returnUsedPointerToMainRB(&DataPackage_ptr);
			} else {
				//ESP_LOGI(TAG, "Storage timeout");
			}
		}
	}
}

void task_kpptr_utils(void *pvParameter){
	TickType_t xLastWakeTime = 0;
	uint32_t interval_ms = 20;

	esp_err_t status = ESP_FAIL;
	while(status != ESP_OK){
		status  = ESP_OK;
		status |= LED_init(interval_ms);
		status |= BUZZER_init();
		ESP_LOGI(TAG, "Task Utils - failed to init!");
		SysMgr_checkout(checkout_utils, check_fail);
		vTaskDelay(pdMS_TO_TICKS( 1000 ));
	}

	ESP_LOGI(TAG, "Task Utils - ready!");

	SysMgr_checkout(checkout_utils, check_ready);
	xLastWakeTime = xTaskGetTickCount ();
	while(1){
		vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS( interval_ms ));
		LED_srv();
	}
	vTaskDelete(NULL);
}

void task_kpptr_analog(void *pvParameter){
	TickType_t xLastWakeTime = 0;
	uint32_t interval_ms = 100;

	Analog_meas_t Analog_meas;

	while(Analog_init(100, 0.1f) != ESP_OK){
		ESP_LOGI(TAG, "Task Analog - failed to init!");
		SysMgr_checkout(checkout_analog, check_fail);
		vTaskDelay(pdMS_TO_TICKS( 1000 ));
	}
	ESP_LOGI(TAG, "Task Analog - ready!");

	SysMgr_checkout(checkout_analog, check_ready);
	xLastWakeTime = xTaskGetTickCount ();
	while(1){
		vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS( interval_ms ));
		Analog_update(&Analog_meas);

		xQueueOverwrite(queue_AnalogToMain, (void *)&Analog_meas);
	}
	vTaskDelete(NULL);
}

void task_kpptr_sysmgr(void *pvParameter){

	ESP_LOGI(TAG, "SysMgr ready");
	SysMgr_checkout(checkout_sysmgr, check_ready);

	while(1){
		SysMgr_update();	//Process new messages

		switch(SysMgr_getCheckoutStatus()){
		case check_ready:
			LED_blinkWS(LED_STAT, COLOUR_GREEN, 20, 100, 0, 0);
			break;

		case check_void:
			LED_blinkWS(LED_STAT, COLOUR_ORANGE, 20, 100, 0, 0);
			break;

		case check_fail:
			LED_blinkWS(LED_STAT, COLOUR_RED, 20, 300, 300, 0);
			break;

		default:
			break;
		}

		switch(SysMgr_getArm()){
		case system_armed:
			LED_blinkWS(LED_ARM, COLOUR_GREEN, 20, 100, 0, 0);
			break;

		case system_dissarmed:
			LED_blinkWS(LED_ARM, COLOUR_ORANGE, 20, 100, 0, 0);
			break;

		case system_arming_error:
			LED_blinkWS(LED_ARM, COLOUR_RED, 20, 300, 300, 0);
			break;

		default:
			break;
		}

		vTaskDelay(pdMS_TO_TICKS( 1000 ));	// Limit loop rate to max 10Hz
	}
}


void app_main(void)
{
    nvs_flash_init();
    SysMgr_init();
    Web_init();
    SPI_init(2000000);
    DM_init();

    //----- Create queues ----------
    queue_AnalogToMain    = xQueueCreate( 1, sizeof( Analog_meas_t ) );
    queue_MainToTelemetry = xQueueCreate( 1, sizeof( DataPackageRF_t ) );

    //----- Check queues -----------
    if(queue_AnalogToMain == 0)
    	ESP_LOGE(TAG, "Failed to create queue -> queue_AnalogToMain");

    xTaskCreatePinnedToCore(&task_kpptr_sysmgr, 	"task_kpptr_sysmgr", 	1024*4, NULL, configMAX_PRIORITIES - 12, NULL, ESP_CORE_0);
    xTaskCreatePinnedToCore(&task_kpptr_utils, 		"task_kpptr_utils", 	1024*4, NULL, configMAX_PRIORITIES - 10, NULL, ESP_CORE_0);
    xTaskCreatePinnedToCore(&task_kpptr_analog, 	"task_kpptr_analog", 	1024*4, NULL, configMAX_PRIORITIES - 11, NULL, ESP_CORE_0);
    xTaskCreatePinnedToCore(&task_kpptr_storage,	"task_kpptr_storage",   1024*4, NULL, configMAX_PRIORITIES - 3,  NULL, ESP_CORE_0);
    //xTaskCreatePinnedToCore(&task_kpptr_telemetry,	"task_kpptr_telemetry", 1024*4, NULL, configMAX_PRIORITIES - 4,  NULL, ESP_CORE_0);
    vTaskDelay(pdMS_TO_TICKS( 40 ));
    xTaskCreatePinnedToCore(&task_kpptr_main,		"task_kpptr_main",      1024*4, NULL, configMAX_PRIORITIES - 1,  NULL, ESP_CORE_1);



    while (true) {
        vTaskDelay(pdMS_TO_TICKS( 1000 ));
    }
}

