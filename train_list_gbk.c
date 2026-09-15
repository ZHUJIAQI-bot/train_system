#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================
   第1步：数据 —— 定义"旅客"长什么样（对应 PPT 里的旅客车票信息）
   ============================================================ */
typedef struct Passenger {
    char id[5];      // 身份证后4位（字符串，留1位给'\0'）
    char name[20];   // 姓名
    int  board;      // 上车车站编号（0~5）
    int  alight;     // 下车车站编号（0~5）
    int  price;      // 票价
    int  carriage;   // 车厢号（1~5）
    int  seat;       // 座位号
    int  firstclass; // 0=二等座  1=一等座
} Passenger;

/* ============================================================
   第2步：结构 —— 单链表结点 = "一个旅客 + 指向下一个旅客的箭头"
   ============================================================ */
typedef struct Node {
    Passenger    data;  // 值：这个结点装的是哪个旅客
    struct Node *next;  // 关系：指向下一个旅客
} Node;

/* ------------------ 全局数据（内存里的"存储"） ------------------ */
Node *head = NULL;          // 链表头指针，空链表时是 NULL

char *stations[6] = {"上海", "苏州", "南京", "济南", "天津", "北京"};

int car_type[6]  = {0, 1, 1, 2, 2, 2};  // 车厢1~5的类型：1=一等座 2=二等座
int car_seats[6] = {0, 5, 5, 9, 9, 9};  // 车厢1~5的座位数
int seat_taken[6][10];                  // seat_taken[车厢][座位]=1 表示已售出

/* ============================================================
   第3步：操作 —— 一组函数，在这些字段上增删改查
   ============================================================ */

// 计算票价：每站150元，一等座再加100元
int calc_price(int board, int alight, int firstclass) {
    int p = (alight - board) * 150;
    if (firstclass) p += 100;
    return p;
}

// 尾插：新旅客加到链表末尾（保持上车先后顺序）
void insert_passenger(Passenger p) {
    Node *n = (Node *)malloc(sizeof(Node));
    n->data = p;
    n->next = NULL;

    if (head == NULL) {          // 空链表：直接当第一个
        head = n;
        return;
    }
    Node *t = head;
    while (t->next != NULL)      // 走到最后一个结点
        t = t->next;
    t->next = n;                 // 把新结点挂上去
}

// 按身份证查找，返回结点指针，找不到返回 NULL
Node *search_passenger(char *id) {
    Node *t = head;
    while (t != NULL) {
        if (strcmp(t->data.id, id) == 0)
            return t;
        t = t->next;
    }
    return NULL;
}

// 删除旅客（下车）：改 next 指针，并释放座位、回收内存
int delete_passenger(char *id) {
    Node *t = head, *prev = NULL;
    while (t != NULL) {
        if (strcmp(t->data.id, id) == 0) {
            if (prev == NULL)           // 删除的是头结点
                head = t->next;
            else                         // 让前一个结点跳过它
                prev->next = t->next;

            seat_taken[t->data.carriage][t->data.seat] = 0; // 释放座位
            free(t);                     // 回收内存
            return 1;                    // 成功
        }
        prev = t;
        t = t->next;
    }
    return 0;                            // 没找到
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

// 显示各车厢余票
void print_available() {
    printf("%-4s %-6s %-4s\n", "车厢", "类型", "余票");
    for (int c = 1; c <= 5; c++) {
        int left = 0;
        for (int s = 1; s <= car_seats[c]; s++)
            if (seat_taken[c][s] == 0) left++;
        printf("%-4d %-6s %-4d\n", c,
               car_type[c] == 1 ? "一等座" : "二等座", left);
    }
}

// 售票：分配一个空座位 + 添加旅客
void sell_ticket() {
    char id[5], name[20];
    int board, alight, firstclass;

    printf("身份证后4位：");   scanf("%s", id);
    printf("姓名：");           scanf("%s", name);
    printf("上车站(0上海 1苏州 2南京 3济南 4天津)："); scanf("%d", &board);
    printf("下车站(1苏州 2南京 3济南 4天津 5北京)："); scanf("%d", &alight);
    printf("等级(0二等座 1一等座)：");                 scanf("%d", &firstclass);

    if (alight <= board || board < 0 || alight > 5) {
        printf("车站不合法：下车站必须在上车站之后。\n");
        return;
    }

    // 找一个符合等级的空座位
    int c = -1, s = -1;
    for (int i = 1; i <= 5 && c == -1; i++) {
        if (car_type[i] != (firstclass ? 1 : 2)) continue; // 等级不符，跳过
        for (int j = 1; j <= car_seats[i]; j++) {
            if (seat_taken[i][j] == 0) { c = i; s = j; break; }
        }
    }
    if (c == -1) {
        printf("该等级座位已售罄，建议改乘其他车次。\n");
        return;
    }

    Passenger p;
    strcpy(p.id, id);
    strcpy(p.name, name);
    p.board      = board;
    p.alight     = alight;
    p.firstclass = firstclass;
    p.price      = calc_price(board, alight, firstclass);
    p.carriage   = c;
    p.seat       = s;

    seat_taken[c][s] = 1;      // 标记座位已售
    insert_passenger(p);       // 加入链表

    printf("购票成功！%s %s 车厢%d %d号座位，票价%d元。\n",
           p.name, stations[p.board], c, s, p.price);
}

// 旅客下车
void passenger_alight() {
    char id[5];
    printf("要下车的旅客身份证后4位：");
    scanf("%s", id);
    if (delete_passenger(id))
        printf("旅客 %s 已下车。\n", id);
    else
        printf("未找到该旅客，下车失败。\n");
}

// 按身份证查询
void query_by_id() {
    char id[5];
    printf("要查询的身份证后4位：");
    scanf("%s", id);
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
    int board_cnt[6] = {0}, alight_cnt[6] = {0};
    int total = 0;
    Node *t = head;
    while (t != NULL) {
        board_cnt[t->data.board]++;
        alight_cnt[t->data.alight]++;
        total += t->data.price;
        t = t->next;
    }
    printf("%-6s %-6s %-6s\n", "车站", "上车", "下车");
    for (int i = 0; i < 6; i++)
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
    printf("  5. 显示各车厢余票\n");
    printf("  6. 分类统计\n");
    printf("  0. 退出\n");
    printf("======================================\n");
    printf("请选择：");
}

int main() {
    int choice;
    do {
        menu();
        scanf("%d", &choice);
        switch (choice) {
            case 1: sell_ticket();      break;
            case 2: passenger_alight(); break;
            case 3: query_by_id();      break;
            case 4: print_all();        break;
            case 5: print_available();  break;
            case 6: statistics();       break;
            case 0: printf("再见！\n"); break;
            default: printf("无效选项，请重试。\n");
        }
    } while (choice != 0);
    return 0;
}
