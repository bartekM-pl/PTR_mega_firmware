#pragma once

#include "esp_types.h"
#include "esp_event.h"
#include "esp_err.h"
#include "driver/uart.h"
#include "freertos/message_buffer.h"
#include "BOARD.h"


#define GPS_MAX_SATELLITES_IN_USE (12)
#define GPS_MAX_SATELLITES_IN_VIEW (16)
#define NMEA_MAX_STATEMENT_ITEM_LENGTH 16
#define NMEA_EVENT_LOOP_QUEUE_SIZE 16
#define TIME_ZONE (+1)   //Warsaw Time
#define YEAR_BASE (2000) //date in GPS starts from 2000

#define NMEA_PARSER_RUNTIME_BUFFER_SIZE (CONFIG_NMEA_PARSER_RING_BUFFER_SIZE / 2)

#define NMEA_TX_BUFFER_SIZE (CONFIG_NMEA_PARSER_RING_BUFFER_SIZE / 2)

typedef enum {
    GPS_MODE_NORMAL = 0,
	GPS_MODE_FITNESS = 1,
	GPS_MODE_AVIATION = 2,
	GPS_MODE_BALLON = 3,
	GPS_MODE_STATIONARY = 4
} gps_nav_mode_t;

/**
 * @brief Declare of NMEA Parser Event base
 *
 */
ESP_EVENT_DECLARE_BASE(ESP_NMEA_EVENT);

/**
 * @brief GPS fix type
 *
 */
typedef enum {
    GPS_FIX_INVALID, /*!< Not fixed */
    GPS_FIX_GPS,     /*!< GPS */
    GPS_FIX_DGPS,    /*!< Differential GPS */
} gps_fix_t;

/**
 * @brief GPS fix mode
 *
 */
typedef enum {
    GPS_MODE_INVALID = 1, /*!< Not fixed */
    GPS_MODE_2D,          /*!< 2D GPS */
    GPS_MODE_3D           /*!< 3D GPS */
} gps_fix_mode_t;

/**
 * @brief GPS satellite information
 *
 */
typedef struct {
    uint8_t num;       /*!< Satellite number */
    uint8_t elevation; /*!< Satellite elevation */
    uint16_t azimuth;  /*!< Satellite azimuth */
    uint8_t snr;       /*!< Satellite signal noise ratio */
} gps_satellite_t;

/**
 * @brief GPS time
 *
 */
typedef struct {
    uint8_t hour;      /*!< Hour */
    uint8_t minute;    /*!< Minute */
    uint8_t second;    /*!< Second */
    uint16_t thousand; /*!< Thousand */
} gps_time_t;

/**
 * @brief GPS date
 *
 */
typedef struct {
    uint8_t day;   /*!< Day (start from 1) */
    uint8_t month; /*!< Month (start from 1) */
    uint16_t year; /*!< Year (start from 2000) */
} gps_date_t;

/**
 * @brief NMEA Statement
 *
 */
typedef enum {
    STATEMENT_UNKNOWN = 0, /*!< Unknown statement */
    STATEMENT_GGA,         /*!< GGA */
    STATEMENT_GSA,         /*!< GSA */
    STATEMENT_RMC,         /*!< RMC */
    STATEMENT_GSV,         /*!< GSV */
    STATEMENT_GLL,         /*!< GLL */
    STATEMENT_VTG          /*!< VTG */
} nmea_statement_t;

/**
 * @brief GPS object
 *
 */
typedef struct {
#if (CONFIG_NMEA_STATEMENT_GGA || CONFIG_NMEA_STATEMENT_GLL)
	float latitude;                                                /*!< Latitude (degrees) */
    float longitude;                                               /*!< Longitude (degrees) */
    gps_time_t tim;                                               /*!< time in UTC */
#endif

#if (CONFIG_NMEA_STATEMENT_GGA)
    float altitude;                                                /*!< Altitude (meters) */
    gps_fix_t fix;                                                 /*!< Fix status */
    uint8_t sats_in_use;                                           /*!< Number of satellites in use */
#endif

#if (CONFIG_NMEA_STATEMENT_GGA || CONFIG_NMEA_STATEMENT_GSA)
    float dop_h;                                                   /*!< Horizontal dilution of precision */
#endif

#if (CONFIG_NMEA_STATEMENT_GSA)
    gps_fix_mode_t fix_mode;                                       /*!< Fix mode */
    uint8_t sats_id_in_use[GPS_MAX_SATELLITES_IN_USE];             /*!< ID list of satellite in use */
    float dop_p;                                                   /*!< Position dilution of precision  */
    float dop_v;                                                   /*!< Vertical dilution of precision  */
#endif

#if (CONFIG_NMEA_STATEMENT_GSV)
    uint8_t sats_in_view;                                          /*!< Number of satellites in view */
    gps_satellite_t sats_desc_in_view[GPS_MAX_SATELLITES_IN_VIEW]; /*!< Information of satellites in view */
#endif

#if (CONFIG_NMEA_STATEMENT_RMC)
    gps_date_t date;                                               /*!< Fix date */
    bool valid;                                                    /*!< GPS validity */
    float speed;                                                   /*!< Ground speed, unit: m/s */
#endif
#if (CONFIG_NMEA_STATEMENT_RMC || CONFIG_NMEA_STATEMENT_VTG)
    float cog;                                                     /*!< Course over ground */
    float variation;                                               /*!< Magnetic variation */
#endif
} gps_t;

/**
 * @brief Configuration of NMEA Parser
 *
 */
typedef struct {
    struct {
        uart_port_t uart_port;        /*!< UART port number */
        uint32_t rx_pin;              /*!< UART Rx Pin number */
        uint32_t baud_rate;           /*!< UART baud rate */
        uart_word_length_t data_bits; /*!< UART data bits length */
        uart_parity_t parity;         /*!< UART parity */
        uart_stop_bits_t stop_bits;   /*!< UART stop bits length */
        uint32_t event_queue_size;    /*!< UART event queue size */
    } uart;                           /*!< UART specific configuration */
} nmea_parser_config_t;

/**
 * @brief NMEA Parser Handle
 *
 */
typedef void *nmea_parser_handle_t;

typedef struct {
    uint8_t item_pos;                              /*!< Current position in item */
    uint8_t item_num;                              /*!< Current item number */
    uint8_t asterisk;                              /*!< Asterisk detected flag */
    uint8_t crc;                                   /*!< Calculated CRC value */
    uint8_t parsed_statement;                      /*!< OR'd of statements that have been parsed */
    uint8_t sat_num;                               /*!< Satellite number */
    uint8_t sat_count;                             /*!< Satellite count */
    uint8_t cur_statement;                         /*!< Current statement ID */
    uint32_t all_statements;                       /*!< All statements mask */
    char item_str[NMEA_MAX_STATEMENT_ITEM_LENGTH]; /*!< Current item */
    gps_t parent;                                  /*!< Parent class */
    uart_port_t uart_port;                         /*!< Uart port number */
    uint8_t *buffer;                               /*!< Runtime buffer */
    esp_event_loop_handle_t event_loop_hdl;        /*!< Event loop handle */
    TaskHandle_t tsk_hdl;                          /*!< NMEA Parser task handle */
    QueueHandle_t event_queue;                     /*!< UART event queue handle */
} esp_gps_t;

/**
 * @brief Default configuration for NMEA Parser
 *
 */
#define NMEA_PARSER_CONFIG_DEFAULT()              \
    {                                             \
        .uart = {                                 \
            .uart_port = GNSS_UART,              \
            .rx_pin = GNSS_TX_PIN, \
            .baud_rate = 9600,                    \
            .data_bits = UART_DATA_8_BITS,        \
            .parity = UART_PARITY_DISABLE,        \
            .stop_bits = UART_STOP_BITS_1,        \
            .event_queue_size = 16                \
        }                                         \
    }

/**
 * @brief NMEA Parser Event ID
 *
 */
typedef enum {
    GPS_UPDATE, /*!< GPS information has been updated */
    GPS_UNKNOWN /*!< Unknown statements detected */
} nmea_event_id_t;

/**
 * @brief Init NMEA Parser
 *
 * @param config Configuration of NMEA Parser
 * @return nmea_parser_handle_t handle of NMEA parser
 */
nmea_parser_handle_t nmea_parser_init(const nmea_parser_config_t *config);

/**
 * @brief Deinit NMEA Parser
 *
 * @param nmea_hdl handle of NMEA parser
 * @return esp_err_t ESP_OK on success, ESP_FAIL on error
 */
esp_err_t nmea_parser_deinit(nmea_parser_handle_t nmea_hdl);

/**
 * @brief Add user defined handler for NMEA parser
 *
 * @param nmea_hdl handle of NMEA parser
 * @param event_handler user defined event handler
 * @param handler_args handler specific arguments
 * @return esp_err_t
 *  - ESP_OK: Success
 *  - ESP_ERR_NO_MEM: Cannot allocate memory for the handler
 *  - ESP_ERR_INVALIG_ARG: Invalid combination of event base and event id
 *  - Others: Fail
 */
esp_err_t nmea_parser_add_handler(nmea_parser_handle_t nmea_hdl, esp_event_handler_t event_handler, void *handler_args);

/**
 * @brief Remove user defined handler for NMEA parser
 *
 * @param nmea_hdl handle of NMEA parser
 * @param event_handler user defined event handler
 * @return esp_err_t
 *  - ESP_OK: Success
 *  - ESP_ERR_INVALIG_ARG: Invalid combination of event base and event id
 *  - Others: Fail
 */
esp_err_t nmea_parser_remove_handler(nmea_parser_handle_t nmea_hdl, esp_event_handler_t event_handler);

/**
 * @brief Initialize GPS task
 *
 * @return esp_err_t
 *  - ESP_OK: Success
 *  - ESP_FAIL: Fail
 */
esp_err_t GPS_init();

/**
 * @brief Check size of GPS message received 
 *
 * @return uint8_t
 *  - size of GPS message struct
 */
uint8_t GNSS_message_size(void);

/**
 * @brief Get GPS positional data
 *
 * @param data received data container
 * @param ms waiting for receive timeout
 * @return uint32_t GPS data
 */
uint32_t GPS_getData(gps_t * data, uint16_t ms); 

/**
 * @brief TODO
 *
 */
esp_err_t GPS_checkStatus();

/**
 * @brief Test if GPS is responding by sending command to release software information
 *
 */
void GPS_test(void);

/**
 * @brief Set GPS baud rate
 *
 * @param baud desired baudrate, default:9600, 4800, 9600, 14400, 19200, 38400, 57600, 115200
 * @return esp_err_t
 *  - ESP_OK: Success
 *  - ESP_FAIL: Fail
 */
esp_err_t GPS_baud_rate_set(uint32_t baud);

/**
 * @brief Set GPS baud rate and ESP UART baudrate to same value
 *
 * @param baud desired baudrate, default:9600, 4800, 9600, 14400, 19200, 38400, 57600, 115200
 * @return esp_err_t
 *  - ESP_OK: Success
 *  - ESP_FAIL: Fail
 */
esp_err_t GPS_baud_rate_set_extra(uint32_t baud);

/**
 * @brief Set GPS fix interval
 *
 * @param time time in millis 100-10000
 */
void GPS_fix_interval_set(uint16_t time);

/**
 * @brief Set navigation mode for your specific use case
 *
 * @param mode mode of GPS to use 
 */
void GPS_nav_mode_set(gps_nav_mode_t mode);

/**
 * @brief Set GPS fix interval for specified messages
 *
 * @param GLL Geographic position – latitude and longitude
 * @param RMC Recommended minimum specific GPS/Transit data
 * @param VTG Course Over Ground and Ground Speed
 * @param GGA Global Positioning System Fix Data
 * @param GSA GNSS DOP and Active Satellites
 * @param GSV GNSS Satellites in View
 */
void GPS_nmea_output_set(uint8_t GLL, uint8_t RMC, uint8_t VTG, uint8_t GGA, uint8_t GSA, uint8_t GSV);
