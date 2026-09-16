/* ============================================================
   网页版的 C ↔ JS 胶水层

   设计要点：
   - 所有业务状态留在 C（沿用 train_model.c），JS 只拿 JSON 快照
   - 每个接口都返回**合法 JSON 字符串**，永不返回 NULL。
     cwrap 的 'string' 返回类型遇到 NULL 只会给空串，NULL 语义在 JS 侧不可见，
     所以失败必须靠 ok/code 字段表达，而不是靠空指针。
   - 本文件是唯一 include <emscripten.h> 的地方，train_model.c 保持干净。
   ============================================================ */
#include "train_model.h"
#include <emscripten.h>
#include <stdarg.h>

/* 结构体字段宽度，用 sizeof 取以免和定义脱节。
   参数是**成员名**，不是表达式：SIZE_OF(id) 对，SIZE_OF(p.id) 会展开成
   ((Passenger *)0)->p.id 而编译失败。 */
#define SIZE_OF(member) (sizeof(((Passenger *)0)->member))

/* ---------------- JSON 输出缓冲 ----------------
   用可增长缓冲而不是固定数组：全量快照在记录多时会超过任何拍脑袋定的上限。 */
static char  *json_buf = NULL;
static size_t json_len = 0;
static size_t json_cap = 0;
static int    json_oom = 0;

static const char *JSON_OOM =
    "{\"ok\":false,\"code\":\"no_memory\",\"message\":\"Memory exhausted\"}";

static void json_reset(void) {
    json_len = 0;
    json_oom = 0;
}

static int json_ensure(size_t extra) {
    if (json_len + extra + 1 <= json_cap) return 1;
    size_t want = json_cap ? json_cap : 2048;
    while (want < json_len + extra + 1) {
        if (want > (size_t)1 << 24) return 0;   /* 16MB 上限，防止死循环 */
        want *= 2;
    }
    char *grown = realloc(json_buf, want);
    if (grown == NULL) {
        json_oom = 1;
        return 0;
    }
    json_buf = grown;
    json_cap = want;
    return 1;
}

static void json_putc(char c) {
    if (!json_ensure(1)) return;
    json_buf[json_len++] = c;
}

static void json_puts(const char *text) {
    if (text == NULL) return;
    size_t n = strlen(text);
    if (!json_ensure(n)) return;
    memcpy(json_buf + json_len, text, n);
    json_len += n;
}

static void json_appendf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    va_list probe;
    va_copy(probe, ap);
    int need = vsnprintf(NULL, 0, fmt, probe);
    va_end(probe);
    if (need > 0 && json_ensure((size_t)need)) {
        vsnprintf(json_buf + json_len, json_cap - json_len, fmt, ap);
        json_len += (size_t)need;
    }
    va_end(ap);
}

/* 字符串转义：转义 " \ 与 0x00-0x1F，非 ASCII 字节原样输出。
   姓名里出现引号完全合法（模型只校验非空），不转义会让整个 JSON.parse 抛异常、
   界面直接白屏。 */
static void json_escape(const char *text) {
    json_putc('"');
    if (text != NULL) {
        for (const unsigned char *p = (const unsigned char *)text; *p != '\0'; p++) {
            unsigned char c = *p;
            if (c == '"')       json_puts("\\\"");
            else if (c == '\\') json_puts("\\\\");
            else if (c == '\b') json_puts("\\b");
            else if (c == '\f') json_puts("\\f");
            else if (c == '\n') json_puts("\\n");
            else if (c == '\r') json_puts("\\r");
            else if (c == '\t') json_puts("\\t");
            else if (c < 0x20)  json_appendf("\\u%04x", c);
            else                json_putc((char)c);
        }
    }
    json_putc('"');
}

static void json_key(const char *key) {
    json_escape(key);
    json_putc(':');
}

static void json_str_field(const char *key, const char *value) {
    json_key(key);
    json_escape(value);
}

static void json_int_field(const char *key, int value) {
    json_key(key);
    json_appendf("%d", value);
}

/* 申请失败时宁可返回一个合法的错误对象，也不返回半截 JSON ——
   半截 JSON 会让前端 JSON.parse 抛异常，错误信息全部丢失。 */
static const char *json_finish(void) {
    if (json_oom || json_buf == NULL) return JSON_OOM;
    json_buf[json_len] = '\0';
    return json_buf;
}

/* ---------------- 统一响应 ---------------- */
static const char *resp_ok(const char *message) {
    json_reset();
    json_puts("{\"ok\":true,\"code\":\"ok\"");
    json_puts(",\"message\":");
    json_escape(message);
    json_puts("}");
    return json_finish();
}

static const char *resp_fail(const char *code, const char *message) {
    json_reset();
    json_puts("{\"ok\":false,");
    json_str_field("code", code);
    json_puts(",");
    json_str_field("message", message);
    json_puts("}");
    return json_finish();
}

/* ---------------- 输入守卫 ---------------- */

// 字段能否完整装进定长数组（要为终止符留一字节）
static int field_fits(const char *text, size_t capacity) {
    return text != NULL && strlen(text) < capacity;
}

// 把一条旅客记录写成 JSON 对象
static void write_passenger(const Passenger *p) {
    json_puts("{");
    json_str_field("id", p->id);
    json_puts(",");
    json_str_field("name", p->name);
    json_puts(",");
    json_str_field("date", p->travel_date);
    json_puts(",");
    json_str_field("train", p->train_no);
    json_puts(",");
    json_str_field("depart", p->depart_time);
    json_puts(",");
    json_int_field("board", p->board);
    json_puts(",");
    json_int_field("alight", p->alight);
    json_puts(",");
    json_int_field("price", p->price);
    json_puts(",");
    json_int_field("carriage", p->carriage);
    json_puts(",");
    json_int_field("seat", p->seat);
    json_puts(",");
    json_int_field("firstclass", p->firstclass);
    json_puts("}");
}

/* ============================================================
   接口实现
   ============================================================ */

/* 全量快照：旅客数组 + 汇总。数据量小（几十条），不做分页。 */
EMSCRIPTEN_KEEPALIVE const char *api_state(void) {
    json_reset();
    json_puts("{\"ok\":true,\"code\":\"ok\",");
    json_int_field("count", passenger_count());
    json_puts(",");
    json_int_field("totalFare", total_fare());
    json_puts(",\"passengers\":[");
    int first = 1;
    for (Node *node = head; node != NULL; node = node->next) {
        if (!first) json_putc(',');
        first = 0;
        write_passenger(&node->data);
    }
    json_puts("]}");
    return json_finish();
}

/* 供前端渲染下拉框用的静态选项。日期不在其中 —— 日期一律由 JS 按本地时区算，
   不依赖 C 的 localtime（见 README 的时区说明）。 */
EMSCRIPTEN_KEEPALIVE const char *api_options(void) {
    json_reset();
    json_puts("{\"ok\":true,\"code\":\"ok\",\"stations\":[");
    for (int i = 0; i < STATION_COUNT; i++) {
        if (i > 0) json_putc(',');
        json_escape(stations[i]);
    }
    /* 车次连方向与发车时间一起返回，前端不要再自己推算 ——
       这些规则属于业务逻辑，在 JS 里重写一份迟早会和 C 漂移。 */
    json_puts("],\"trains\":[");
    for (int i = 1; i <= TRAIN_COUNT; i++) {
        char code[6];
        char depart[6];
        snprintf(code, sizeof(code), "G%d", i);
        train_depart_time(i, depart);
        if (i > 1) json_putc(',');
        json_puts("{");
        json_str_field("code", code);
        json_puts(",");
        json_int_field("number", i);
        json_puts(",");
        json_int_field("northbound", train_is_northbound(i));
        json_puts(",");
        json_str_field("depart", depart);
        json_puts("}");
    }
    json_puts("],\"carriages\":[");
    for (int c = 1; c <= CARRIAGE_COUNT; c++) {
        if (c > 1) json_putc(',');
        json_puts("{");
        json_int_field("index", c);
        json_puts(",");
        json_int_field("type", car_type[c]);
        json_puts(",");
        json_int_field("seats", car_seats[c]);
        json_puts("}");
    }
    json_puts("],");
    json_int_field("segmentCount", SEGMENT_COUNT);
    json_puts(",");
    json_int_field("stationCount", STATION_COUNT);
    json_puts("}");
    return json_finish();
}

/* 售票。必须复刻完整校验流水线：
   insert_passenger 只做「重算票价 + 规范化 id 大小写 + 查重」，
   业务校验全是各前端自己做的。少任何一步，坏记录都会进内存并被落盘，
   最终导致下次加载时整份存档被拒收。 */
EMSCRIPTEN_KEEPALIVE const char *api_sell(const char *id, const char *name,
                                          const char *date, const char *train_no,
                                          int board, int alight, int firstclass) {
    char err[160];

    if (id == NULL || name == NULL || date == NULL || train_no == NULL) {
        return resp_fail("invalid_input", "缺少必填参数");
    }
    if (!field_fits(id, SIZE_OF(id))) {
        return resp_fail("invalid_id", "身份证后4位过长");
    }
    if (!valid_id(id)) {
        return resp_fail("invalid_id", "身份证后4位应为 4 位半角数字（末位可为 x）");
    }
    if (name[0] == '\0') {
        return resp_fail("invalid_name", "请输入姓名");
    }
    if (!field_fits(name, SIZE_OF(name))) {
        return resp_fail("invalid_name", "姓名过长：最多 19 字节（约 6 个汉字）");
    }
    if (search_passenger(id) != NULL) {
        return resp_fail("duplicate", "该身份证已经购票");
    }
    if (!validate_trip(train_no, date, board, alight, firstclass, err, sizeof(err))) {
        return resp_fail("invalid_trip", err);
    }

    int carriage = -1;
    int seat = -1;
    if (!assign_seat(train_no, date, firstclass, board, alight, &carriage, &seat)) {
        return resp_fail("sold_out", "该区间该等级座位已售罄，建议改乘其他车次");
    }

    Passenger p = {0};
    strncpy(p.id, id, SIZE_OF(id) - 1);
    strncpy(p.name, name, SIZE_OF(name) - 1);
    strncpy(p.travel_date, date, SIZE_OF(travel_date) - 1);
    strncpy(p.train_no, train_no, SIZE_OF(train_no) - 1);
    train_depart_time(train_number_of(p.train_no), p.depart_time);
    p.board = board;
    p.alight = alight;
    p.firstclass = firstclass;
    p.price = calc_price(board, alight, firstclass);
    p.carriage = carriage;
    p.seat = seat;

    InsertResult inserted = insert_passenger(p);
    if (inserted == INSERT_DUPLICATE) {
        return resp_fail("duplicate", "该身份证已经购票");
    }
    if (inserted == INSERT_NO_MEMORY) {
        return resp_fail("no_memory", "内存不足，购票失败");
    }

    json_reset();
    json_puts("{\"ok\":true,\"code\":\"ok\",");
    json_int_field("carriage", carriage);
    json_puts(",");
    json_int_field("seat", seat);
    json_puts(",");
    json_int_field("price", p.price);
    json_puts(",");
    json_int_field("count", passenger_count());
    json_puts(",\"message\":");
    json_appendf("\"购票成功：%d号车厢 %d号座位，票价 %d 元\"", carriage, seat, p.price);
    json_puts("}");
    return json_finish();
}

EMSCRIPTEN_KEEPALIVE const char *api_refund(const char *id) {
    if (id == NULL || id[0] == '\0') {
        return resp_fail("invalid_id", "请提供身份证后4位");
    }
    Node *found = search_passenger(id);
    if (found == NULL) {
        return resp_fail("not_found", "未找到该旅客");
    }
    /* delete_passenger 会 free 掉这个结点，姓名必须先拷出来再用 */
    char refunded_name[SIZE_OF(name)];
    memcpy(refunded_name, found->data.name, sizeof(refunded_name));
    refunded_name[sizeof(refunded_name) - 1] = '\0';

    if (!delete_passenger(id)) {
        return resp_fail("not_found", "未找到该旅客");
    }
    json_reset();
    json_puts("{\"ok\":true,\"code\":\"ok\",");
    json_int_field("count", passenger_count());
    json_puts(",");
    json_str_field("name", refunded_name);
    json_puts("}");
    return json_finish();
}

/* 各车厢占用情况。区段复用下「已售 N/12」会出现超过总座位数的自相矛盾数字，
   因此拆成三类：总座位、全程空座、有被占用过的座位；另外单列实际人数。 */
EMSCRIPTEN_KEEPALIVE const char *api_carriages(const char *train_no, const char *date,
                                               int firstclass) {
    if (train_no == NULL || date == NULL) {
        return resp_fail("invalid_input", "缺少必填参数");
    }
    json_reset();
    json_puts("{\"ok\":true,\"code\":\"ok\",\"carriages\":[");
    int first = 1;
    for (int c = 1; c <= CARRIAGE_COUNT; c++) {
        if (car_type[c] != (firstclass ? 1 : 2)) continue;
        if (!first) json_putc(',');
        first = 0;
        json_puts("{");
        json_int_field("index", c);
        json_puts(",");
        json_int_field("type", car_type[c]);
        json_puts(",");
        json_int_field("seats", car_seats[c]);
        json_puts(",");
        json_int_field("free", carriage_fully_free_seats(train_no, date, c));
        json_puts(",");
        json_int_field("occupied", carriage_occupied_seats(train_no, date, c));
        json_puts(",");
        json_int_field("headcount", carriage_headcount(train_no, date, c));
        json_puts("}");
    }
    json_puts("]}");
    return json_finish();
}

/* 各区段载客数，恒在 0~总座位数之间 */
EMSCRIPTEN_KEEPALIVE const char *api_segments(const char *train_no, const char *date) {
    if (train_no == NULL || date == NULL) {
        return resp_fail("invalid_input", "缺少必填参数");
    }
    int capacity = 0;
    for (int c = 1; c <= CARRIAGE_COUNT; c++) capacity += car_seats[c];

    json_reset();
    json_puts("{\"ok\":true,\"code\":\"ok\",");
    json_int_field("capacity", capacity);
    json_puts(",\"segments\":[");
    for (int i = 0; i < SEGMENT_COUNT; i++) {
        if (i > 0) json_putc(',');
        json_puts("{");
        json_int_field("index", i);
        json_puts(",");
        json_int_field("from", i);
        json_puts(",");
        json_int_field("to", i + 1);
        json_puts(",");
        json_int_field("load", segment_load(train_no, date, i));
        json_puts("}");
    }
    json_puts("]}");
    return json_finish();
}

/* 票价查询：前端预览用。刻意做成接口而不是在 JS 里重写一遍 calc_price，
   避免计价规则改动后两边不一致。 */
EMSCRIPTEN_KEEPALIVE const char *api_price(int board, int alight, int firstclass) {
    if (board < 0 || board >= STATION_COUNT || alight < 0 || alight >= STATION_COUNT) {
        return resp_fail("invalid_trip", "车站编号越界");
    }
    json_reset();
    json_puts("{\"ok\":true,\"code\":\"ok\",");
    json_int_field("price", calc_price(board, alight, firstclass));
    json_puts(",");
    json_int_field("segments", board > alight ? board - alight : alight - board);
    json_puts("}");
    return json_finish();
}

EMSCRIPTEN_KEEPALIVE const char *api_seats_left(const char *train_no, const char *date,
                                                int board, int alight, int firstclass) {
    if (train_no == NULL || date == NULL) {
        return resp_fail("invalid_input", "缺少必填参数");
    }
    char err[160];
    if (!validate_trip(train_no, date, board, alight, firstclass, err, sizeof(err))) {
        return resp_fail("invalid_trip", err);
    }
    json_reset();
    json_puts("{\"ok\":true,\"code\":\"ok\",");
    json_int_field("seats", seats_available(train_no, date, firstclass, board, alight));
    json_puts("}");
    return json_finish();
}

/* 清理出行日期早于 today 的旅客。
   走 purge_expired_passengers：它会做年份闸门，并在真正删除前把当前存档
   另存为 <path>.before-expire-<时间戳>，误删之后还能捞回来。

   注意这里**不做**「today 早于最晚出行日期就判时钟倒流」那种检查 ——
   用户买了明天的票时 that 判据会误触发，把正常清理永久挡住。 */
EMSCRIPTEN_KEEPALIVE const char *api_expire(const char *today, const char *archive_path) {
    if (today == NULL || !valid_date(today)) {
        return resp_fail("invalid_date", "日期格式不正确");
    }
    char reason[128];
    int removed = purge_expired_passengers(today, archive_path, reason, sizeof(reason));

    json_reset();
    json_puts("{\"ok\":true,\"code\":\"ok\",");
    json_int_field("removed", removed);
    json_puts(",");
    json_int_field("count", passenger_count());
    json_puts(",");
    json_str_field("reason", reason);
    json_puts(",");
    json_str_field("backup", last_purge_backup_path());
    json_puts("}");
    return json_finish();
}

/* 存档路径由 JS 传入（MEMFS 里用 "/passengers.dat"）。
   刻意不调 default_data_file()：它返回 "D:/train_system/passengers.dat"，
   在 MEMFS 里父目录不存在，fopen 会直接失败。 */
EMSCRIPTEN_KEEPALIVE const char *api_load(const char *path) {
    if (path == NULL || path[0] == '\0') {
        return resp_fail("invalid_input", "缺少存档路径");
    }
    switch (load_passengers(path)) {
        case LOAD_OK:
            json_reset();
            json_puts("{\"ok\":true,\"result\":\"loaded\",");
            json_int_field("count", passenger_count());
            json_puts("}");
            return json_finish();
        case LOAD_NO_FILE:
            /* 首次运行，没有存档。这是唯一可以放心写入的状态之一。
               result 字段是前端区分「没有存档」与「存档损坏」的依据，
               损坏时走的是 code:"rejected" 分支。 */
            json_reset();
            json_puts("{\"ok\":true,\"code\":\"ok\",\"result\":\"empty\"}");
            return json_finish();
        case LOAD_NO_MEMORY:
            return resp_fail("no_memory", "内存不足，无法读取存档");
        case LOAD_REJECTED:
        default:
            /* JS 侧收到这个必须进入只读熔断态，禁止一切写回 ——
               否则会用一份空存档覆盖掉用户的 localStorage。 */
            return resp_fail("rejected", "存档未通过校验，已停止写入以保护原始数据");
    }
}

EMSCRIPTEN_KEEPALIVE const char *api_save(const char *path) {
    if (path == NULL || path[0] == '\0') {
        return resp_fail("invalid_input", "缺少存档路径");
    }
    if (!save_passengers(path)) {
        return resp_fail("storage_failed", "保存失败，本次改动仅在内存中");
    }
    return resp_ok("saved");
}

/* 诊断用：报告 C 视角的今天。前端拿它和自己按本地时区算的日期比对，
   不一致就告警。业务逻辑不要用它 —— 它依赖 Emscripten 的 localtime，
   在跨时区和系统时钟异常时不可靠。 */
EMSCRIPTEN_KEEPALIVE const char *api_diagnostic_today(void) {
    char today[11];
    today_string(today);
    json_reset();
    json_puts("{\"ok\":true,\"code\":\"ok\",");
    json_str_field("today", today);
    json_puts("}");
    return json_finish();
}
