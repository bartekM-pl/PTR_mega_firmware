#include "LSM6DSO32_driver.h"

static const char *TAG = "LSM6DSO32";

esp_err_t LSM6DSO32_Write(uint8_t address, uint8_t val);
esp_err_t LSM6DSO32_Read (uint8_t address, uint8_t * rx, uint8_t length);

LSM6DSO32_t LSM6DSO32_d;
static spi_dev_handle_t spi_dev_handle_LSM6DSO32;


esp_err_t LSM6DSO32_SPIinit(){
	if(SPI_checkInit() != ESP_OK){
		ESP_LOGE(TAG, "SPI controller not initialized! Use SPI_init() in main.c");
		return ESP_ERR_INVALID_STATE;
	}

	/* CONFIGURE SPI DEVICE */
	/* Max SCK frequency - 10MHz */
	ESP_RETURN_ON_ERROR(SPI_registerDevice(&spi_dev_handle_LSM6DSO32, SPI_SLAVE_LSM6DSO32_PIN,
												SPI_SCK_10MHZ, 1, 1, 7), TAG, "SPI register failed");

	return ESP_OK;
}

esp_err_t LSM6DSO32_init(){
	LSM6DSO32_SPIinit();
	LSM6DSO32_Write(LSM6DS_CTRL1_XL, LSM6DS_CTRL1_XL_ACC_RATE_104_HZ | LSM6DS_CTRL1_XL_ACC_FS_32G | LSM6DS_CTRL1_ACC_LPF2_EN);
	LSM6DSO32_Write(LSM6DS_CTRL2_G,  LSM6DS_CTRL2_G_GYRO_RATE_104_HZ | LSM6DS_CTRL2_G_GYRO_FS_1000_DPS);
	LSM6DSO32_Write(LSM6DS_CTRL3_C,  LSM6DS_CTRL3_BDU | LSM6DS_CTRL3_INT_PP | LSM6DS_CTRL3_INT_H | LSM6DS_CTRL3_INC);
	LSM6DSO32_Write(LSM6DS_CTRL4_C,  LSM6DS_CTRL4_INT12_SEP | LSM6DS_CTRL4_I2C_DIS | LSM6DS_CTRL4_GYRO_LPF1_EN);
	LSM6DSO32_Write(LSM6DS_CTRL5_C,  LSM6DS_CTRL5_ACC_ULP_DIS | LSM6DS_CTRL5_ROUNDING_DIS | LSM6DS_CTRL5_GYRO_ST_DIS | LSM6DS_CTRL5_ACC_ST_DIS);
	LSM6DSO32_Write(LSM6DS_CTRL6_C,  LSM6DS_CTRL6_GYRO_LPF1_0);
	LSM6DSO32_Write(LSM6DS_CTRL7_G, 0);	//default
	LSM6DSO32_Write(LSM6DS_CTRL8_XL, LSM6DS_CTRL8_ACC_LPF | LSM6DS_CTRL8_FILTER_ODR_4);

	return ESP_OK;
}

uint8_t LSM6DSO32_WhoAmI(){
	uint8_t rxBuff[2] = {0U};
	LSM6DSO32_Read(0x0F, rxBuff, 1);

	printf("ID: 0x%x\n", rxBuff[1]);

	return rxBuff[1];
}

esp_err_t LSM6DSO32_readMeas(){
	LSM6DSO32_Read(0x20, LSM6DSO32_d.raw, 14);

	LSM6DSO32_d.meas.accX  = (LSM6DSO32_d.accX_raw)*(64.0f/65536.0f) - LSM6DSO32_d.accXoffset;
	LSM6DSO32_d.meas.accY  = (LSM6DSO32_d.accY_raw)*(64.0f/65536.0f) - LSM6DSO32_d.accYoffset;
	LSM6DSO32_d.meas.accZ  = (LSM6DSO32_d.accZ_raw)*(64.0f/65536.0f) - LSM6DSO32_d.accZoffset;

	LSM6DSO32_d.meas.gyroX = (LSM6DSO32_d.gyroX_raw)*(0.035f) - LSM6DSO32_d.gyroXoffset;
	LSM6DSO32_d.meas.gyroY = (LSM6DSO32_d.gyroY_raw)*(0.035f) - LSM6DSO32_d.gyroYoffset;
	LSM6DSO32_d.meas.gyroZ = (LSM6DSO32_d.gyroZ_raw)*(0.035f) - LSM6DSO32_d.gyroZoffset;

	LSM6DSO32_d.meas.temp  = (LSM6DSO32_d.temp_raw)*(0.00390625f) + 25.0f;

	return ESP_OK;
}

esp_err_t LSM6DSO32_getMeas(LSM6DS_meas_t * meas){
	*meas = LSM6DSO32_d.meas;

	return ESP_OK;
}

esp_err_t LSM6DSO32_Write(uint8_t address, uint8_t val){
	return SPI_transfer(spi_dev_handle_LSM6DSO32, 0, address, &val, NULL, 1);
}

esp_err_t LSM6DSO32_Read(uint8_t address, uint8_t * rx, uint8_t length){
	return SPI_transfer(spi_dev_handle_LSM6DSO32, 1, address, NULL, rx, length);
}
