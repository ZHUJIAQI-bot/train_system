/* ============================================================
   模型层单元测试 —— 不依赖 raylib，可离线构建运行
   覆盖：区段重叠判定、车次解析、行程校验、座位复用、
         等级约束、容量边界、索引一致性、统计口径
   ============================================================ */
#include "../train_model.h"

static int checks = 0;
static int failures = 0;

static void check(int condition, const char *what) {
    checks++;
    if (!condition) {
        failures++;
        printf("  FAIL: %s\n", what);
    }
}

static void reset(void) {
    free_all_passengers();
}

// 构造一条记录（仅用于摆放状态，不经过校验）
static Passenger make(const char *id, const char *date, const char *train,
                      int board, int alight, int firstclass,
                      int carriage, int seat) {
    Passenger p = {0};
    snprintf(p.id, sizeof(p.id), "%s", id);
    snprintf(p.name, sizeof(p.name), "T%s", id);
    snprintf(p.travel_date, sizeof(p.travel_date), "%s", date);
    snprintf(p.train_no, sizeof(p.train_no), "%s", train);
    train_depart_time(train_number_of(train), p.depart_time);
    p.board = board;
    p.alight = alight;
    p.firstclass = firstclass;
    p.price = calc_price(board, alight, firstclass);
    p.carriage = carriage;
    p.seat = seat;
    return p;
}

/* ---------------- 区段重叠 ---------------- */
static void test_overlap(void) {
    printf("区段重叠判定\n");

    // 半开区间 [lo,hi)：旅客在 alight 站下车，不占用该站之后的区段
    check(seat_segments_overlap(0, 2, 1, 3), "上海→南京 与 苏州→济南 应重叠");
    check(!seat_segments_overlap(0, 2, 2, 5), "上海→南京 与 南京→北京 不重叠");
    check(!seat_segments_overlap(0, 2, 3, 5), "上海→南京 与 济南→北京 不重叠");
    check(seat_segments_overlap(0, 5, 1, 2), "全程 与 苏州→南京 应重叠");

    // 关键回归：奇数车次 board > alight。
    // 朴素写法 b1<a2 && a1>b2 在此返回「不重叠」，会造成同座位重复售票。
    check(seat_segments_overlap(5, 0, 3, 0), "北京→上海 与 济南→上海 应重叠（反向区间）");
    check(seat_segments_overlap(5, 0, 5, 3), "北京→上海 与 北京→济南 应重叠");
    check(!seat_segments_overlap(5, 2, 2, 0), "北京→南京 与 南京→上海 不重叠");
    check(!seat_segments_overlap(5, 3, 3, 0), "北京→济南 与 济南→上海 不重叠");
    check(seat_segments_overlap(4, 1, 3, 2), "天津→苏州 与 济南→南京 应重叠");
    check(seat_segments_overlap(3, 0, 5, 0) == seat_segments_overlap(5, 0, 3, 0),
          "重叠判定应对称");
}

/* ---------------- 车次 ---------------- */
static void test_train(void) {
    printf("车次解析与发车时间\n");
    char t[6];

    check(train_number_of("G1") == 1, "G1 → 1");
    check(train_number_of("G10") == 10, "G10 → 10");
    check(train_number_of("G0") == 0, "G0 非法");
    check(train_number_of("G11") == 0, "G11 非法");
    check(train_number_of("X1") == 0, "X1 非法");
    check(train_number_of("G01") == 0, "G01 非法（前导0）");
    check(train_number_of("G1abc") == 0, "G1abc 非法");
    check(train_number_of("") == 0, "空串非法");
    check(!valid_train_no("G0000000001"),
          "G0000000001 非法（曾可绕过 atoi 校验导致 train_no 栈溢出）");

    train_depart_time(1, t);  check(strcmp(t, "06:00") == 0, "G1 → 06:00");
    train_depart_time(2, t);  check(strcmp(t, "06:30") == 0, "G2 → 06:30");
    train_depart_time(9, t);  check(strcmp(t, "10:00") == 0, "G9 → 10:00");
    train_depart_time(10, t); check(strcmp(t, "10:30") == 0, "G10 → 10:30");
}

/* ---------------- 行程校验 ---------------- */
static void test_trip(void) {
    printf("行程校验\n");
    char err[160];

    check(validate_trip("G2", "2026-09-16", 0, 2, 0, err, sizeof(err)), "G2 上海→北京 合法");
    check(validate_trip("G1", "2026-09-16", 5, 2, 0, err, sizeof(err)), "G1 北京→上海 合法");
    check(!validate_trip("G1", "2026-09-16", 0, 2, 0, err, sizeof(err)), "G1 走上海→北京 方向不符");
    check(!validate_trip("G2", "2026-09-16", 5, 2, 0, err, sizeof(err)), "G2 走北京→上海 方向不符");
    check(!validate_trip("G2", "2026-09-16", 2, 2, 0, err, sizeof(err)), "上下车站相同");
    check(!validate_trip("G2", "2026-09-16", 0, 99, 0, err, sizeof(err)), "车站越界");
    check(!validate_trip("G2", "2026-13-01", 0, 2, 0, err, sizeof(err)), "月份非法");
    check(!validate_trip("G2", "2026-02-30", 0, 2, 0, err, sizeof(err)), "2月30日非法");
    check(!validate_trip("G2", "2026-09-16", 0, 2, 2, err, sizeof(err)), "等级非法");
    check(validate_trip("G2", "2024-02-29", 0, 2, 0, err, sizeof(err)), "闰年2月29 合法");
    check(!validate_trip("G2", "2023-02-29", 0, 2, 0, err, sizeof(err)), "平年2月29 非法");
}

/* ---------------- 座位区段复用 ---------------- */
static void test_seat_reuse(void) {
    printf("座位区段复用\n");
    int c, s;

    reset();
    insert_passenger(make("1111", "2026-09-16", "G2", 0, 2, 0, 3, 1));
    check(assign_seat("G2", "2026-09-16", 0, 3, 5, &c, &s), "区段不相交的第二位旅客应能买到座");
    check(c == 3 && s == 1, "区段不相交 → 应复用 车厢3 1号座");

    reset();
    insert_passenger(make("1111", "2026-09-16", "G2", 0, 2, 0, 3, 1));
    check(assign_seat("G2", "2026-09-16", 0, 1, 4, &c, &s), "区段相交的旅客应能买到座");
    check(!(c == 3 && s == 1), "区段相交 → 不能复用同一座位");

    reset();
    insert_passenger(make("1111", "2026-09-16", "G2", 0, 5, 0, 3, 1));
    check(assign_seat("G2", "2026-09-17", 0, 0, 5, &c, &s), "次日同一行程应能买到座");
    check(c == 3 && s == 1, "换日期 → 同一座位可再售");

    reset();
    insert_passenger(make("1111", "2026-09-16", "G2", 0, 5, 0, 3, 1));
    check(assign_seat("G4", "2026-09-16", 0, 0, 5, &c, &s), "其他车次同一行程应能买到座");
    check(c == 3 && s == 1, "换车次 → 同一座位可再售");

    // 奇数车次的两个方向都要覆盖：只用偶数车次测不出反向区间的 bug
    reset();
    insert_passenger(make("1111", "2026-09-16", "G1", 5, 2, 0, 3, 1));  // 北京→南京 [2,5)
    check(assign_seat("G1", "2026-09-16", 0, 2, 0, &c, &s), "G1 南京→上海 应能买到座");
    check(c == 3 && s == 1, "G1 区段不相交 → 应复用同一座位");

    reset();
    insert_passenger(make("1111", "2026-09-16", "G1", 5, 0, 0, 3, 1));  // 北京→上海 [0,5)
    check(assign_seat("G1", "2026-09-16", 0, 3, 0, &c, &s), "G1 济南→上海 应能买到座");
    check(!(c == 3 && s == 1), "G1 区段重叠 → 不能复用（奇数车次回归）");
}

/* ---------------- 等级约束与容量边界 ---------------- */
static void test_class_and_capacity(void) {
    printf("等级约束与容量边界\n");
    int c, s;
    char id[5];

    // 二等座共 12*3 = 36 个，车厢 3/4/5
    reset();
    for (int i = 0; i < 36; i++) {
        snprintf(id, sizeof(id), "%04d", i);
        check(assign_seat("G2", "2026-09-16", 0, 0, 5, &c, &s), "二等座应能分配");
        check(car_type[c] == 2, "二等座不能落在车厢1/2（一等座车厢）");
        insert_passenger(make(id, "2026-09-16", "G2", 0, 5, 0, c, s));
    }
    check(!assign_seat("G2", "2026-09-16", 0, 0, 5, &c, &s), "36 张全段重叠票后二等座应售罄");
    check(seats_available("G2", "2026-09-16", 0, 0, 5) == 0, "售罄时可售数应为 0");

    // 退票后立刻能再卖 —— 座位是派生量，不该有残留状态
    check(delete_passenger("0000"), "删除旅客应成功");
    check(seats_available("G2", "2026-09-16", 0, 0, 5) == 1, "退票后可售数应恢复为 1");
    check(assign_seat("G2", "2026-09-16", 0, 0, 5, &c, &s), "退票后应能再售");
    check(c == 3 && s == 1, "应重新用回刚释放的 车厢3 1号座");

    // 一等座共 8*2 = 16 个，车厢 1/2
    reset();
    for (int i = 0; i < 16; i++) {
        snprintf(id, sizeof(id), "%04d", i);
        check(assign_seat("G2", "2026-09-16", 1, 0, 5, &c, &s), "一等座应能分配");
        check(car_type[c] == 1, "一等座不能落在车厢3/4/5（二等座车厢）");
        insert_passenger(make(id, "2026-09-16", "G2", 0, 5, 1, c, s));
    }
    check(!assign_seat("G2", "2026-09-16", 1, 0, 5, &c, &s), "16 张全段重叠票后一等座应售罄");
    check(assign_seat("G2", "2026-09-16", 0, 0, 5, &c, &s), "一等座售罄不应影响二等座");
}

/* ---------------- 索引一致性 ---------------- */
static void test_indexes(void) {
    printf("索引一致性\n");
    char id[5];

    reset();
    for (int i = 0; i < 200; i++) {
        snprintf(id, sizeof(id), "%04d", i);
        insert_passenger(make(id, "2026-09-16", "G2", 0, 2, 0, 3, (i % 12) + 1));
    }
    check(passenger_count() == 200, "链表应有 200 个结点");
    check(indexes_are_consistent(), "插入 200 条后两棵索引应与链表一致");

    for (int i = 0; i < 200; i += 2) {
        snprintf(id, sizeof(id), "%04d", i);
        delete_passenger(id);
    }
    check(passenger_count() == 100, "删除 100 条后应剩 100 个结点");
    check(indexes_are_consistent(), "删除后两棵索引仍应一致");

    int hit = 1;
    for (int i = 1; i < 200; i += 2) {
        snprintf(id, sizeof(id), "%04d", i);
        Node *n = search_passenger(id);
        if (n == NULL || strcmp(n->data.id, id) != 0) hit = 0;
    }
    check(hit, "在册的身份证都应能查到正确结点");

    int ghost = 0;
    for (int i = 0; i < 200; i += 2) {
        snprintf(id, sizeof(id), "%04d", i);
        if (search_passenger(id) != NULL) ghost = 1;
    }
    check(!ghost, "已删除的身份证不应留下幽灵记录");
}

/* ---------------- 统计口径 ---------------- */
static void test_stats(void) {
    printf("统计口径\n");
    reset();
    insert_passenger(make("1111", "2026-09-16", "G2", 0, 2, 0, 3, 1));
    insert_passenger(make("2222", "2026-09-16", "G2", 3, 5, 0, 3, 1));  // 与前一条复用同一座位
    insert_passenger(make("3333", "2026-09-16", "G2", 1, 4, 0, 4, 1));

    check(segment_load("G2", "2026-09-16", 0) == 1, "上海-苏州 载客 1");
    check(segment_load("G2", "2026-09-16", 1) == 2, "苏州-南京 载客 2");
    check(segment_load("G2", "2026-09-16", 2) == 1, "南京-济南 载客 1");
    check(segment_load("G2", "2026-09-16", 3) == 2, "济南-天津 载客 2");
    check(segment_load("G2", "2026-09-16", 4) == 1, "天津-北京 载客 1");
    check(segment_load("G2", "2026-09-15", 0) == 0, "别的日期不串数据");

    // 车厢3 的 1 号座被两条记录共用，占用数只能算 1
    check(carriage_occupied_seats("G2", "2026-09-16", 3) == 1, "车厢3 只应算 1 个占用座位");
    check(carriage_fully_free_seats("G2", "2026-09-16", 3) == car_seats[3] - 1,
          "车厢3 空座数应为 总座位-1");
    check(occupied_seat_total("2026-09-16") == 2, "去重后占用座位数应为 2");
    check(occupied_seat_total("2026-09-15") == 0, "其他日期无占用");

    int expected = calc_price(0, 2, 0) + calc_price(3, 5, 0) + calc_price(1, 4, 0);
    check(total_fare() == expected, "票款合计应为三张票之和");

    // 退票后票款随之减少（口径：只统计当前在车旅客）
    delete_passenger("3333");
    check(total_fare() == expected - calc_price(1, 4, 0), "退票后票款应减少");
}

/* ---------------- 存档损坏用例 ----------------
   v4 记录布局（相对记录起点）：
     id 0 | name 5 | travel_date 25 | train_no 36 | depart_time 40
     board 46 | alight 50 | price 54 | carriage 58 | seat 62 | firstclass 66
   文件布局：16 字节头 + count × 70 字节记录                      */
#define ARCHIVE_PATH "test_archive.dat"
#define RECORD_BASE(i) (16L + (long)(i) * 70L)

static void put_i32_le(FILE *file, int value) {
    unsigned char bytes[4];
    bytes[0] = (unsigned char)(value & 0xFF);
    bytes[1] = (unsigned char)((value >> 8) & 0xFF);
    bytes[2] = (unsigned char)((value >> 16) & 0xFF);
    bytes[3] = (unsigned char)((value >> 24) & 0xFF);
    fwrite(bytes, 1, 4, file);
}

static void put_fixed(FILE *file, const char *text, size_t size) {
    char buffer[32];
    memset(buffer, 0, size);
    memcpy(buffer, text, strlen(text) < size ? strlen(text) : size);
    fwrite(buffer, 1, size, file);
}

// 按 v4 格式写一份存档
static void write_archive(const char *path, const Passenger *records, int count) {
    FILE *file = fopen(path, "wb");
    if (file == NULL) return;
    fwrite("TRNP", 1, 4, file);
    put_i32_le(file, 4);
    put_i32_le(file, count);
    put_i32_le(file, 70);
    for (int i = 0; i < count; i++) {
        put_fixed(file, records[i].id, 5);
        put_fixed(file, records[i].name, 20);
        put_fixed(file, records[i].travel_date, 11);
        put_fixed(file, records[i].train_no, 4);
        put_fixed(file, records[i].depart_time, 6);
        put_i32_le(file, records[i].board);
        put_i32_le(file, records[i].alight);
        put_i32_le(file, records[i].price);
        put_i32_le(file, records[i].carriage);
        put_i32_le(file, records[i].seat);
        put_i32_le(file, records[i].firstclass);
    }
    fclose(file);
}

// 就地改写文件里的若干字节，用来模拟损坏
static void poke_bytes(const char *path, long offset,
                       const unsigned char *bytes, size_t n) {
    FILE *file = fopen(path, "r+b");
    if (file == NULL) return;
    if (fseek(file, offset, SEEK_SET) == 0) fwrite(bytes, 1, n, file);
    fclose(file);
}

static void test_archive(void) {
    printf("存档读写与损坏拒收\n");
    Passenger good = make("1111", "2026-09-16", "G2", 0, 2, 0, 3, 1);
    Passenger second = make("2222", "2026-09-16", "G2", 3, 5, 0, 3, 1);

    // --- 正常往返 ---
    write_archive(ARCHIVE_PATH, &good, 1);
    reset();
    check(load_passengers(ARCHIVE_PATH) == LOAD_OK, "合法 v4 文件应能读入");
    check(passenger_count() == 1, "应读入 1 条记录");
    check(indexes_are_consistent(), "读入后两棵索引应一致");

    // --- 文件不存在 ---
    check(load_passengers("no_such_file_here.dat") == LOAD_NO_FILE,
          "文件不存在应返回 LOAD_NO_FILE");

    // --- 单人字段级损坏 ---
    struct { const char *what; int field_off; int value; } int_cases[] = {
        {"board=99（旧代码会 board_cnt[99]++ 越界）", 46, 99},
        {"alight=6 越界",              50, 6},
        {"board==alight",              46, 2},
        {"carriage=0",                 58, 0},
        {"carriage=6",                 58, 6},
        {"seat=0",                     62, 0},
        {"seat=car_seats+1",           62, 13},
        {"firstclass=2",               66, 2},
        {"level 与车厢不符（二等票进一等车厢）", 58, 1},
    };
    for (size_t i = 0; i < sizeof(int_cases) / sizeof(int_cases[0]); i++) {
        write_archive(ARCHIVE_PATH, &good, 1);
        FILE *file = fopen(ARCHIVE_PATH, "r+b");
        fseek(file, RECORD_BASE(0) + int_cases[i].field_off, SEEK_SET);
        put_i32_le(file, int_cases[i].value);
        fclose(file);
        check(load_passengers(ARCHIVE_PATH) == LOAD_REJECTED, int_cases[i].what);
    }

    // board==alight 的用例需要 alight 与 board 相同
    write_archive(ARCHIVE_PATH, &good, 1);
    {
        FILE *file = fopen(ARCHIVE_PATH, "r+b");
        fseek(file, RECORD_BASE(0) + 50, SEEK_SET);
        put_i32_le(file, 0);
        fclose(file);
    }
    check(load_passengers(ARCHIVE_PATH) == LOAD_REJECTED, "board==alight 应被拒收");

    // --- 车次与方向 ---
    {
        Passenger bad = good;
        snprintf(bad.train_no, sizeof(bad.train_no), "G11");
        write_archive(ARCHIVE_PATH, &bad, 1);
        check(load_passengers(ARCHIVE_PATH) == LOAD_REJECTED, "车次 G11 应被拒收");

        bad = good;
        snprintf(bad.train_no, sizeof(bad.train_no), "G1");   // G1 是北京→上海，与 0→2 方向不符
        write_archive(ARCHIVE_PATH, &bad, 1);
        check(load_passengers(ARCHIVE_PATH) == LOAD_REJECTED, "车次方向与行程不符应被拒收");

        bad = good;
        snprintf(bad.depart_time, sizeof(bad.depart_time), "09:99");  // 与 G2 的 06:30 不符
        write_archive(ARCHIVE_PATH, &bad, 1);
        check(load_passengers(ARCHIVE_PATH) == LOAD_REJECTED, "发车时间与车次不符应被拒收");
    }

    // --- 定长字段界内没有 NUL 终止符（旧代码会在这里 strlen 越界读） ---
    write_archive(ARCHIVE_PATH, &good, 1);
    {
        unsigned char no_nul[5] = {'1', '2', '3', '4', '5'};
        poke_bytes(ARCHIVE_PATH, RECORD_BASE(0), no_nul, 5);
    }
    check(load_passengers(ARCHIVE_PATH) == LOAD_REJECTED,
          "id 字段无终止符应被拒收且不崩溃");

    write_archive(ARCHIVE_PATH, &good, 1);
    {
        unsigned char no_nul[20];
        memset(no_nul, 'A', sizeof(no_nul));
        poke_bytes(ARCHIVE_PATH, RECORD_BASE(0) + 5, no_nul, 20);
    }
    check(load_passengers(ARCHIVE_PATH) == LOAD_REJECTED,
          "name 字段无终止符应被拒收且不崩溃");

    // --- 文件头损坏 ---
    write_archive(ARCHIVE_PATH, &good, 1);
    {
        unsigned char bogus_version[4] = {99, 0, 0, 0};
        poke_bytes(ARCHIVE_PATH, 4, bogus_version, 4);
    }
    check(load_passengers(ARCHIVE_PATH) == LOAD_REJECTED, "version=99 应被拒收");

    write_archive(ARCHIVE_PATH, &good, 1);
    {
        unsigned char bogus_magic[4] = {'X', 'X', 'X', 'X'};
        poke_bytes(ARCHIVE_PATH, 0, bogus_magic, 4);
    }
    check(load_passengers(ARCHIVE_PATH) == LOAD_REJECTED, "magic 错误应被拒收");

    write_archive(ARCHIVE_PATH, &good, 1);
    {
        FILE *file = fopen(ARCHIVE_PATH, "r+b");
        fseek(file, 8, SEEK_SET);      // count 写成 5，但文件里只有 1 条
        put_i32_le(file, 5);
        fclose(file);
    }
    check(load_passengers(ARCHIVE_PATH) == LOAD_REJECTED,
          "count 与实际长度不符（截断文件）应被拒收");

    // --- 价格不受信任，读入后按规则重算 ---
    write_archive(ARCHIVE_PATH, &good, 1);
    {
        FILE *file = fopen(ARCHIVE_PATH, "r+b");
        fseek(file, RECORD_BASE(0) + 54, SEEK_SET);
        put_i32_le(file, 99999);
        fclose(file);
    }
    reset();
    check(load_passengers(ARCHIVE_PATH) == LOAD_OK, "price 被篡改仍应接受该记录");
    check(head != NULL && head->data.price == calc_price(0, 2, 0),
          "读入后 price 应被重算覆盖，而不是照抄文件里的 99999");

    // --- 跨记录约束 ---
    Passenger pair[2];
    pair[0] = good;
    pair[1] = second;                       // 同一座位、区段不相交
    write_archive(ARCHIVE_PATH, pair, 2);
    reset();
    check(load_passengers(ARCHIVE_PATH) == LOAD_OK, "同座位但区段不相交应被接受");
    check(passenger_count() == 2, "应读入 2 条");

    pair[1] = make("2222", "2026-09-16", "G2", 1, 4, 0, 3, 1);  // 同座位且区段相交
    write_archive(ARCHIVE_PATH, pair, 2);
    check(load_passengers(ARCHIVE_PATH) == LOAD_REJECTED, "同座位且区段重叠应被拒收");

    pair[1] = make("1111", "2026-09-16", "G2", 3, 5, 0, 3, 2);  // 身份证重复
    write_archive(ARCHIVE_PATH, pair, 2);
    check(load_passengers(ARCHIVE_PATH) == LOAD_REJECTED, "身份证重复应被拒收");

    // --- 原子性：拒收绝不能破坏内存中已有的数据 ---
    reset();
    insert_passenger(make("9999", "2026-09-16", "G2", 0, 2, 0, 3, 1));
    write_archive(ARCHIVE_PATH, pair, 2);   // 这份是坏的（身份证重复）
    check(load_passengers(ARCHIVE_PATH) == LOAD_REJECTED, "坏文件应被拒收");
    check(passenger_count() == 1, "拒收后内存中的原有数据不应被清空");
    check(search_passenger("9999") != NULL, "拒收后原有记录仍应能查到");
    check(indexes_are_consistent(), "拒收后索引仍应一致");

    remove(ARCHIVE_PATH);
    reset();
}

int main(void) {
    test_overlap();
    test_train();
    test_trip();
    test_seat_reuse();
    test_class_and_capacity();
    test_indexes();
    test_stats();
    test_archive();
    reset();

    printf("\n================================\n");
    printf("%d 项检查，%d 项失败\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
