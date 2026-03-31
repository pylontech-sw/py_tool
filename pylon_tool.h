/****************************************************************************
*
* Copyright (c) 2025, Pylontech Inc. All rights reserved.
*
*---------------------------------------------------------------------------
* Filename: pylon_tool.h
* Description:
* Author:yangjb
* Version:
* Created Date:
* Change Log:
* Date By Version Change Description
* =========================================================================
*
*---------------------------------------------------------------------------
*
*****************************************************************************/
#ifndef __PYLON_TOOL_H__
#define __PYLON_TOOL_H__

#define PY_APP_VER              v1.2
#define PY_PRODUCT_ID           0xB009   // 产品ID
#define PY_BLOCK_SIZE           128      // 固定分块大小
#define PY_TIMEOUT_MS           3000     // 默认超时3秒
#define PY_NODE_ID_DEFAULT      0x01     // 默认节点地址
#define PY_TRY_TIMES            5        // 默认重试次数

/* ==================== 协议定义 (严格按照派能文档) ==================== */
typedef enum {
    /* 标准消息 (Standard Messages) */
    PY_STD_OPERATION_LIMIT      = 0x351,  // 运行限制
    PY_STD_SOC_SOH              = 0x355,  // SOC和SOH状态
    PY_STD_ANALOG_QUANTITY      = 0x356,  // 模拟量
    PY_STD_PROTECT_ALARM        = 0x359,  // 保护和告警
    PY_STD_BMS_REQUEST          = 0x35C,  // BMS请求
    PY_STD_BRAND                = 0x35E,  // 品牌信息
    
    /* 控制消息 (Control Messages) */
    PY_CTRL_TURN_OFF            = 0x4800, // 关机命令
    PY_CTRL_HEARTBEAT           = 0x1001, // 心跳信号
    PY_CTRL_PROTOCOL_CHANGE1    = 0x0020, // 协议变更
    PY_CTRL_PROTOCOL_CHANGE2    = 0x0060, // 协议变更
    
    /* 扩展系统信息 (Extended System Level) */
    PY_EXT_SYSTEM_INFO          = 0x35A,  // 系统信息
    PY_EXT_MODULE_STATUS        = 0x372,  // 模块状态
    PY_EXT_MAX_MIN_ANALOG       = 0x373,  // 最大最小模拟量
    PY_EXT_MODULE_ADDR_1        = 0x374,  // 模块地址1 (0x373中的模块)
    PY_EXT_MODULE_ADDR_2        = 0x375,  // 模块地址2
    PY_EXT_MODULE_ADDR_3        = 0x376,  // 模块地址3
    PY_EXT_MODULE_ADDR_4        = 0x377,  // 模块地址4
    PY_EXT_TOTAL_CAPACITY       = 0x379,  // 总容量
    PY_EXT_BATTERY_FAMILY       = 0x382,  // 电池系列名称
    
    /* 模块告警信息 */
    PY_EXT_ALARM_MOD1_ALARM     = 0x37A,  // 第一个告警模块的告警状态
    PY_EXT_ALARM_MOD1_ID        = 0x37B,  // 第一个告警模块的ID
    PY_EXT_ALARM_MOD2_ALARM     = 0x37C,  // 第二个告警模块的告警状态
    PY_EXT_ALARM_MOD2_ID        = 0x37D,  // 第二个告警模块的ID
    PY_EXT_ALARM_MOD3_ALARM     = 0x37E,  // 第三个告警模块的告警状态
    PY_EXT_ALARM_MOD3_ID        = 0x37F,  // 第三个告警模块的ID
    
    /* 扩展系统信息2 */
    PY_EXT2_DEVICE_NAME         = 0x382,  // 设备名称

    /* 扩展系统信息3 */
    PY_EXT3_SYSTEM_INFO         = 0x3A0,  // 系统信息
    PY_EXT3_DAILY_CAPACITY      = 0x3A1,  // 日充放电容量
    PY_EXT3_ACCUM_CAPACITY      = 0x3A2,  // 累计充放电容量
    PY_EXT3_HEATING             = 0x3A4,  // 加热状态
    PY_EXT3_EMERGENCY_STOP      = 0x3A5,  // 紧急停止请求标志
    PY_EXT3_BMS_PROTECT_INFO    = 0x3A6,  // BMS保护信息
} PylontechCanId;

typedef enum {
    PY_CMD_FIRMWARE_SIZE     = 0x4610,      // 固件大小通知
    PY_CMD_BLOCK_NUMBER      = 0x4630,      // 分块序号通知
    PY_CMD_BLOCK_DATA        = 0x4650,      // 分块数据传输
    PY_CMD_BLOCK_CRC         = 0x4670,      // 分块CRC校验
    PY_CMD_FIRMWARE_CRC      = 0x4690,      // 整包CRC校验
    PY_CMD_RESTART           = 0x46B0,      // 重启升级命令
    PY_CMD_CHECK_STATUS      = 0x46D0,      // 状态查询命令
    PY_CMD_SN_VER            = 0x5000000,   // sn,ver查询命令
    PY_CMD_EXT1              = 0x305,       // 增补1
    PY_CMD_EXT2              = 0x307,       // 增补2
} PylontechCmdType;

typedef enum {
    PY_RESP_NONE             = 0x0000,      // 无响应
    PY_RESP_FIRMWARE_SIZE    = 0x4620,      // 固件大小响应
    PY_RESP_BLOCK_STATUS     = 0x4680,      // 分块状态响应
    PY_RESP_FIRMWARE_CRC     = 0x46A0,      // CRC校验响应
    PY_RESP_RESTART          = 0x46C0,      // 重启响应
    PY_RESP_STATUS           = 0x46E0,      // 状态响应
    PY_RESP_SN1              = 0x05010101,  // SN查询响应1
    PY_RESP_SN2              = 0x05010102,  // SN查询响应1
    PY_RESP_VER              = 0x05010105,  // 版本查询响应
} PylontechRespType;

typedef enum {
    /* 成功状态 */
    PY_STATUS_SIZE_OK        = 0xA1,
    PY_STATUS_BLOCK_OK       = 0xA2,
    PY_STATUS_CRC_OK         = 0xA3,
    
    /* 错误状态 */
    PY_ERR_SIZE_INVALID      = 0x01,
    PY_ERR_BLOCK_CRC         = 0x02,
    PY_ERR_BLOCK_NUMBER      = 0x03,
    PY_ERR_BLOCK_WRITE       = 0x04,
    PY_ERR_BLOCK_SIZE        = 0x05,
    PY_ERR_CRC_DATA_WRITE    = 0x06,
    PY_ERR_FIRMWARE_SIZE     = 0x07,
    PY_ERR_CRC_MISMATCH      = 0x08,
    PY_ERR_SEQUENCE          = 0x13,
    PY_ERR_CRC_STORE         = 0x14,
    PY_ERR_NO_CONDITIONS     = 0x15,
    
    /* 重启更新状态回复 */
    PY_RESTART_INVALID       = 0x09,
    PY_RESTART_CFM_TRANS     = 0x0A,
    PY_RESTART_CFM_UPGRADE   = 0x0B,

    /* 升级状态 */
    PY_UPGRADE_TRANSFERRING  = 0x0C,
    PY_UPGRADE_SUCCESS       = 0x0D,
    PY_UPGRADE_TRANS_ERROR   = 0x0E,
    PY_UPGRADE_FAILED        = 0x0F,
    PY_UPGRADE_UPGRADING     = 0x10,
    PY_UPGRADE_CANNOT_UP     = 0x11,
    PY_UPGRADE_VER_MISMATCH  = 0x12,
    PY_UPGRADE_CMD_MISMATCH  = 0x13,
} PylontechStatusCode;

// 错误码定义
typedef enum {
    PYLON_SUCCESS = 0,                  // 成功
    PYLON_ERROR_GENERAL = 1,            // 通用错误
    PYLON_ERROR_CAN_INIT = 2,           // CAN初始化失败
    PYLON_ERROR_CAN_IO = 3,             // CAN通信错误
    PYLON_ERROR_TIMEOUT = 4,            // 超时错误
    PYLON_ERROR_FIRMWARE = 5,           // 固件相关错误
    PYLON_ERROR_PARAM = 6,              // 参数错误
    PYLON_ERROR_NO_DEVICE = 7,          // 未找到设备
    PYLON_ERROR_MEMORY = 8,             // 内存错误
    PYLON_ERROR_FILE = 9,               // 文件错误
    PYLON_ERROR_CHECK_FAILED = 10,      // 升级检查失败
    PYLON_ERROR_CHECK_TIMEOUT = 11,     // 升级检查超时
} PylonErrorCode;

typedef struct {
    int sockfd;
    uint8_t node_id;
    char ifname[IFNAMSIZ];
} CanHandle;

typedef struct {
    char name[17];
    char sn[17];
    char ver[8];
    uint8_t flagName;
    uint8_t flagVer;
    uint8_t flagSN[2];
} DevInfo;

#endif /* __PYLON_TOOL_H__ */