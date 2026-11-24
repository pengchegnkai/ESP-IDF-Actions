#!/usr/bin/env python3
# gen_payloads.py — 生成 100 个 BadUSB 演示载荷到 payloads/ (拷至 SD 卡 /sdcard/payloads)
# 教育演示级: 应用快捷键/系统信息/文本演示 — 不含破坏性内容
import os

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "payloads")
os.makedirs(OUT, exist_ok=True)

P = []  # (filename, script)

# ============ Windows (1-50) ============
P += [
    ("win_01_open_calculator", '# Windows: 打开计算器\nDELAY 500\nKEYCOMBO gui r\nDELAY 300\nSTRING calc\nDELAY 100\nKEYCOMBO enter\n'),
    ("win_02_open_notepad", '# Windows: 打开记事本\nDELAY 500\nKEYCOMBO gui r\nDELAY 300\nSTRING notepad\nDELAY 100\nKEYCOMBO enter\nDELAY 500\nSTRING Hello from SecurityTerminal!\n'),
    ("win_03_open_paint", '# Windows: 打开画图\nDELAY 500\nKEYCOMBO gui r\nDELAY 300\nSTRING mspaint\nDELAY 100\nKEYCOMBO enter\n'),
    ("win_04_open_explorer", '# Windows: 打开资源管理器\nDELAY 300\nKEYCOMBO gui e\n'),
    ("win_05_lock_screen", '# Windows: 锁屏\nDELAY 300\nKEYCOMBO gui l\n'),
    ("win_06_show_desktop", '# Windows: 显示桌面\nDELAY 300\nKEYCOMBO gui d\n'),
    ("win_07_task_view", '# Windows: 任务视图\nDELAY 300\nKEYCOMBO gui tab\nDELAY 200\nKEYCOMBO gui tab\n'),
    ("win_08_screenshot", '# Windows: 截图工具\nDELAY 300\nKEYCOMBO gui shift s\n'),
    ("win_09_clipboard_history", '# Windows: 剪贴板历史\nDELAY 300\nKEYCOMBO gui v\n'),
    ("win_10_settings", '# Windows: 打开设置\nDELAY 500\nKEYCOMBO gui i\n'),
    ("win_11_ipconfig_demo", '# Windows: 演示查看本机 IP 配置 (合法自检)\nDELAY 500\nKEYCOMBO gui r\nDELAY 300\nSTRING cmd\nDELAY 100\nKEYCOMBO enter\nDELAY 800\nSTRING ipconfig /all\nDELAY 100\nKEYCOMBO enter\n'),
    ("win_12_systeminfo_demo", '# Windows: 演示系统信息 (合法自检)\nDELAY 500\nKEYCOMBO gui r\nDELAY 300\nSTRING cmd\nDELAY 100\nKEYCOMBO enter\nDELAY 800\nSTRING systeminfo\nDELAY 100\nKEYCOMBO enter\n'),
    ("win_13_taskmgr", '# Windows: 打开任务管理器\nDELAY 500\nKEYCOMBO ctrl shift esc\n'),
    ("win_14_minimize_all", '# Windows: 最小化所有窗口\nDELAY 300\nKEYCOMBO gui m\n'),
    ("win_15_close_window", '# Windows: 关闭当前窗口\nDELAY 300\nKEYCOMBO alt f4\n'),
    ("win_16_alt_tab_demo", '# Windows: 切换窗口演示\nDELAY 300\nKEYCOMBO alt tab\nDELAY 300\nKEYCOMBO alt tab\n'),
    ("win_17_desktop_switch", '# Windows: 切换虚拟桌面\nDELAY 300\nKEYCOMBO ctrl gui right\nDELAY 500\nKEYCOMBO ctrl gui left\n'),
    ("win_18_run_diskmgmt", '# Windows: 打开磁盘管理 (只读查看)\nDELAY 500\nKEYCOMBO gui r\nDELAY 300\nSTRING diskmgmt.msc\nDELAY 100\nKEYCOMBO enter\n'),
    ("win_19_run_devmgmt", '# Windows: 打开设备管理器\nDELAY 500\nKEYCOMBO gui r\nDELAY 300\nSTRING devmgmt.msc\nDELAY 100\nKEYCOMBO enter\n'),
    ("win_20_date_time", '# Windows: 打开日期时间设置\nDELAY 500\nKEYCOMBO gui r\nDELAY 300\nSTRING timedate.cpl\nDELAY 100\nKEYCOMBO enter\n'),
    ("win_21_network_conns", '# Windows: 打开网络连接\nDELAY 500\nKEYCOMBO gui r\nDELAY 300\nSTRING ncpa.cpl\nDELAY 100\nKEYCOMBO enter\n'),
    ("win_22_notepad_sysinfo", '# Windows: 用记事本记录环境信息\nDELAY 500\nKEYCOMBO gui r\nDELAY 300\nSTRING notepad\nDELAY 100\nKEYCOMBO enter\nDELAY 800\nSTRING 环境巡检: 已通过安全终端记录\nDELAY 200\nKEYCOMBO enter\nSTRING 操作时间已由终端时间戳记录\n'),
    ("win_23_volume_up", '# Windows: 提高音量 x5\nDELAY 300\nKEYCOMBO enter\nDELAY 100\nKEYCOMBO gui r\nDELAY 300\nSTRING sndvol\nDELAY 100\nKEYCOMBO enter\n'),
    ("win_24_explorer_downloads", '# Windows: 打开下载目录\nDELAY 300\nKEYCOMBO gui r\nDELAY 300\nSTRING shell:Downloads\nDELAY 100\nKEYCOMBO enter\n'),
    ("win_25_explorer_documents", '# Windows: 打开文档目录\nDELAY 300\nKEYCOMBO gui r\nDELAY 300\nSTRING shell:Personal\nDELAY 100\nKEYCOMBO enter\n'),
    ("win_26_explorer_pictures", '# Windows: 打开图片目录\nDELAY 300\nKEYCOMBO gui r\nDELAY 300\nSTRING shell:Pictures\nDELAY 100\nKEYCOMBO enter\n'),
    ("win_27_task_view_new_desktop", '# Windows: 新建虚拟桌面\nDELAY 300\nKEYCOMBO ctrl gui d\n'),
    ("win_28_snipping_full", '# Windows: 全屏截图到图片目录\nDELAY 300\nKEYCOMBO gui printscreen\n'),
    ("win_29_focus_assist", '# Windows: 打开专注助手\nDELAY 300\nKEYCOMBO gui n\n'),
    ("win_30_widgets", '# Windows: 打开小组件面板\nDELAY 300\nKEYCOMBO gui w\n'),
    ("win_31_search_web", '# Windows: 打开搜索\nDELAY 300\nKEYCOMBO gui s\nDELAY 300\nSTRING weather\n'),
    ("win_32_notepad_ascii", '# Windows: 记事本 ASCII 演示\nDELAY 500\nKEYCOMBO gui r\nDELAY 300\nSTRING notepad\nDELAY 100\nKEYCOMBO enter\nDELAY 800\nSTRING =====================\nDELAY 100\nKEYCOMBO enter\nSTRING  Security Terminal\nDELAY 100\nKEYCOMBO enter\nSTRING =====================\n'),
    ("win_33_cmd_help", '# Windows: cmd 帮助菜单演示\nDELAY 500\nKEYCOMBO gui r\nDELAY 300\nSTRING cmd\nDELAY 100\nKEYCOMBO enter\nDELAY 800\nSTRING help\nDELAY 100\nKEYCOMBO enter\n'),
    ("win_34_cmd_ver", '# Windows: 显示系统版本\nDELAY 500\nKEYCOMBO gui r\nDELAY 300\nSTRING cmd\nDELAY 100\nKEYCOMBO enter\nDELAY 800\nSTRING ver\nDELAY 100\nKEYCOMBO enter\n'),
    ("win_35_cmd_tree_demo", '# Windows: 目录树演示 (当前目录)\nDELAY 500\nKEYCOMBO gui r\nDELAY 300\nSTRING cmd\nDELAY 100\nKEYCOMBO enter\nDELAY 800\nSTRING tree /f\nDELAY 100\nKEYCOMBO enter\n'),
    ("win_36_explorer_c", '# Windows: 打开 C 盘\nDELAY 300\nKEYCOMBO gui e\nDELAY 500\nSTRING C:\nDELAY 100\nKEYCOMBO enter\n'),
    ("win_37_quick_settings", '# Windows: 快速设置\nDELAY 300\nKEYCOMBO gui a\n'),
    ("win_38_notification_center", '# Windows: 通知中心\nDELAY 300\nKEYCOMBO gui n\n'),
    ("win_39_run_regedit_demo", '# Windows: 打开注册表编辑器 (仅打开, 只读查看)\nDELAY 500\nKEYCOMBO gui r\nDELAY 300\nSTRING regedit\nDELAY 100\nKEYCOMBO enter\nDELAY 500\nKEYCOMBO alt f4\n'),
    ("win_40_run_charmap", '# Windows: 打开字符映射表\nDELAY 500\nKEYCOMBO gui r\nDELAY 300\nSTRING charmap\nDELAY 100\nKEYCOMBO enter\n'),
    ("win_41_run_osk", '# Windows: 打开屏幕键盘\nDELAY 500\nKEYCOMBO gui r\nDELAY 300\nSTRING osk\nDELAY 100\nKEYCOMBO enter\n'),
    ("win_42_run_magnify", '# Windows: 打开放大镜\nDELAY 500\nKEYCOMBO gui r\nDELAY 300\nSTRING magnify\nDELAY 100\nKEYCOMBO enter\n'),
    ("win_43_hello_multiline", '# Windows: 多行文本演示\nDELAY 500\nKEYCOMBO gui r\nDELAY 300\nSTRING notepad\nDELAY 100\nKEYCOMBO enter\nDELAY 800\nSTRING Line1: 离线安全终端演示\nDELAY 100\nKEYCOMBO enter\nSTRING Line2: BadUSB 教育载荷\nDELAY 100\nKEYCOMBO enter\nSTRING Line3: 仅用于授权环境\n'),
    ("win_44_ctrl_a_demo", '# Windows: 全选演示\nDELAY 800\nKEYCOMBO ctrl a\n'),
    ("win_45_ctrl_c_demo", '# Windows: 复制演示\nDELAY 300\nKEYCOMBO ctrl a\nDELAY 200\nKEYCOMBO ctrl c\n'),
    ("win_46_undo_demo", '# Windows: 撤销演示\nDELAY 300\nKEYCOMBO ctrl z\n'),
    ("win_47_save_demo", '# Windows: 保存演示 (Ctrl+S)\nDELAY 300\nKEYCOMBO ctrl s\nDELAY 500\nKEYCOMBO enter\n'),
    ("win_48_new_folder", '# Windows: 新建文件夹 (资源管理器内)\nDELAY 300\nKEYCOMBO gui e\nDELAY 500\nKEYCOMBO ctrl shift n\nDELAY 500\nSTRING DemoFolder\nDELAY 100\nKEYCOMBO enter\n'),
    ("win_49_rename_demo", '# Windows: 重命名演示\nDELAY 500\nKEYCOMBO f2\nDELAY 500\nKEYCOMBO esc\n'),
    ("win_50_delete_demo", '# Windows: 删除演示 (回收站)\nDELAY 500\nKEYCOMBO delete\nDELAY 500\nKEYCOMBO enter\n'),
]

# ============ Linux (51-80) ============
P += [
    ("lin_51_open_terminal", '# Linux: 打开终端\nDELAY 500\nKEYCOMBO ctrl alt t\n'),
    ("lin_52_neofetch", '# Linux: neofetch 系统信息\nDELAY 500\nKEYCOMBO ctrl alt t\nDELAY 800\nSTRING neofetch\nDELAY 100\nKEYCOMBO enter\n'),
    ("lin_53_uptime", '# Linux: uptime\nDELAY 500\nKEYCOMBO ctrl alt t\nDELAY 800\nSTRING uptime\nDELAY 100\nKEYCOMBO enter\n'),
    ("lin_54_df_demo", '# Linux: 磁盘占用 (只读)\nDELAY 500\nKEYCOMBO ctrl alt t\nDELAY 800\nSTRING df -h\nDELAY 100\nKEYCOMBO enter\n'),
    ("lin_55_free_demo", '# Linux: 内存占用 (只读)\nDELAY 500\nKEYCOMBO ctrl alt t\nDELAY 800\nSTRING free -h\nDELAY 100\nKEYCOMBO enter\n'),
    ("lin_56_ls_demo", '# Linux: 列出文件\nDELAY 500\nKEYCOMBO ctrl alt t\nDELAY 800\nSTRING ls -la\nDELAY 100\nKEYCOMBO enter\n'),
    ("lin_57_date_demo", '# Linux: 系统时间\nDELAY 500\nKEYCOMBO ctrl alt t\nDELAY 800\nSTRING date\nDELAY 100\nKEYCOMBO enter\n'),
    ("lin_58_whoami", '# Linux: 当前用户\nDELAY 500\nKEYCOMBO ctrl alt t\nDELAY 800\nSTRING whoami\nDELAY 100\nKEYCOMBO enter\n'),
    ("lin_59_history_demo", '# Linux: 命令历史 (只读查看)\nDELAY 500\nKEYCOMBO ctrl alt t\nDELAY 800\nSTRING history | tail -5\nDELAY 100\nKEYCOMBO enter\n'),
    ("lin_60_ping_demo", '# Linux: 连通性测试\nDELAY 500\nKEYCOMBO ctrl alt t\nDELAY 800\nSTRING ping -c 4 gateway\nDELAY 100\nKEYCOMBO enter\n'),
    ("lin_61_lock_screen", '# Linux (GNOME): 锁屏\nDELAY 300\nKEYCOMBO gui l\n'),
    ("lin_62_workspaces", '# Linux: 工作区切换\nDELAY 300\nKEYCOMBO ctrl alt right\nDELAY 500\nKEYCOMBO ctrl alt left\n'),
    ("lin_63_screenshot", '# Linux: 截图工具\nDELAY 300\nKEYCOMBO shift printscreen\n'),
    ("lin_64_app_menu", '# Linux: 应用菜单\nDELAY 300\nKEYCOMBO gui\n'),
    ("lin_65_htop", '# Linux: 进程监控\nDELAY 500\nKEYCOMBO ctrl alt t\nDELAY 800\nSTRING htop\nDELAY 100\nKEYCOMBO enter\n'),
    ("lin_66_top_demo", '# Linux: top 监控\nDELAY 500\nKEYCOMBO ctrl alt t\nDELAY 800\nSTRING top\nDELAY 100\nKEYCOMBO enter\n'),
    ("lin_67_kernel_demo", '# Linux: 内核版本 (只读)\nDELAY 500\nKEYCOMBO ctrl alt t\nDELAY 800\nSTRING uname -a\nDELAY 100\nKEYCOMBO enter\n'),
    ("lin_68_ip_demo", '# Linux: 网络接口 (只读)\nDELAY 500\nKEYCOMBO ctrl alt t\nDELAY 800\nSTRING ip a\nDELAY 100\nKEYCOMBO enter\n'),
    ("lin_69_ps_demo", '# Linux: 进程列表 (只读)\nDELAY 500\nKEYCOMBO ctrl alt t\nDELAY 800\nSTRING ps aux | head -10\nDELAY 100\nKEYCOMBO enter\n'),
    ("lin_70_clear_demo", '# Linux: 清屏演示\nDELAY 500\nKEYCOMBO ctrl alt t\nDELAY 800\nSTRING clear\nDELAY 100\nKEYCOMBO enter\n'),
]

# ============ macOS (71-90) ============
P += [
    ("mac_71_spotlight", '# macOS: Spotlight\nDELAY 300\nKEYCOMBO gui space\nDELAY 300\nSTRING calculator\nDELAY 100\nKEYCOMBO enter\n'),
    ("mac_72_lock_screen", '# macOS: 锁屏\nDELAY 300\nKEYCOMBO ctrl gui q\n'),
    ("mac_73_launchpad", '# macOS: 启动台\nDELAY 300\nKEYCOMBO ctrl esc\n'),
    ("mac_74_mission_control", '# macOS: 调度中心\nDELAY 300\nKEYCOMBO ctrl up\n'),
    ("mac_75_finder", '# macOS: 打开 Finder\nDELAY 300\nKEYCOMBO gui up\nDELAY 200\nKEYCOMBO gui n\n'),
    ("mac_76_screenshot", '# macOS: 截图\nDELAY 300\nKEYCOMBO shift gui 4\n'),
    ("mac_77_terminal", '# macOS: 打开终端\nDELAY 300\nKEYCOMBO gui space\nDELAY 300\nSTRING terminal\nDELAY 100\nKEYCOMBO enter\n'),
    ("mac_78_sysinfo", '# macOS: 关于本机\nDELAY 300\nKEYCOMBO gui space\nDELAY 300\nSTRING about\nDELAY 100\nKEYCOMBO enter\n'),
    ("mac_79_switch_app", '# macOS: 切换应用\nDELAY 300\nKEYCOMBO gui tab\n'),
    ("mac_80_hide_app", '# macOS: 隐藏应用\nDELAY 300\nKEYCOMBO gui h\n'),
]

# ============ 通用/文本演示 (81-100) ============
P += [
    ("gen_81_hello_world", '# 通用: Hello World 演示\nDELAY 500\nSTRING Hello World from ESP32-P4!\n'),
    ("gen_82_esp32_intro", '# 通用: 设备介绍\nDELAY 500\nSTRING 本机由 ESP32-P4 离线安全终端驱动。\nDELAY 200\nKEYCOMBO enter\nSTRING 所有载荷仅在授权环境下用于演示。\n'),
    ("gen_83_ascii_banner", '# 通用: ASCII 横幅\nDELAY 500\nSTRING ===== SECURITY TERMINAL =====\nDELAY 200\nKEYCOMBO enter\nSTRING [P4] ready.\n'),
    ("gen_84_typing_speed", '# 通用: 打字节奏演示\nDELAY 500\nSTRING a\nDELAY 100\nSTRING b\nDELAY 100\nSTRING c\nDELAY 100\nSTRING d\nDELAY 100\nSTRING e\n'),
    ("gen_85_multi_delay", '# 通用: DELAY 指令演示\nDELAY 500\nSTRING 3\nDELAY 1000\nSTRING 2\nDELAY 1000\nSTRING 1\n'),
    ("gen_86_keycombo_demo", '# 通用: 组合键演示 (Ctrl+A 全选)\nDELAY 500\nSTRING select me\nDELAY 300\nKEYCOMBO ctrl a\n'),
    ("gen_87_number_pad", '# 通用: 数字串演示\nDELAY 500\nSTRING 0123456789\n'),
    ("gen_88_alphabet", '# 通用: 字母表演示\nDELAY 500\nSTRING ABCDEFGHIJKLMNOPQRSTUVWXYZ\n'),
    ("gen_89_symbols", '# 通用: 符号演示\nDELAY 500\nSTRING !@#$%^&*()\n'),
    ("gen_90_unicode_note", '# 通用: 中文演示\nDELAY 500\nSTRING 安全终端 BadUSB 载荷演示成功。\n'),
    ("gen_91_win_security_check", '# Windows: 安全自查清单\nDELAY 500\nKEYCOMBO gui r\nDELAY 300\nSTRING notepad\nDELAY 100\nKEYCOMBO enter\nDELAY 800\nSTRING [自查] 1.锁屏已测试 2.防火墙状态已查看 3.补丁已确认\n'),
    ("gen_92_win_update_check", '# Windows: 打开更新设置\nDELAY 500\nKEYCOMBO gui r\nDELAY 300\nSTRING ms-settings:windowsupdate\nDELAY 100\nKEYCOMBO enter\n'),
    ("gen_93_win_defender", '# Windows: 打开安全中心\nDELAY 500\nKEYCOMBO gui r\nDELAY 300\nSTRING windowsdefender:\nDELAY 100\nKEYCOMBO enter\n'),
    ("gen_94_win_battery", '# Windows: 电池报告 (生成到当前目录)\nDELAY 500\nKEYCOMBO gui r\nDELAY 300\nSTRING cmd\nDELAY 100\nKEYCOMBO enter\nDELAY 800\nSTRING powercfg /batteryreport\nDELAY 100\nKEYCOMBO enter\n'),
    ("gen_95_win_display_info", '# Windows: 显示设置\nDELAY 500\nKEYCOMBO gui r\nDELAY 300\nSTRING dpiscaling\nDELAY 100\nKEYCOMBO enter\n'),
    ("gen_96_win_sound_settings", '# Windows: 声音设置\nDELAY 500\nKEYCOMBO gui r\nDELAY 300\nSTRING mmsys.cpl\nDELAY 100\nKEYCOMBO enter\n'),
    ("win_97_win_printers", '# Windows: 打印机设置\nDELAY 500\nKEYCOMBO gui r\nDELAY 300\nSTRING control printers\nDELAY 100\nKEYCOMBO enter\n'),
    ("win_98_win_mouse", '# Windows: 鼠标设置\nDELAY 500\nKEYCOMBO gui r\nDELAY 300\nSTRING main.cpl\nDELAY 100\nKEYCOMBO enter\n'),
    ("win_99_win_keyboard", '# Windows: 键盘设置\nDELAY 500\nKEYCOMBO gui r\nDELAY 300\nSTRING control keyboard\nDELAY 100\nKEYCOMBO enter\n'),
    ("gen_100_goodbye", '# 通用: 结束演示\nDELAY 500\nSTRING 100 个载荷演示完毕。安全第一!\n'),
]

count = 0
for name, script in P:
    with open(os.path.join(OUT, name + ".txt"), "w", encoding="utf-8") as f:
        f.write(script)
    count += 1
print(f"generated {count} payloads -> {OUT}")
assert count == 100, f"expected 100, got {count}"
