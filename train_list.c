/* ============================================================
   控制台版入口：只保留菜单与交互，数据模型见 train_model.h / train_model.c
   ============================================================ */
#include "train_model.h"

#ifdef _WIN32
#include <windows.h>
#endif

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

// 显示全部旅客
void print_all() {
    if (head == NULL) {
        printf("当前车上没有旅客。\n");
        return;
    }
    printf("%-6s %-10s %-5s %-5s %-5s %-4s %-4s %-4s\n",
           "身份证", "姓名", "上站", "下站", "票价", "车厢", "座位", "等级");
    Node *t = head;
    while (t != NULL) {
        Passenger *p = &t->data;
        printf("%-6s %-10s %-5s %-5s %-5d %-4d %-4d %-4s\n",
               p->id, p->name,
               stations[p->board], stations[p->alight],
               p->price, p->carriage, p->seat,
               p->firstclass ? "一等" : "二等");
        t = t->next;
    }
}

// 按车厢统计：总座位 / 已售 / 余票
void stat_by_carriage() {
    printf("%-4s %-6s %-6s %-6s %-4s\n", "车厢", "类型", "总座位", "已售", "余票");
    for (int c = 1; c <= CARRIAGE_COUNT; c++) {
        int taken = 0;
        for (int s = 1; s <= car_seats[c]; s++)
            if (seat_taken[c][s]) taken++;
        printf("%-4d %-6s %-6d %-6d %-4d\n",
               c, car_type[c] == 1 ? "一等座" : "二等座",
               car_seats[c], taken, car_seats[c] - taken);
    }
}

// 售票：分配一个空座位 + 添加旅客
void sell_ticket() {
    char id[20], name[20];
    int board, alight, firstclass;

    printf("身份证后4位：");   scanf("%19s", id);
    if (!valid_id(id)) {
        printf("身份证后4位不合法：应为4位数字，或3位数字+末尾x/X。\n");
        return;
    }

    if (search_passenger(id) != NULL) {
        printf("该旅客已经购票，不能重复购票。\n");
        return;
    }
    printf("姓名：");           scanf("%19s", name);  // 限宽，避免超过 name[20] 越界
    if (!read_int("上车站(0上海 1苏州 2南京 3济南 4天津 5北京)：", &board)) return;
    if (!read_int("下车站(0上海 1苏州 2南京 3济南 4天津 5北京)：", &alight)) return;
    if (!read_int("等级(0二等座 1一等座)：", &firstclass)) return;

    int input_valid = 1;
    if (board < 0 || board > 5 || alight < 0 || alight > 5) {
        printf("车站编号不合法（应在 0~5 之间）。\n");
        input_valid = 0;
    } else if (alight == board) {
        printf("车站不合法：上下车站不能相同。\n");
        input_valid = 0;
    }
    if (firstclass != 0 && firstclass != 1) {
        printf("等级不合法：只能输入 0（二等座）或 1（一等座）。\n");
        input_valid = 0;
    }
    if (!input_valid) {
        return;
    }

    // 平均分配：在符合等级的车厢里，选当前余票最多的那节，再找它的第一个空座
    int c = -1, s = -1, best_left = -1;
    for (int i = 1; i <= CARRIAGE_COUNT; i++) {
        if (car_type[i] != (firstclass ? 1 : 2)) continue; // 等级不符，跳过
        int left = 0;
        for (int j = 1; j <= car_seats[i]; j++)
            if (seat_taken[i][j] == 0) left++;
        if (left > best_left) { best_left = left; c = i; }
    }
    if (c == -1 || best_left == 0) {
        printf("该等级座位已售罄，建议改乘其他车次。\n");
        return;
    }
    for (int j = 1; j <= car_seats[c]; j++)
        if (seat_taken[c][j] == 0) { s = j; break; }

    Passenger p;
    strcpy(p.id, id);
    strcpy(p.name, name);
    p.board      = board;
    p.alight     = alight;
    p.firstclass = firstclass;
    p.price      = calc_price(board, alight, firstclass);
    p.carriage   = c;
    p.seat       = s;

    if (!insert_passenger(p)) {
        printf("购票失败，无法保存旅客信息。\n");
        return;
    }
    seat_taken[c][s] = 1;      // 标记座位已售

    printf("购票成功！%s %s 车厢%d %d号座位，票价%d元。\n",
           p.name, stations[p.board], c, s, p.price);
}

// 旅客下车
void passenger_alight() {
    char id[20];
    printf("要下车的旅客身份证后4位：");
    scanf("%19s", id);
    if (delete_passenger(id))
        printf("旅客 %s 已下车。\n", id);
    else
        printf("未找到该旅客，下车失败。\n");
}

// 按身份证查询
void query_by_id() {
    char id[20];
    printf("要查询的身份证后4位：");
    scanf("%19s", id);
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

// 分类统计：各站上下车人数 + 总票款
void statistics() {
    int board_cnt[STATION_COUNT] = {0}, alight_cnt[STATION_COUNT] = {0};
    int total = 0;
    Node *t = head;
    while (t != NULL) {
        board_cnt[t->data.board]++;
        alight_cnt[t->data.alight]++;
        total += t->data.price;
        t = t->next;
    }
    printf("%-6s %-6s %-6s\n", "车站", "上车", "下车");
    for (int i = 0; i < STATION_COUNT; i++)
        printf("%-6s %-6d %-6d\n", stations[i], board_cnt[i], alight_cnt[i]);
    printf("本趟列车总票款：%d 元\n", total);
}

// 菜单
void menu() {
    printf("\n======== 高铁列车旅客管理系统 ========\n");
    printf("  1. 售票（旅客购票上车）\n");
    printf("  2. 旅客下车\n");
    printf("  3. 按身份证查询旅客\n");
    printf("  4. 显示全部旅客\n");
    printf("  5. 按车厢统计\n");
    printf("  6. 分类统计\n");
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
            case 0:
                free_all_passengers();
                printf("再见！\n");
                break;
            default: printf("无效选项，请重试。\n");
        }
    } while (choice != 0);
    return 0;
}
