/* adb_proto.h — ADB 协议报文与客户端接口 ([纯逻辑·可仿真], USB 传输由 hal 注入)
 *
 * ADB over USB: 接口 class 0x42 / subclass 0x01 / protocol 0x01,
 * 24B 报文头 [cmd][arg0][arg1][data_len][data_checksum][magic], bulk 传输.
 *
 * 握手: P4(host) → CNXN → 手机 → AUTH(RSAPUBLICKEY) → P4 回公钥 →
 * 手机弹授权 → 允许 → 手机 → AUTH(SIGNATURE) 通过 → CNXN 确认 →
 * OPEN "shell:..." → OKAY → WRTE(命令) ... 双向流.
 */
#ifndef ADB_PROTO_H
#define ADB_PROTO_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ADB_VERSION      0x01000000
#define ADB_MAX_PAYLOAD  4096

enum {
    ADB_CMD_SYNC     = 0x434E5953, /* 'CNYS' */
    ADB_CMD_CNXN     = 0x434E584E, /* 'CNXN' */
    ADB_CMD_AUTH     = 0x41555448, /* 'AUTH' */
    ADB_CMD_OPEN     = 0x4F50454E, /* 'OPEN' */
    ADB_CMD_OKAY     = 0x4F4B4159, /* 'OKAY' */
    ADB_CMD_CLSE     = 0x434C5345, /* 'CLSE' */
    ADB_CMD_WRTE     = 0x57525445, /* 'WRTE' */
};

typedef struct __attribute__((packed)) {
    uint32_t cmd;
    uint32_t arg0;
    uint32_t arg1;
    uint32_t data_len;
    uint32_t checksum;
    uint32_t magic;
} adb_msg_hdr_t;

#define ADB_HDR_SZ sizeof(adb_msg_hdr_t)

static inline uint32_t adb_checksum(const uint8_t *data, size_t len)
{
    uint32_t sum = 0;
    for (size_t i = 0; i < len; i++) sum += data[i];
    return sum;
}

static inline uint32_t adb_magic(uint32_t cmd)
{
    return cmd ^ 0xFFFFFFFFu;
}

/* 发送一条消息 (data 可 NULL) → 返回序列化长度 */
int adb_pack(adb_msg_hdr_t *hdr_out, uint32_t cmd, uint32_t arg0, uint32_t arg1,
             const uint8_t *data, size_t data_len);

/* 解析 24B 头: 校验 magic/长度; 合法返回 0 */
int adb_hdr_check(const adb_msg_hdr_t *hdr);

#ifdef __cplusplus
}
#endif
#endif /* ADB_PROTO_H */
