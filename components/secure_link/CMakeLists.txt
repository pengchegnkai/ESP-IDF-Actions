# secure_link —— 安全链路 (P4 <-> C6 协处理器)
# 阶段三: stub; 阶段二换真实现时本文件无需改动
idf_component_register(
    SRCS "src/secure_link_stub.c"
    INCLUDE_DIRS "include"
    REQUIRES log
)
