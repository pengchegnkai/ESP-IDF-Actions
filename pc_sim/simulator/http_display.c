/* http_display.c — 浏览器显示 + 触摸通道实现
 *
 * [纯逻辑·可仿真] LVGL 直绘帧缓冲 (RGB565) → 24bit BMP → HTTP 推给手机浏览器;
 * 浏览器 pointer 事件经 /touch 注入 LVGL 指针 indev. 单线程非阻塞, 无依赖.
 */
#include "http_display.h"

#include "lvgl.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int s_w, s_h;                 /* 帧缓冲尺寸 */
static uint8_t *s_fb;                /* RGB565 帧缓冲 (LVGL 直绘) */
static lv_indev_t *s_indev;
static int s_listen = -1;

/* 触摸状态: HTTP 写入, indev 回调消费 */
static volatile int s_down;
static volatile int s_tx, s_ty;

/* ---------------- LVGL 显示与触摸 ---------------- */

static void flush_cb(lv_display_t *d, const lv_area_t *area, uint8_t *px_map)
{
    lv_display_flush_ready(d); /* DIRECT 模式直绘帧缓冲, 无需拷贝 */
    (void)area;
    (void)px_map;
}

static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    data->point.x = (int16_t)s_tx;
    data->point.y = (int16_t)s_ty;
    data->state = s_down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    /* 诊断: 状态翻转时打印 (确认 indev 被轮询与坐标正确) */
    static int last_state = -1;
    if ((int)data->state != last_state) {
        fprintf(stderr, "[indev] poll state=%d at (%d,%d)\n",
                data->state, data->point.x, data->point.y);
        last_state = (int)data->state;
    }
    (void)indev;
}

/* ---------------- BMP 编码 (RGB565 → 24bit) ---------------- */

static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static size_t bmp_build(uint8_t *out)
{
    /* 24bpp BMP: 文件头 54B + 像素 (行按 4B 对齐, 自下而上) */
    const int row = (s_w * 3 + 3) & ~3;
    const size_t data = (size_t)row * s_h;
    const size_t total = 54 + data;
    memset(out, 0, 54);
    out[0] = 'B'; out[1] = 'M';
    put32(out + 2, (uint32_t)total);
    put32(out + 10, 54);
    put32(out + 14, 40);
    put32(out + 18, (uint32_t)s_w);
    put32(out + 22, (uint32_t)s_h);
    put16(out + 26, 1);
    put16(out + 28, 24);
    put32(out + 34, (uint32_t)data);

    uint8_t *px = out + 54;
    for (int y = 0; y < s_h; y++) {          /* BMP 自下而上 */
        uint8_t *rowp = px + (size_t)(s_h - 1 - y) * row;
        const uint16_t *src = (const uint16_t *)(s_fb + (size_t)y * s_w * 2);
        for (int x = 0; x < s_w; x++) {
            uint16_t c = src[x];
            rowp[x * 3 + 0] = (uint8_t)(((c & 0xF800) >> 11) * 255 / 31); /* R */
            rowp[x * 3 + 1] = (uint8_t)(((c & 0x07E0) >> 5) * 255 / 63);  /* G */
            rowp[x * 3 + 2] = (uint8_t)((c & 0x001F) * 255 / 31);         /* B */
        }
    }
    return total;
}

/* ---------------- 极简 HTTP ---------------- */

static const char PAGE[] =
    "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"
    "<!DOCTYPE html><html><head><meta name=viewport content=\"width=device-width,initial-scale=1\">"
    "<style>body{margin:0;background:#000;display:flex;justify-content:center}"
    "img{height:98vh}</style></head><body>"
    "<img id=f src=\"/fb.bmp\">"
    "<script>const f=document.getElementById('f');"
    "setInterval(()=>{f.src='/fb.bmp?t='+Date.now()},300);"
    "function pos(e){const r=f.getBoundingClientRect();"
    "return{x:(e.clientX-r.left)/r.width*480,y:(e.clientY-r.top)/r.height*800}}"
    "function p(e){e.preventDefault();const q=pos(e);fetch('/touch?x='+q.x+'&y='+q.y+'&d=1')}"
    "function r(e){e.preventDefault();const q=pos(e);fetch('/touch?x='+q.x+'&y='+q.y+'&d=0')}"
    "f.addEventListener('pointerdown',p);f.addEventListener('pointermove',p);"
    "f.addEventListener('pointerup',r);f.addEventListener('pointerleave',r);"
    "</script></body></html>";

static void serve(int fd, const char *req)
{
    if (strncmp(req, "GET /fb.bmp", 11) == 0) {
        static uint8_t *bmp; /* 单连接顺序服务, 静态缓冲够用 */
        if (!bmp) bmp = malloc(54 + (size_t)s_w * 3 * s_h + 64);
        size_t n = bmp_build(bmp);
        char hdr[128]; /* 69 字节实际头 — 64 会截断掉头结束符, 导致 0 字节下载 */
        int hn = snprintf(hdr, sizeof(hdr), "HTTP/1.1 200 OK\r\nContent-Type: image/bmp\r\n"
                          "Content-Length: %zu\r\n\r\n", n);
        fprintf(stderr, "[http] fb.bmp: bmp_build=%zu hdr=%d\n", n, hn);
        if (write(fd, hdr, hn) > 0) {
            size_t sent = 0; /* 循环写: write 可能部分发送 */
            while (sent < n) {
                ssize_t w = write(fd, bmp + sent, n - sent);
                if (w <= 0) {
                    fprintf(stderr, "[http] fb.bmp body write=%zd at %zu/%zu errno=%d (%s)\n",
                            w, sent, n, errno, strerror(errno));
                    break;
                }
                sent += (size_t)w;
            }
            fprintf(stderr, "[http] fb.bmp sent=%zu/%zu\n", sent, n);
        }
        return;
    }
    if (strncmp(req, "GET /touch?", 11) == 0) {
        int x, y, d;
        if (sscanf(req, "GET /touch?x=%d&y=%d&d=%d", &x, &y, &d) == 3) {
            s_tx = x; s_ty = y; s_down = d;
        }
        if (write(fd, "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok", 44) < 0) {
        }
        return;
    }
    if (strncmp(req, "GET /", 5) == 0 || strncmp(req, "GET /index", 10) == 0) {
        if (write(fd, PAGE, sizeof(PAGE) - 1) < 0) {
        }
        return;
    }
    if (write(fd, "HTTP/1.1 404\r\n\r\n", 16) < 0) {
    }
}

/* 解析 /touch 请求并更新触摸状态 — 必须在父进程做 (fork 子进程的写入会随进程消亡) */
static void parse_touch(const char *req)
{
    int x, y, d;
    if (sscanf(req, "GET /touch?x=%d&y=%d&d=%d", &x, &y, &d) == 3) {
        s_tx = x; s_ty = y; s_down = d;
        if (d) fprintf(stderr, "[http] tap (%d,%d)\n", x, y);
    }
}

void http_display_poll(void)
{
    if (s_listen < 0) return;
    int fd = accept(s_listen, NULL, NULL);
    if (fd < 0) return;
    char req[512];
    ssize_t n = read(fd, req, sizeof(req) - 1);
    if (n > 0) {
        req[n] = '\0';
        if (strncmp(req, "GET /touch", 10) == 0) {
            parse_touch(req);
            if (write(fd, "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok", 44) < 0) {
            }
            close(fd);
            return;
        }
        /* 大体积响应 (帧 BMP/页面) 交给子进程, 浏览器中止连接不影响主循环 */
        pid_t pid = fork();
        if (pid == 0) {
            serve(fd, req);
            _exit(0);
        }
    }
    close(fd);
}

int http_display_start(int w, int h)
{
    signal(SIGPIPE, SIG_IGN);
    signal(SIGCHLD, SIG_IGN); /* fork-per-connection 的子进程自动回收, 无僵尸 */

    s_w = w; s_h = h;
    s_fb = malloc((size_t)w * h * 2);
    if (!s_fb) return -1;
    memset(s_fb, 0, (size_t)w * h * 2);

    lv_display_t *d = lv_display_create(w, h);
    lv_display_set_buffers(d, s_fb, NULL, (size_t)w * h * 2, LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(d, flush_cb);

    s_indev = lv_indev_create();
    lv_indev_set_type(s_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(s_indev, touch_read_cb);
    lv_indev_set_display(s_indev, d); /* 9.2+: 未关联 display 的 indev 不会被轮询 */

    struct sockaddr_in addr = { 0 };
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(8080);
    s_listen = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    setsockopt(s_listen, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    if (bind(s_listen, (struct sockaddr *)&addr, sizeof(addr)) != 0 || listen(s_listen, 4) != 0) {
        perror("http bind");
        return -1;
    }
    printf("═══ 浏览器打开 http://127.0.0.1:8080 即可亲手测试 ═══\n");
    return 0;
}
