/* adb_proto.c — ADB 报文打包/校验 ([纯逻辑·可仿真]) */
#include "adb_proto.h"

int adb_pack(adb_msg_hdr_t *hdr_out, uint32_t cmd, uint32_t arg0, uint32_t arg1,
             const uint8_t *data, size_t data_len)
{
    hdr_out->cmd = cmd;
    hdr_out->arg0 = arg0;
    hdr_out->arg1 = arg1;
    hdr_out->data_len = (uint32_t)data_len;
    hdr_out->checksum = adb_checksum(data, data_len);
    hdr_out->magic = adb_magic(cmd);
    return 0;
}

int adb_hdr_check(const adb_msg_hdr_t *hdr)
{
    if (hdr->magic != adb_magic(hdr->cmd)) return -1;
    if (hdr->data_len > ADB_MAX_PAYLOAD) return -1;
    return 0;
}
