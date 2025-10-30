/*******************************************************************************
*
* @copyright (c) 2025, Pylon Technologies Co., Ltd. All rights reserved.
*
*-----------------------------------------------------------------------------------
* @file        pylon_tool.c
* @details
* @author      yang.jiubao
* @version     1.0
* @date        2025-07-14 18:24:04
* @par         Change Log Table:
* ================================================================================
* <table>
* <tr><th>Date        <th>Version  <th>Author        <th>Description
* <tr><td>2025-07-14  <td>1.0      <td>yang.jiubao   <td>创建初始版本
* </table>
*
*-----------------------------------------------------------------------------------
*
*******************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <time.h>
#include "pylon_tool.h"

int g_oldProtocol = 0; // 是否为老协议标志
// 输出Victron格式消息
static void vic_print_message(const char *type, const char *format, ...) {
    char buffer[256]; // 定义缓冲区存储格式化后的字符串
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    printf("<message type=\"%s\">%s</message>\n", type, buffer);
}

// 输出Victron格式进度（仅在进度变化>=1%时打印）
static void vic_print_progress(int percent) {
    static int last_percent = -1;  // 初始值
    // 只在百分比变化超过1%时打印
    if (percent != last_percent) {
        printf("<progress level=\"%d\"/>\n", percent);
        last_percent = percent;
    }
}

// Modbus CRC16计算
uint16_t calcCRC(uint8_t* Buffer, uint16_t u16length) {
    unsigned int temp, temp2, flag;
    temp = 0xFFFF;
    for (uint16_t i = 0; i < u16length; i++) {
        temp = temp ^ Buffer[i];
        for (unsigned char j = 1; j <= 8; j++) {
            flag = temp & 0x0001;
            temp >>= 1;
            if (flag) {
                temp ^= 0xA001;
            }
        }
    }
    temp2 = temp >> 8;
    temp = (temp << 8) | temp2;
    temp &= 0xFFFF;
    return temp;
}

// 初始化CAN接口
static int can_init(CanHandle *h, const char *ifname, uint8_t node_id) {
    struct sockaddr_can addr;
    struct ifreq ifr;

    if ((h->sockfd = socket(PF_CAN, SOCK_RAW, CAN_RAW)) < 0) {
        perror("socket");
        return PYLON_ERROR_CAN_IO;
    }

    strncpy(h->ifname, ifname, IFNAMSIZ);
    h->ifname[IFNAMSIZ - 1] = '\0';
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ);
    ifr.ifr_name[IFNAMSIZ - 1] = '\0';
    if (ioctl(h->sockfd, SIOCGIFINDEX, &ifr) < 0) {
        perror("ioctl");
        close(h->sockfd);
        return PYLON_ERROR_CAN_IO;
    }

    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    if (bind(h->sockfd, (struct sockaddr *)&addr, sizeof(addr))) {
        perror("bind");
        close(h->sockfd);
        return PYLON_ERROR_CAN_IO;
    }

    h->node_id = node_id;
    return PYLON_SUCCESS;
}

// 完整的CAN事务处理
static int can_transaction(CanHandle *h, PylontechCmdType cmd, 
                         const void *data, uint8_t len,
                         PylontechRespType resp_type, 
                         uint8_t *resp_data, int timeout_ms) 
{
    struct can_frame frame = {
        .can_id = cmd,
        .can_dlc = len
    };
    if(frame.can_id > 0x7FF)
    {
        frame.can_id = (frame.can_id & CAN_EFF_MASK) | CAN_EFF_FLAG;
    }
    if (data) memcpy(frame.data, data, len);
    // printf("Sending CAN frame: ID=0x%X, DLC=0x%d\n", frame.can_id, frame.can_dlc);

    // 清空接收缓存，避免读到旧数据
    struct can_frame discard_frame;
    fcntl(h->sockfd, F_SETFL, fcntl(h->sockfd, F_GETFL) | O_NONBLOCK);
    while (read(h->sockfd, &discard_frame, sizeof(discard_frame)) > 0);
    fcntl(h->sockfd, F_SETFL, fcntl(h->sockfd, F_GETFL) & ~O_NONBLOCK);

    // 发送请求
    if (write(h->sockfd, &frame, sizeof(frame)) != sizeof(frame)) {
        vic_print_message("error", "can write");
        return PYLON_ERROR_CAN_IO;
    }

    if(resp_type == PY_RESP_NONE) {
        // 如果不需要响应，直接返回
        return PYLON_SUCCESS;
    }

    // 设置超时时间
    struct timeval end_time, now;
    gettimeofday(&end_time, NULL);
    end_time.tv_sec += timeout_ms / 1000;
    end_time.tv_usec += (timeout_ms % 1000) * 1000;

    uint32_t expected_id = resp_type + h->node_id;

    // 持续等待响应
    while (1) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(h->sockfd, &fds);

        gettimeofday(&now, NULL);
        if (timercmp(&now, &end_time, >)) break;

        struct timeval remaining;
        timersub(&end_time, &now, &remaining);

        int ready = select(h->sockfd + 1, &fds, NULL, NULL, &remaining);
        if (ready <= 0) continue;

        struct can_frame recv_frame;
        if (read(h->sockfd, &recv_frame, sizeof(recv_frame)) < 0) {
            if (errno == EINTR) continue;
            vic_print_message("error", "can read");
            return PYLON_ERROR_CAN_IO;
        }
        if(recv_frame.can_id > 0x7FF)
        {
            recv_frame.can_id &= CAN_EFF_MASK;
        }
        // printf("Received CAN frame: ID=0x%X, EXPID=%X\n", recv_frame.can_id, expected_id);
        if (recv_frame.can_id == expected_id || recv_frame.can_id == resp_type) {
            if(recv_frame.can_id == resp_type)
            {
                g_oldProtocol = 1; // 收到老协议响应
            }
            if (resp_data) memcpy(resp_data, recv_frame.data, recv_frame.can_dlc);
            return recv_frame.can_dlc;
        }
    }

    vic_print_message("error", "CAN transaction timeout");
    return PYLON_ERROR_TIMEOUT; // Timeout
}

/* ==================== 设备版本SN名称获取 ==================== */
static int pylon_get_devices_info(CanHandle *h) {
    char data[8] = {0};
    DevInfo devInfo = {0};
    strcpy(devInfo.name, "Pylon Low-V Bat");
    strcpy(devInfo.ver, "1.0");
    strcpy(devInfo.sn, "");
    // 0x5000000: 设备SN和版本查询
    can_transaction(h, PY_CMD_SN_VER, data, sizeof(data), PY_RESP_NONE, NULL, PY_TIMEOUT_MS);

    struct timeval timeout = {
        .tv_sec = PY_TIMEOUT_MS / 1000,         // 毫秒转换为秒
        .tv_usec = (PY_TIMEOUT_MS % 1000) * 1000 // 毫秒转换为微秒
    };

    fd_set fds;
    time_t start_time = time(NULL);
    while (1) {
        // Check if timeout has expired
        if (time(NULL) - start_time >= timeout.tv_sec) {
            printf("<device serial=\"%s\" version=\"%s\" "
                    "description=\"%s\" id=\"%X\" "
                    "type=\"pylontech\" connection=\"%s\" "
                    "updatable=\"True\"/>\n",
                    devInfo.sn, devInfo.ver, devInfo.name, PY_PRODUCT_ID, h->ifname);
            return PYLON_SUCCESS;
        }

        FD_ZERO(&fds);
        FD_SET(h->sockfd, &fds);
        
        // Calculate remaining timeout
        struct timeval remaining;
        remaining.tv_sec = timeout.tv_sec - (time(NULL) - start_time);
        remaining.tv_usec = 0;

        // Wait for CAN messages
        int ready = select(h->sockfd + 1, &fds, NULL, NULL, &remaining);
        if (ready <= 0) {
            continue;  // Timeout or error, keep waiting until total timeout
        }

        // Read CAN frame
        struct can_frame frame;
        if (read(h->sockfd, &frame, sizeof(frame)) < 0) {
            perror("read");
            return PYLON_ERROR_CAN_IO;
        }
        if(frame.can_id > 0x7FF)
        {
            frame.can_id &= CAN_EFF_MASK;
        }
        // Check if it's a Pylontech brand message
        if (frame.can_id == PY_RESP_SN1) {
            memset(devInfo.sn, 0, sizeof(devInfo.sn));
            if (memcmp(frame.data, "\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF", 8) != 0) {
                strncpy(devInfo.sn, (char *)frame.data, 8);
            }
            devInfo.flagSN[0] = 1; // 标记SN已获取
        } else if (frame.can_id == PY_RESP_SN2) {
            if (memcmp(frame.data, "\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF", 8) != 0) {
                strncpy(devInfo.sn + 8, (char *)frame.data, 8);
            }
            devInfo.sn[16] = '\0'; // 确保字符串结束
            devInfo.flagSN[1] = 1; // 标记SN已获取
        } else if (frame.can_id == PY_RESP_VER) {
            memset(devInfo.ver, 0, sizeof(devInfo.ver));
            sprintf(devInfo.ver,"%d.%d", frame.data[0], frame.data[1]);
            devInfo.flagVer = 1; // 标记版本已获取
        } else if (frame.can_id == PY_EXT2_DEVICE_NAME) {
            memset(devInfo.name, 0, sizeof(devInfo.name));
            strncpy(devInfo.name, (char *)frame.data, 8);
            devInfo.flagName = 1; // 标记版本已获取
        }
        // 检查是否已获取所有信息
        if( devInfo.flagSN[0] && devInfo.flagSN[1] && 
                devInfo.flagName && devInfo.flagVer) {
            // 如果所有信息都已获取，打印设备信息
            printf("<device serial=\"%s\" version=\"%s\" "
                    "description=\"%s\" id=\"%X\" "
                    "type=\"pylontech\" connection=\"%s\" "
                    "updatable=\"True\"/>\n",
                    devInfo.sn, devInfo.ver, devInfo.name, PY_PRODUCT_ID, h->ifname);
            return PYLON_SUCCESS;
        }
    }
    // Should never reach here
    return PYLON_ERROR_GENERAL;
}

/* ==================== 设备扫描功能 (被动监听模式) ==================== */
static int pylon_scan_devices(CanHandle *h) {
    struct timeval timeout = {
        .tv_sec = PY_TIMEOUT_MS / 1000,         // 毫秒转换为秒
        .tv_usec = (PY_TIMEOUT_MS % 1000) * 1000 // 毫秒转换为微秒
    };
    fd_set fds;
    time_t start_time = time(NULL);
    
    while (1) {
        // Check if timeout has expired
        if (time(NULL) - start_time >= timeout.tv_sec) {
            // vic_print_message("warning", "No Pylontech devices found on bus");
            return PYLON_ERROR_NO_DEVICE;
        }

        FD_ZERO(&fds);
        FD_SET(h->sockfd, &fds);
        
        // Calculate remaining timeout
        struct timeval remaining;
        remaining.tv_sec = timeout.tv_sec - (time(NULL) - start_time);
        remaining.tv_usec = 0;

        // Wait for CAN messages
        int ready = select(h->sockfd + 1, &fds, NULL, NULL, &remaining);
        if (ready <= 0) {
            continue;  // Timeout or error, keep waiting until total timeout
        }

        // Read CAN frame
        struct can_frame frame;
        if (read(h->sockfd, &frame, sizeof(frame)) < 0) {
            perror("read");
            return PYLON_ERROR_CAN_IO;
        }
        if(frame.can_id > 0x7FF)
        {
            frame.can_id &= CAN_EFF_MASK;
        }
        // Check if it's a Pylontech brand message
        if (frame.can_id == PY_STD_BRAND) {
            if (frame.can_dlc >= 5 && memcmp(frame.data, "PYLON", 5) == 0) {
                // vic_print_message("normal", "Find Pylontech devices, Wait ...");
                return PYLON_SUCCESS;
            }
        }
    }

    // Should never reach here
    return PYLON_ERROR_GENERAL;
}

/* ==================== 固件升级流程 ==================== */
static int pylon_can_update(CanHandle *h, const char *fw_path) {
    vic_print_message("normal", "Loading firmware file:%s", fw_path);
    FILE *fw_file = fopen(fw_path, "rb");
    if (!fw_file) {
        vic_print_message("error", "Cannot open firmware file");
        return PYLON_ERROR_FILE;
    }

    fseek(fw_file, 0, SEEK_END);
    long fw_size = ftell(fw_file);
    fseek(fw_file, 0, SEEK_SET);
    
    uint8_t *fw_data = malloc(fw_size);
    if (!fw_data || fread(fw_data, 1, fw_size, fw_file) != fw_size) {
        vic_print_message("error", "Firmware read error");
        if (fw_file) fclose(fw_file);
        if (fw_data) free(fw_data);
        return PYLON_ERROR_FILE;
    }
    fclose(fw_file);

    // 1. 发送固件大小
    vic_print_message("normal", "Step 1.Sending firmware size");
    uint32_t fw_size_be = htonl(fw_size);
    uint8_t response[8] = {0};
    
    int result = -1;
    for(int try=0; try<PY_TRY_TIMES; try++) {
        memset(response, 0, sizeof(response));
        if (can_transaction(h, PY_CMD_FIRMWARE_SIZE, &fw_size_be, 4,
                        PY_RESP_FIRMWARE_SIZE, response, PY_TIMEOUT_MS) < 0) {
            continue;
        }

        if (response[0] != PY_STATUS_SIZE_OK) {
            vic_print_message("error", "Firmware size rejected ErrID:0x%02X", response[0]);
            continue;
        }

        if(((response[1] << 8) + response[2]) != PY_BLOCK_SIZE) {
            vic_print_message("error", "Block size mismatch ErrSize");
            continue;
        }
        result = 0;
        break;
    }
    if(result < 0) {
        vic_print_message("error", "Failed to send firmware size after 3 attempts");
        free(fw_data);
        return PYLON_ERROR_STEP1_FAILED;
    }

    // 2. 分块传输
    int total_blocks = (fw_size + PY_BLOCK_SIZE - 1) / PY_BLOCK_SIZE;
    vic_print_message("normal", "Step 2.Starting block transfer");
    
    for (int i = 0; i < total_blocks; i++) {
        result = -1;
        for(int try=0; try<PY_TRY_TIMES; try++) {
            int offset = i * PY_BLOCK_SIZE;
            int block_len = (i == total_blocks - 1) ? (fw_size - offset) : PY_BLOCK_SIZE;
            
            // 发送块序号 (1-based)
            uint16_t block_num = htons(i + 1);
            can_transaction(h, PY_CMD_BLOCK_NUMBER, &block_num, 2, PY_RESP_NONE, response, PY_TIMEOUT_MS);
            if(g_oldProtocol)
            {
                usleep(40 * 1000);
            }

            // 发送块数据
            for (int offset_data = 0; offset_data < block_len; offset_data += 8) {
                int len_to_send = (block_len - offset_data > 8) ? 8 : (block_len - offset_data);
                can_transaction(h, PY_CMD_BLOCK_DATA, fw_data + offset + offset_data, len_to_send, PY_RESP_NONE, response, PY_TIMEOUT_MS);
            }

            // 计算并发送CRC以及分包大小
            uint8_t crc_data[2 + PY_BLOCK_SIZE];
            memcpy(crc_data, &block_num, 2);                     // 写入 block_num (2字节)
            memcpy(crc_data + 2, fw_data + offset, block_len);   // 写入数据块
            // 计算 CRC（仅 block_num + 数据块）
            uint16_t crc = calcCRC(crc_data, 2 + block_len);

            // 构造发送数据：CRC (2字节) + block_len (2字节)
            uint8_t send_data[2];
            send_data[0] = crc & 0xFF;              // CRC 低字节
            send_data[1] = (crc >> 8) & 0xFF;       // CRC 高字节
        
            // send_data[2] = (block_len >> 8) & 0xFF; // block_len 高字节
            // send_data[3] = block_len & 0xFF;        // block_len 低字节

            // 发送 4 字节数据（CRC + block_len）
            memset(response, 0, sizeof(response));
            if (can_transaction(h, PY_CMD_BLOCK_CRC, send_data, sizeof(send_data),
                            PY_RESP_BLOCK_STATUS, response, PY_TIMEOUT_MS) < 0) {
                vic_print_message("warning", "error can_transaction");
                continue;
            }

            if (response[0] != PY_STATUS_BLOCK_OK) {
                // vic_print_message("warning", "Block transfer failed at block %d, ErrID:0x%02X", i + 1, response[0]);
                continue;
            }

            vic_print_progress((i+1)*100/total_blocks);
            result = 0;
            break;
        }
        if(result < 0) {
            vic_print_message("error", "Failed to transfer block %d after %d attempts", i + 1, PY_TRY_TIMES);
            free(fw_data);
            return PYLON_ERROR_STEP2_FAILED;
        }
    }

    // 3. 发送固件CRC
    vic_print_message("normal", "Step 3.Verifying firmware CRC");
    uint16_t fw_crc = htonl(calcCRC(fw_data, fw_size));
    result = -1;
    for(int try=0; try<PY_TRY_TIMES; try++) {
        memset(response, 0, sizeof(response));
        if (can_transaction(h, PY_CMD_FIRMWARE_CRC, &fw_crc, 2,
                        PY_RESP_FIRMWARE_CRC, response, PY_TIMEOUT_MS) < 0) {
            continue;
        }

        if (response[0] != PY_STATUS_CRC_OK) {
            vic_print_message("error", "Firmware CRC mismatch ErrID:0x%02X", response[0]);
            continue;
        }
        result = 0;
        break;
    }
    if(result < 0) {
        vic_print_message("error", "Failed to verify firmware CRC after 3 attempts");
        free(fw_data);
        return PYLON_ERROR_STEP3_FAILED;
    }

    // 4. 重启升级
    char data[8] = {0};
    vic_print_message("normal", "Step 4.Restarting device");

    result = -1;
    for(int try=0; try<PY_TRY_TIMES; try++) {
        memset(response, 0, sizeof(response));
        if (can_transaction(h, PY_CMD_RESTART, data, sizeof(data),
                        PY_RESP_RESTART, response, PY_TIMEOUT_MS) < 0) {
            continue;
        }

        if (response[0] != PY_RESTART_CFM_UPGRADE && response[0] != PY_RESTART_CFM_TRANS) {
            vic_print_message("error", "Restart rejected ErrID:0x%02X", response[0]);
            continue;
        }
        result = 0;
        break;
    }
    if(result < 0) {
        vic_print_message("error", "Failed to restart device after 3 attempts");
        free(fw_data);
        return PYLON_ERROR_STEP4_FAILED;
    }

    // 5. 检查升级状态
    vic_print_message("normal", "Step 5.Checking upgrade status");
    int retries = 150;
    while (retries--) {
        memset(response, 0, sizeof(response));
        if (can_transaction(h, PY_CMD_CHECK_STATUS, data, 1,
                          PY_RESP_STATUS, response, PY_TIMEOUT_MS) < 0) {
            sleep(2);
            continue;
        }

        if (response[0] == PY_UPGRADE_SUCCESS || 
                    response[0] == 0x00) {    //兼容老电池协议
            vic_print_message("success", "Upgrade completed");
            free(fw_data);
            return PYLON_SUCCESS;
        } else if (response[0] == PY_UPGRADE_FAILED) {
            vic_print_message("error", "Upgrade failed ErrID:0x%02X", response[0]);
            free(fw_data);
            return PYLON_ERROR_CHECK_FAILED;
        }
        sleep(2);
    }

    vic_print_message("error", "Status check timeout");
    free(fw_data);
    return PYLON_ERROR_CHECK_TIMEOUT;
}

/* 处理单个CAN接口的通用函数 */
static int process_single_interface(const char *can_if, uint8_t node_id, 
                                  bool list_mode, bool update_mode, 
                                  const char *fw_file) {
    CanHandle can;
    int ret = -1;

    if (can_init(&can, can_if, node_id)) {
        vic_print_message("error", "CAN init failed on interface %s", can_if);
        return PYLON_ERROR_CAN_INIT;
    }

    if (list_mode) {
        ret = pylon_scan_devices(&can);
        if (ret == 0) {
            ret = pylon_get_devices_info(&can);
        }
    } 
    else if (update_mode && fw_file) {
        vic_print_message("normal", "Updating device on socketcan:%s", can_if);
        ret = pylon_can_update(&can, fw_file);
        if (ret == 0) {
            vic_print_message("success", "Update successful on %s", can_if);
            ret = PYLON_SUCCESS;
        } else {
            vic_print_message("error", "Update failed on %s", can_if);
        }
    }

    close(can.sockfd);
    return ret;
}

/* 打印用法信息 */
static void print_usage(const char *prog_name) {
    vic_print_message("warning", 
        "Usage:\n"
        "  %s -l [-c <can_if>]\n"
        "  %s -u -f <file> [-c <can_if>] [-n <node_id>]\n"
        "\n"
        "If -c is omitted, all available CAN interfaces will be scanned",
        prog_name, prog_name);
}

/* ==================== 主程序 ==================== */
char** get_available_can_interfaces() {
    FILE *fp;
    char line[256];
    char **interfaces = NULL;
    int count = 0;
    int capacity = 10;

    // 使用ip命令获取CAN接口
    fp = popen("ip link show 2>/dev/null | grep -o 'can[0-9]\\+' | sort -u", "r");
    if (!fp) {
        return NULL;
    }

    // 分配初始内存
    interfaces = malloc(capacity * sizeof(char*));
    if (!interfaces) {
        pclose(fp);
        return NULL;
    }

    // 读取所有CAN接口
    while (fgets(line, sizeof(line), fp)) {
        // 去除换行符
        line[strcspn(line, "\n")] = 0;
        
        // 检查是否需要扩容
        if (count >= capacity - 1) {
            capacity *= 2;
            char **new_interfaces = realloc(interfaces, capacity * sizeof(char*));
            if (!new_interfaces) {
                break;
            }
            interfaces = new_interfaces;
        }
        
        interfaces[count] = strdup(line);
        if (interfaces[count]) {
            count++;
        }
    }

    pclose(fp);
    interfaces[count] = NULL; // NULL结尾
    
    return interfaces;
}

void free_can_interfaces(char **interfaces) {
    if (interfaces) {
        for (int i = 0; interfaces[i] != NULL; i++) {
            free(interfaces[i]);
        }
        free(interfaces);
    }
}

int main(int argc, char **argv) {
    uint8_t node_id = PY_NODE_ID_DEFAULT;
    const char *can_if = NULL;
    const char *fw_file = NULL;
    bool list_mode = false;
    bool update_mode = false;
    int ret = 0;

    // 设置输出为行缓冲模式
    setvbuf(stdout, NULL, _IOLBF, 0);
    // vic_print_message("normal", "Welcome Pylon Tool Version: %s", PYLON_TOOL_VERSION);

    // 参数解析
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-l") == 0) list_mode = true;
        else if (strcmp(argv[i], "-u") == 0) update_mode = true;
        else if (strcmp(argv[i], "-c") == 0) can_if = argv[++i];
        else if (strcmp(argv[i], "-f") == 0) fw_file = argv[++i];
        else if (strcmp(argv[i], "-n") == 0) node_id = atoi(argv[++i]);
    }

    // 参数校验
    if (!list_mode && !update_mode) {
        vic_print_message("error", "Must specify either --list or --update");
        print_usage(argv[0]);
        return PYLON_ERROR_PARAM;
    }

    if (update_mode && !fw_file) {
        vic_print_message("error", "Firmware file required for update");
        print_usage(argv[0]);
        return PYLON_ERROR_FIRMWARE;
    }

    // CAN接口处理逻辑
    if (can_if) {
        // 使用指定的CAN接口
        ret = process_single_interface(can_if, node_id, list_mode, update_mode, fw_file);
    } else {
        // 自动扫描所有可用CAN接口
        vic_print_message("normal", "Scanning all available interfaces...");
        
        char **can_interfaces = get_available_can_interfaces();
        bool found_device = false;
        
        if (can_interfaces == NULL || can_interfaces[0] == NULL) {
            vic_print_message("error", "No CAN interfaces found on this system");
            ret = PYLON_ERROR_NO_DEVICE;
        } else {
            // 显示找到的接口
            for (int i = 0; can_interfaces[i] != NULL; i++) {
                vic_print_message("normal", "Found CAN interface: %s", can_interfaces[i]);
            }
            
            // 尝试每个接口
            for (int i = 0; can_interfaces[i] != NULL; i++) {
                vic_print_message("normal", "Checking interface %s...", can_interfaces[i]);
                ret = process_single_interface(can_interfaces[i], node_id, list_mode, update_mode, fw_file);
                if (ret == 0) {
                    found_device = true;
                    // 如果是列表模式，继续扫描其他接口
                    if (!list_mode) break; // 更新模式找到第一个可用接口即可
                }
            }
            
            if (!found_device) {
                vic_print_message("error", "No valid devices found on any CAN interface");
                ret = PYLON_ERROR_NO_DEVICE;
            }
            
            free_can_interfaces(can_interfaces);
        }
    }

    return ret;
}
