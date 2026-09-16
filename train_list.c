/* ============================================================
   控制台版入口：只保留菜单与交互，数据模型见 train_model.h / train_model.c
   ============================================================ */
#include "train_model.h"

#ifdef _WIN32
#include <windows.h>
#endif

// 每次改动后落盘；失败只提示，不中断流程
static void save_data(void) {
    if (!save_passengers(default_data_file())) {
        printf("警告：数据保存失败，本次改动仅存在于内存中。\n");
    }
}

// 读取整数，并处理用户输入非数字的情况
int read_int(const char *prompt, int *value) {
    int result;
    int character;

    printf("%s", prompt);
    result = scanf("%d", value);
    while ((character = getchar()) != '\n' && character != EOF) {
    }

    if (result != 1) {
        printf("输入无效，请输入数字。\n");
        return 0;
    }
    return 1;
}

/* 读取一个「词」（跳过前导空白，读到下一个空白为止）。
   比 scanf("%Ns") 多做一件事：超长输入会把该行剩余部分**丢弃**。

   原来的写法有个隐蔽的坑：用户要是在姓名里多打了字，scanf 只吃掉限宽内的
   那些字符，多出来的会留在输入缓冲里，被下一次读取当成出行日期 ——
   于是用户看到「日期不合法」，却不知道自己哪里填错了。 */
int read_word(const char *prompt, char *out, size_t size) {
    int character;
    size_t n = 0;

    printf("%s", prompt);
    if (size == 0) return 0;

    do {
        character = getchar();
    } while (character == ' ' || character == '\t');

    if (character == EOF) {
        out[0] = '\0';
        return 0;
    }
    while (character != EOF && character != '\n' &&
           character != ' ' && character != '\t') {
        if (n + 1 < size) out[n++] = (char)character;
        character = getchar();
    }
    out[n] = '\0';

    // 丢弃本行剩余内容，避免污染下一次读取
    while (character != '\n' && character != EOF) character = getchar();
    return 1;
}

// 显示全部旅客
void print_all() {
    if (head == NULL) {
        printf("当前车上没有旅客。\n");
        return;
    }
    printf("%-6s %-10s %-12s %-4s %-5s %-5s %-5s %-4s %-4s %-4s\n",
           "身份证", "姓名", "日期", "车次", "上站", "下站", "票价", "车厢", "座位", "等级");
    Node *t = head;
    while (t != NULL) {
        Passenger *p = &t->data;
        printf("%-6s %-10s %-12s %-4s %-5s %-5s %-5d %-4d %-4d %-4s\n",
               p->id, p->name, p->travel_date, p->train_no,
               stations[p->board], stations[p->alight],
               p->price, p->carriage, p->seat,
               p->firstclass ? "一等" : "二等");
        t = t->next;
    }
}

// 按车厢统计（需指定车次+日期）：区段复用下「已售 N/12」没有意义，
// 因此统计「全程空座」与「有被占用过的座位」两类，二者之和恒等于总座位数。
void stat_by_carriage() {
    char train_no[8], date[20];
    if (!read_word("车次(G1-G10)：", train_no, sizeof(train_no))) return;
    if (!read_word("出行日期(YYYY-MM-DD)：", date, sizeof(date))) return;
    if (!valid_train_no(train_no) || !valid_date(date)) {
        printf("车次或日期不合法。\n");
        return;
    }
    printf("%-4s %-6s %-6s %-8s %-8s\n", "车厢", "类型", "总座位", "全程空座", "已占用");
    for (int c = 1; c <= CARRIAGE_COUNT; c++) {
        printf("%-4d %-6s %-6d %-8d %-8d\n",
               c, car_type[c] == 1 ? "一等座" : "二等座",
               car_seats[c],
               carriage_fully_free_seats(train_no, date, c),
               carriage_occupied_seats(train_no, date, c));
    }
}

// 售票：按车次+日期+区段分配座位，并把旅客加入链表
void sell_ticket() {
    char id[20], name[20], train_no[8], date[20];
    int board, alight, firstclass;
    char err[160];

    if (!read_word("身份证后4位：", id, sizeof(id))) return;
    if (!valid_id(id)) {
        printf("身份证后4位不合法：应为4位数字，或3位数字+末尾x/X。\n");
        return;
    }
    if (search_passenger(id) != NULL) {
        printf("该旅客已经购票，不能重复购票。\n");
        return;
    }
    if (!read_word("姓名：", name, sizeof(name))) return;
    if (!read_word("出行日期(YYYY-MM-DD)：", date, sizeof(date))) return;
    if (!read_word("车次(G1-G10)：", train_no, sizeof(train_no))) return;
    if (!read_int("上车站(0上海 1苏州 2南京 3济南 4天津 5北京)：", &board)) return;
    if (!read_int("下车站(0上海 1苏州 2南京 3济南 4天津 5北京)：", &alight)) return;
    if (!read_int("等级(0二等座 1一等座)：", &firstclass)) return;

    // 车次/日期/车站/方向/等级 全部交给模型层统一校验
    if (!validate_trip(train_no, date, board, alight, firstclass, err, sizeof(err))) {
        printf("%s\n", err);
        return;
    }

    /* 出行日期不得早于今天。GUI 版的日期只能从下拉里选「今天起 4 天」，
       天然没有这个问题；但控制台是手输，填了过去的日期也照样能过，
       然后下次启动就被过期清理静默删掉 —— 用户看起来像「刚买的票丢了」。 */
    char today[11];
    today_string(today);
    if (strcmp(date, today) < 0) {
        printf("出行日期不能早于今天（%s）。\n", today);
        return;
    }

    int c = -1, s = -1;
    if (!assign_seat(train_no, date, firstclass, board, alight, &c, &s)) {
        printf("该区间该等级座位已售罄，建议改乘其他车次。\n");
        return;
    }

    Passenger p = {0};             // 先全零，保证没有未初始化的字段
    strncpy(p.id, id, sizeof(p.id) - 1);
    strncpy(p.name, name, sizeof(p.name) - 1);
    strncpy(p.travel_date, date, sizeof(p.travel_date) - 1);
    strncpy(p.train_no, train_no, sizeof(p.train_no) - 1);
    train_depart_time(train_number_of(train_no), p.depart_time);
    p.board      = board;
    p.alight     = alight;
    p.firstclass = firstclass;
    p.price      = calc_price(board, alight, firstclass);
    p.carriage   = c;
    p.seat       = s;

    InsertResult result = insert_passenger(p);
    if (result != INSERT_OK) {
        printf("%s\n", result == INSERT_DUPLICATE ? "该身份证已经购票。"
                                                  : "购票失败，内存不足。");
        return;
    }
    save_data();

    printf("购票成功！%s %s %s %s发车 车厢%d %d号座位，票价%d元。\n",
           p.name, p.travel_date, p.train_no, p.depart_time, c, s, p.price);
}

// 旅客下车
void passenger_alight() {
    char id[20];
    if (!read_word("要退票的旅客身份证后4位：", id, sizeof(id))) return;
    if (delete_passenger(id)) {
        save_data();
        printf("旅客 %s 已退票。\n", id);
    } else {
        printf("未找到该旅客，退票失败。\n");
    }
}

// 按身份证查询
void query_by_id() {
    char id[20];
    if (!read_word("要查询的身份证后4位：", id, sizeof(id))) return;
    Node *n = search_passenger(id);
    if (n == NULL) {
        printf("未找到该旅客。\n");
        return;
    }
    Passenger *p = &n->data;
    printf("姓名：%s  上站：%s  下站：%s  票价：%d  车厢%d  座位%d  %s\n",
           p->name, stations[p->board], stations[p->alight],
           p->price, p->carriage, p->seat,
           p->firstclass ? "一等座" : "二等座");
}

// 分类统计：指定车次+日期的各站上下车人数、各区段载客数与票款
void statistics() {
    char train_no[8], date[20];
    if (!read_word("车次(G1-G10)：", train_no, sizeof(train_no))) return;
    if (!read_word("出行日期(YYYY-MM-DD)：", date, sizeof(date))) return;
    if (!valid_train_no(train_no) || !valid_date(date)) {
        printf("车次或日期不合法。\n");
        return;
    }

    int board_cnt[STATION_COUNT] = {0}, alight_cnt[STATION_COUNT] = {0};
    int count = 0;
    for (Node *t = head; t != NULL; t = t->next) {
        if (strcmp(t->data.train_no, train_no) != 0) continue;
        if (strcmp(t->data.travel_date, date) != 0) continue;
        board_cnt[t->data.board]++;
        alight_cnt[t->data.alight]++;
        count++;
    }
    printf("%-6s %-6s %-6s\n", "车站", "上车", "下车");
    for (int i = 0; i < STATION_COUNT; i++)
        printf("%-6s %-6d %-6d\n", stations[i], board_cnt[i], alight_cnt[i]);
    printf("本车次本日售票 %d 张\n", count);

    printf("%-16s %-6s\n", "区段", "载客");
    char label[32];
    for (int i = 0; i < SEGMENT_COUNT; i++) {
        snprintf(label, sizeof(label), "%s-%s", stations[i], stations[i + 1]);
        printf("%-16s %-6d\n", label, segment_load(train_no, date, i));
    }
    printf("当前在车旅客票款合计：%d 元\n", total_fare());
}

// 菜单
void menu() {
    printf("\n======== 高铁列车旅客管理系统 ========\n");
    printf("  1. 售票（旅客购票上车）\n");
    printf("  2. 旅客退票\n");
    printf("  3. 按身份证查询旅客\n");
    printf("  4. 显示全部旅客\n");
    printf("  5. 按车厢统计（需车次+日期）\n");
    printf("  6. 分类统计（需车次+日期）\n");
    printf("  0. 退出\n");
    printf("======================================\n");
}

int main() {
#ifdef _WIN32
    /* 用 API 切换控制台代码页，而不是 system("chcp 65001")：
       后者会启动一个 cmd.exe 子进程，当 stdin 被重定向时它会吞掉输入，
       导致管道/脚本喂入的指令全部丢失。 */
    SetConsoleOutputCP(65001);
#else
    system("chcp 65001 >nul");
#endif

    const char *data_file = default_data_file();
    LoadResult loaded = load_passengers(data_file);
    if (loaded == LOAD_OK) {
        printf("已读取存档：%d 名旅客。\n", passenger_count());
    } else if (loaded == LOAD_REJECTED) {
        printf("警告：存档未通过校验，已备份为 %s.bad-<时间戳>，本次以空列表启动。\n",
               data_file);
    } else if (loaded == LOAD_NO_MEMORY) {
        printf("警告：内存不足，无法读取存档。\n");
    }

    char today[11];
    char reason[128];
    today_string(today);
    int expired = purge_expired_passengers(today, default_data_file(), reason, sizeof(reason));
    if (expired > 0) {
        printf("已自动清理 %d 名过期旅客。\n", expired);
        save_data();
    } else if (reason[0] != '\0') {
        printf("提示：%s\n", reason);
    }

    int choice;
    do {
        menu();

        if (!read_int("请选择：", &choice)) {
            if (feof(stdin)) {          // 输入流结束：退出，否则会无限循环刷菜单
                printf("\n输入已结束，退出。\n");
                break;
            }
            continue;
        }
        switch (choice) {
            case 1: sell_ticket();      break;
            case 2: passenger_alight(); break;
            case 3: query_by_id();      break;
            case 4: print_all();        break;
            case 5: stat_by_carriage(); break;
            case 6: statistics();       break;
            case 0:                     break;
            default: printf("无效选项，请重试。\n");
        }
    } while (choice != 0);

    save_data();                        // 正常退出与输入结束两条路径都落盘
    free_all_passengers();
    printf("再见！\n");
    return 0;
}
