/*!
 * @file DFRobot_C4002.h
 * @brief Define basic struct of DFRobot_C4002 class
 * @details This is the header file for the DFRobot_C4002 class. It contains the declaration of the class and its members.
 * @copyright	Copyright (c) 2025 DFRobot Co.Ltd (http://www.dfrobot.com)
 * @license The MIT License (MIT)
 * @author JiaLi(zhixin.liu@dfrobot.com)
 * @version V1.0
 * @date 2025-11-04
 * @url https://github.com/DFRobot/DFRobot_C4002
 */
#ifndef __DFROBOT_C4002_H__
#define __DFROBOT_C4002_H__

#include <Arduino.h>

#if defined(ESP8266) || defined(ARDUINO_AVR_UNO)
#include <SoftwareSerial.h>
#else
#include <HardwareSerial.h>
#endif

//#define ENABLE_DEBUG
#ifdef ENABLE_DEBUG
#define DBG(...)                 \
  {                              \
    Serial.print("[");           \
    Serial.print(__FUNCTION__);  \
    Serial.print("(): ");        \
    Serial.print(__LINE__);      \
    Serial.print(" ] ");         \
    Serial.println(__VA_ARGS__); \
  }
#else
#define DBG(...)
#endif

#define TIME_OUT      0x64    ///< time out
#define FRAME_HEADER1 0xFA    ///< frame header1
#define FRAME_HEADER2 0xF5    ///< frame header2
#define FRAME_HEADER3 0xAA    ///< frame header3
#define FRAME_HEADER4 0xA5    ///< frame header4

#define FRAME_TYPE_WRITE_REQUSET 0x00    ///< write request frame type
#define FRAME_TYPE_READ_REQUSET  0x01    ///< read request frame type
#define FRAME_TYPE_WRITE_RESPOND 0x02    ///< write respond frame type
#define FRAME_TYPE_READ_RESPOND  0x03    ///< read respond frame type
#define FRAME_TYPE_NOTIFICATION  0x04    ///< notification frame type
#define FRAME_ERROR              0xFF    ///< error frame type

#define CMD_SET_LED_MODE                0xA1    ///< set led mode
#define CMD_CONFIG_OUT_MODE             0xA0    ///< set output mode
#define CMD_ENVIRNMENT_CALIBRATION      0x60    ///< environment calibration
#define CMD_RESTART                     0x00    ///< restart command
#define CMD_SET_DETECT_RANGE            0x86    ///< set detect range command
#define CMD_FACTORY_RESET               0x80    ///< factory reset command
#define CMD_FACTORY_RESET_USER          0x02    ///< factory reset user command
#define CMD_SET_REPORT_PERIOD           0x83    ///< set report period
#define CMD_SET_LIGHT_THRESHOLD         0x88    ///< set light threshold
#define CMD_SET_DISTANCE_DOOR           0x62    ///< set distance gate
#define CMD_GET_VERSION                 0x82    ///< get version command
#define CMD_GET_AND_SET_RESOLUTION_MODE 0x66    ///< get resolution mode command
#define CMD_SET_DISTANCE_DOOR_THRESHOLD 0x63    ///< set distance gate threshold
#define CMD_SET_BAUDRATE                0x21    ///< set baudrate command
#define CMD_TARGET_DISAPPEAR_DELAY_TIME 0x84    ///< target disappear delay time
#define CMD_LOCK_TIME                   0x85    ///< lock time
#define CMD_THRESHOLD_GROUP             0x87    ///< threshold group command

#define NOTE_RESULT_CMD                 0x60    ///< detection result notification command
#define NOTE_ENVIRNMENT_CALIBRATION_CMD 0x03    ///< environment calibration notification command

#define SOFTWARE_VERSION 0x01    ///< get software version
#define HARDWARE_VERSION 0x00    ///< get hardware version

#define C4002_ENABLE  1
#define C4002_DISABLE 0

/**
 * @enum eResolutionMode_t
 * @brief Resolution mode
*/
typedef enum {
  eResolution80Cm = 0x00,
  eResolution20Cm = 0x01
} eResolutionMode_t;

/**
 * @enum eDistanceGateType_t
 * @brief Distance gate type
*/
typedef enum {
  eMotionDistGate   = 0x00,
  ePresenceDistGate = 0x01
} eDistanceGateType_t;

/**
 * @enum eResponseCode_t
 * @brief Response code
*/
typedef enum {
  eReadAndWriteReq   = 0x00, /*read and write request       */
  eSucceed           = 0x01,
  eCmdErr            = 0x02, /* The CMD does not presence      */
  eAuthenticationErr = 0x03, /* Authentication error        */
  eResourcesBusy     = 0x04, /* Resources are busy          */
  eParamsErr         = 0x05, /* The parameters are illegal  */
  eDataLenErr        = 0x06, /* Abnormal data length        */
  eInternalErr       = 0x07  /* internal error              */
} eResponseCode_t;

/**
 * @enum eOutpinMode_t
 * @brief Output pin mode
*/
typedef enum {
  eOutpinMode1 = 0x01, /* Only when motion is detected will a high level be output */
  eOutpinMode2 = 0x02, /* A high level is output only when its presence is detected */
  eOutpinMode3 = 0x03, /* A high level only appears when motion or presence is detected */
  eOutpinModex = 0xFF  /* reserved                          */
} eOutpinMode_t;

/**
 * @enum eMotionDirection_t
 * @brief The direction of the motion
*/
typedef enum {
  eAway        = 0,
  eNoDirection = 1,
  eApproaching = 2
} eMotionDirection_t;

/**
 * @enum eTargetState_t
 * @brief The state of the target
*/
typedef enum {
  eNoTarget           = 0,
  ePresence           = 1,
  eMotion             = 2,
  eMotionOrPresence   = 3,
  eMotionOrNoTarget   = 4,
  ePresenceOrNoTarget = 5,
  ePinError           = 255
} eTargetState_t;

/**
 * @enum eLedMode_t
 * @brief The operation led mode
*/
typedef enum {
  eLedOff  = 0x00,
  eLedOn   = 0x01,
  eLedKeep = 0xFF
} eLedMode_t;

/**
 * @enum eNoteType_t
 * @brief The type of the notification message
*/
typedef enum {
  eNoNote      = 0x00,
  eResult      = 0x01,
  eCalibration = 0x02,
} eNoteType_t;

/**
 * @enum eThreshGroup_t
 * @brief The group of the threshold
*/
typedef enum {
  eLowThreshGroup     = 0x00,
  eMidThreshGroup     = 0x01,
  eHighThreshGroup    = 0x02,
  eCustomThreshGroup  = 0x03,
  eCurrentThreshGroup = 0xFF,
  eThreshGroupError   = 0xFE
} eThreshGroup_t;

/**
 * @enum eBaudrate_t
 * @brief The baudrate
*/
typedef enum {
  eBaud57600   = 57600,
  eBaud115200  = 115200,
  eBaud230400  = 230400,
  eBaud460800  = 460800,
  eBaud500000  = 500000,
  eBaud921600  = 921600,
  eBaud1000000 = 1000000
} eBaudrate_t;

/**
 * @struct sDetectResult_t
 * @brief The detection result
*/
typedef struct {
  uint8_t  targetStatus;
  uint16_t light;
  uint32_t existDistIndex;
  uint16_t existCountDown;
  uint16_t existTargetDist;
  uint8_t  existTargetEnery;
  uint16_t moveTargetDist;
  int16_t  moveTargetSpeed;
  uint8_t  moveTargetEnery;
  uint8_t  moveTargetDirect;
} __attribute__((packed)) sDetectResult_t;   // PHYGITAL PATCH: "packed" added (not in upstream v1.0.0)

/**
 * @struct sDataHeader_t
 * @brief The data header of the received package
*/
typedef struct {
  uint8_t  cmd;
  uint8_t  respCode;
  uint16_t dataLen;
} sDataHeader_t;

/**
 * @struct sRecvPack_t
 * @brief The received package
*/
typedef struct {
  sDataHeader_t   dataHeader;
  uint8_t         data[50];
  uint8_t         packType;
  eResponseCode_t resPonCode;
} sRecvPack_t;

/**
 * @struct sResData_t
 * @brief The detection result and environment calibration information
*/
typedef struct {
  uint8_t         cmd;
  uint8_t         respCode;
  sDetectResult_t dectResult;
  uint16_t        calibCountdown;
} sResData_t;

/**
 * @struct sPresenceTarget_t
 * @brief The presence target
*/
typedef struct {
  float   distance;
  uint8_t energy;
} sPresenceTarget_t;

/**
 * @struct sMotionTarget_t
 * @brief The motion target
*/
typedef struct {
  float              distance;
  float              speed;
  uint8_t            energy;
  eMotionDirection_t direction;
} sMotionTarget_t;

/**
 *  @struct sRetResult_t
 *  @brief The detection result and environment calibration information
*/
typedef struct {
  eNoteType_t noteType;
  uint16_t    calibCountdown;
} sRetResult_t;

/**
 * @struct sDetectRange_t
 * @brief The detection distance range
*/
typedef struct {
  uint16_t closest;
  uint16_t farthest;
} sDetectRange_t;

/**
 * @struct sLedStatus_t
 * @brief The led status
*/
typedef struct {
  eLedMode_t runLed;
  eLedMode_t outLed;
} sLedStatus_t;

/**
 * @struct sConfigParams_t
 * @brief The configuration parameters
*/
typedef struct {
  float             curLightThreshold;
  sDetectRange_t    curDetectRange;
  eOutpinMode_t     curOutMode;
  eResolutionMode_t curResolutionMode;
  eThreshGroup_t    curMotionSensitivity;
  eThreshGroup_t    curPresenceSensitivity;
  uint16_t          curTargetDisappearDelayTime;
  sLedStatus_t      curLedStatus;
} sConfigParams_t;

class DFRobot_C4002 {
public:
#if defined(ESP8266) || defined(ARDUINO_AVR_UNO)
  DFRobot_C4002(SoftwareSerial *sSerial, uint32_t baud);
#else
  DFRobot_C4002(HardwareSerial *hSerial, uint32_t baud, uint8_t rxpin = 0, uint8_t txpin = 0);
#endif

  ~DFRobot_C4002() {};

  /**
 * @fn begin
 * @brief Initialize the serial port and set the output pin
 * @param outPin: The output pin, default is 255, which means no output pin is used.
 * @return true: Initialization succeeded, false: Initialization failed.
 */
  bool begin(uint8_t outPin = 255);

  /**
   * @fn setRunLedState
   * @brief Set the operation led of runing status
   * @param switching
   *      eLedOff: Turn off the operation LED.
   *      eLedOn : Turn on the operation LED.
   * @return true: Operation LED succeeded, false: Operation LED failed.
  */
  bool setRunLedState(eLedMode_t switching);

  /**
   * @fn setOutLedState
   * @brief Set the output led
   * @param switching
   * @n     eLedOff: Turn off the output LED.
   * @n     eLedOn : Turn on the output LED.
   * @return true: Output LED succeeded, false: Output LED failed.
  */
  bool setOutLedState(eLedMode_t switching);

  /**
   * @fn setOutPinMode
   * @brief Set the output pin mode
   * @param outMode
   * @n      eOutpinMode1: Only when motion is detected will a high level be output.
   * @n      eOutpinMode2: A high level is output only when its presence is detected.
   * @n      eOutpinMode3: A high level only appears when motion or presence is detected.
   * @return true: Set output pin mode succeeded, false: Set output pin mode failed.
  */
  bool setOutPinMode(eOutpinMode_t outMode);

  /**
   * @fn startEnvCalibration
   * @brief Start environment calibration
   * @param delayTime: Delay the time when the calibration starts to be executed, unit: s
   * @param contTime : The calibration time, unit: s
   * @return true: Start calibration succeeded, false: Start calibration failed.
  */
  void startEnvCalibration(uint16_t delayTime, uint16_t contTime);

  /**
   * @fn setDetectRange
   * @brief Set the detection distance range,range: 0-1100cm
   * @param closest : The closest distance to detect,   unit: cm
   * @param farthest: The farthest distance to detect,  unit: cm
   * @return true: Set detection range succeeded, false: Set detection range failed.
  */
  bool setDetectRange(uint16_t closest, uint16_t farthest);

  /**
   * @fn configureGate
   * @brief Configure the distance gate function
   * @param gateType
   * @n      eMotionDistGate : Use this parameter when the motion gate parameter is enabled
   * @n      ePresenceDistGate: Use this parameter when the presence gate parameter is enabled
   * @param gateData: An array of type uint8 t is needed, with parameters 0 and 1 representing disabling and enabling respectively
   * @return true: Configure gate succeeded, false: Configure gate failed.
  */
  bool configureGate(eDistanceGateType_t gateType, uint8_t *gateData);

  /**
   * @fn factoryReset
   * @brief Factory reset the device
   * @return true: Factory reset succeeded, false: Factory reset failed.
   */
  bool factoryReset(void);

  /**
   * @fn setResolutionMode
   * @brief Set the resolution mode
   * @param mode : The resolution mode,  eResolution80Cm: 80cm, eResolution20Cm: 20cm
   * @return true: Set resolution mode succeeded, false: Set resolution mode failed.
  */
  bool setResolutionMode(eResolutionMode_t mode);

  /**
   * @fn setReportPeriod
   * @brief Set the report period
   * @param period : The report period, range: 0-255, unit: 0.1s
   * @return true: Set report period succeeded, false: Set report period failed.
  */
  bool setReportPeriod(uint8_t period);

  /**
   * @fn setLightThresh
   * @brief Set the light threshold
   * @param threshold : The light threshold, range: 0-50, unit: lux
   * @n     Note: Note: When the valve group is set to 0, the target detection function will be triggered regardless of the light intensity.
   * @n     When the threshold is not 0, the target inspection function will only be activated when the light intensity is lower than the threshold; otherwise, it will not be activated
   * @return true: Set light threshold succeeded, false: Set light threshold failed.
   */
  bool setLightThresh(float threshold);

  /**
   * @fn setGateThresh
   * @brief Set the gate threshold
   * @param gateType
   * @n      eMotionDistGate  : Use this parameter when the motion gate parameter is enabled
   * @n      ePresenceDistGate: Use this parameter when the presence gate parameter is enabled
   * @param thresh : An array of type uint8 t is needed, with parameters 0-99 representing the threshold value of the gate
   * @return true: Set gate threshold succeeded, false: Set gate threshold failed.
  */
  bool setGateThresh(eDistanceGateType_t gateType, uint8_t *thresh);

  /**
   * @fn setBaudrate
   * @brief Set the baudrate of the serial port
   * @param baud : The baud rate type, eBaudrate_t
   * @n      eBaud57600   : 57600 bps
   * @n      eBaud115200  : 115200 bps
   * @n      eBaud230400  : 230400 bps
   * @n      eBaud460800  : 460800 bps
   * @n      eBaud500000  : 500000 bps
   * @n      eBaud921600  : 921600 bps
   * @n      eBaud1000000 : 1000000 bps
   * @n     Note: The baud rate should not be too high; otherwise, it will lead to data loss
   * @n     It takes effect after a successful restart
   * @return true: Set baudrate succeeded, false: Set baudrate failed.
  */
  bool setBaudrate(eBaudrate_t baud);

  /**
   * @fn getNoteInfo
   * @brief Get the detection result and environment calibration information
   * @return  sRetResult_t
   * @n           noteType      : The type of the notification message, eResult: detection result notification message, eCalibration: environment calibration notification message.
   * @n           calibCountdown: The remaining time of the environment calibration, unit: s.
  */
  sRetResult_t getNoteInfo(void);

  /**
   * @fn getTargetState
   * @brief Get the current state of the target
   * @return eTargetState_t
   * @n          eNoTarget        : No target is detected.
   * @n          ePresence        : The target is detected.
   * @n          eMotion          : The target is motion.
  */
  eTargetState_t getTargetState(void);

  /**
   * @fn getLightIntensity
   * @brief Get the current light intensity
   * @return The current light intensity, unit: lux.
  */
  float getLightIntensity(void);

  /**
   * @fn getPresenceGateIndex
   * @brief Get the index of the detected target
   * @n     Note: When the resolution mode is 80cm, 0 to 15 bits may be set to 1 to represent the presence of the target.
   * @n     When the resolution mode is 20cm, 0 to 25 bits may be set to 1 to represent the presence of the target.
   * @return The index of the detected target, range: 0-0xFFFFFFFF.
  */
  uint32_t getPresenceGateIndex(void);

  /**
   * @fn getPresenceTargetInfo
   * @brief Get the information of the detected target
   * @return sPresenceTarget_t
   * @n        distance: The distance of the detected target, unit: m.
   * @n        energy  : The energy of the detected target, range:0-99.
  */
  sPresenceTarget_t getPresenceTargetInfo(void);

  /**
   * @fn getMotionTargetInfo
   * @brief Get the information of the detected target
   * @return sMotionTarget_t
   * @n        distance : The distance of the detected target, unit: m.
   * @n        speed    : The speed of the detected target, unit: m/s.
   * @n        energy   : The energy of the detected target, range:0-99.
   * @n        direction: The direction of the detected target, eMotionDirection_t.
   */
  sMotionTarget_t getMotionTargetInfo(void);

  /**
   * @fn getOutTargetState
   * @brief Get the current state of the output target
   * @return eTargetState_t
   * @n          eNoTarget            : No output target is detected.
   * @n          ePresence            : The output target is detected.
   * @n          eMotion              : The output target is motion.
   * @n          eMotionOrPresence    : The output target is motion or detected.
   * @n          eMotionOrNoTarget    : The output target is motion or no output target is detected.
   * @n          ePresenceOrNoTarget  : The output target is detected or no output target is detected.
   * @n          ePinError            : The pin error occurred.
  */
  eTargetState_t getOutTargetState(void);

  /**
   * @fn setLockTime
   * @brief Set the lock time. When changing from occupied to unoccupied, the detection function
   * @n   is locked for 1 second (default, adjustable). During the lock time, the sensor does not
   * @n   detect targets. Detection is allowed again after the lock time expires.
   * @param lockTime : The lock time, unit: s range:0.2-10s accuracy:0.1s
   * @return true: Set lock time succeeded, false: Set lock time failed.
  */
  bool setLockTime(float lockTime);

  /**
   * @fn setTargetDisappearDelay
   * @brief Set the delay time for the target to disappear after it is no longer detected
   * @param delayTime : The delay time, unit: s (0-65535s)
   * @return true: Set target disappear delay succeeded, false: Set target disappear delay failed.
  */
  bool setTargetDisappearDelay(uint16_t delayTime);

  /**
   * @fn setSensitivity
   * @brief Set the sensitivity of the detection
   * @param sensitivity : The sensitivity
   * @return true: Set sensitivity succeeded, false: Set sensitivity failed.
  */
  bool setSensitivity(eDistanceGateType_t gateType, eThreshGroup_t sensitivity);

  /**
   * @fn getLightThresh
   * @brief Get the light threshold
   * @return The light threshold, unit: lux.
  */
  float getLightThresh(void);

  /**
   * @fn getDetectRange
   * @brief Get the distance gate threshold
   * @return sDetectRange_t
   * @n        closest : The closest distance to detect, unit: cm,range: 0-1100cm.
   * @n        farthest: The farthest distance to detect, unit: cm,range: 0-1100cm.
  */
  sDetectRange_t getDetectRange(void);

  /**
   * @fn getTargetDisappearDelay
   * @brief Get the delay time for the target to disappear after it is no longer detected
   * @return The delay time, unit: s.
  */
  uint16_t getTargetDisappearDelay(void);

  /**
   * @fn getOutPinMode
   * @brief Get the output pin mode
   * @return eOutpinMode_t
   * @n          eOutpinMode1: Only when motion is detected will a high level be output.
   * @n          eOutpinMode2: A high level is output only when its presence is detected.
   * @n          eOutpinMode3: A high level only appears when motion or presence is detected.
  */
  eOutpinMode_t getOutPinMode(void);

  /**
   * @fn getResolutionMode
   * @brief Get the resolution mode
   * @return eResolutionMode_t
   * @n          eResolution80Cm: 80cm
   * @n          eResolution20Cm: 20cm
  */
  eResolutionMode_t getResolutionMode(void);

  /**
   * @fn getSensitivity
   * @brief Get the sensitivity of the detection
   * @param gateType
   * @n      eMotionDistGate : Use this parameter when the motion gate parameter is enabled
   * @n      ePresenceDistGate: Use this parameter when the presence gate parameter is enabled
   * @return eThreshGroup_t
  */
  eThreshGroup_t getSensitivity(eDistanceGateType_t gateType);

  /**
   * @fn getAllConfigParams
   * @brief Get all the configuration parameters of the device
   * @return sConfigParams_t
   * @n        curLightThreshold        : The light threshold, unit: lux.
   * @n        curDetectRange           : The detection distance range, unit: cm, range: 0-1100cm.
   * @n        curOutMode               : The output mode, eOutpinMode_t.
   * @n        curResolutionMode        : The resolution mode, eResolutionMode_t.
   * @n        curMotionSensitivity     : The sensitivity of the motion detection, eThreshGroup_t.
   * @n        curPresenceSensitivity   : The sensitivity of the presence detection, eThreshGroup_t.
   * @n        curTargetDisappearDelay  : The delay time for the target to disappear after it is no longer detected, unit: s.
   * @n        curLedStatus             : The led status, eLedMode_t.
  */
  sConfigParams_t getAllConfigParams(void);

  /**
   * @fn getPresenceCountDown
   * @brief Get the remaining time after target disappears, state changes from presence to no target
   * @return The remaining time, unit: s.
  */
  uint16_t getPresenceCountDown(void);

  /**
   * @fn getDistanceGateThresh
   * @brief Get the distance gate threshold
   * @param gateType
   * @n      eMotionDistGate  : Use this parameter when the motion gate parameter is enabled
   * @n      ePresenceDistGate: Use this parameter when the presence gate parameter is enabled
   * @param gateData: An array of type uint8 t is needed, with parameters 0-99 representing the threshold value of the distance gate
   * @return true: Get distance gate threshold succeeded, false: Get distance gate threshold failed.
  */
  bool getDistanceGateThresh(eDistanceGateType_t gateType, uint8_t *gateData);

protected:
  String       getVersioninfo(uint8_t version);
  int8_t       restart(void);
  bool         setLed(eLedMode_t runLed, eLedMode_t outLed);
  sLedStatus_t getLedStatus(void);

  void        sendPack(void *pdata, uint16_t len, uint8_t msgType);
  sRecvPack_t recvPack();
  bool        checkSum(uint8_t *pdata, uint8_t len);
  uint16_t    getCheckSum(uint8_t *pdata, uint16_t len);
  void        writeReg(uint8_t reg, void *pdata, uint8_t len);
  int16_t     readReg(uint8_t reg, void *pdata, uint8_t len);

private:
#if defined(ESP8266) || defined(ARDUINO_AVR_UNO)
  SoftwareSerial *_serial;
#else
  HardwareSerial *_serial;
#endif

  uint32_t _baud;
  uint8_t  _rxpin;
  uint8_t  _txpin;
  uint8_t  _outpin;

  sDetectResult_t   _detectResult;
  eResolutionMode_t _resolutionMode = eResolution80Cm;
  eOutpinMode_t     _outMode;
};

#endif
