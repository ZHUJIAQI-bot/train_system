#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================
   第1步：数据 —— 定义"旅客"长什么样（对应 PPT 里的旅客车票信息）
   ============================================================ */
typedef struct Passenger {
    char id[5];      // 身份证后4位（字符串，留1位给'\0'）
    char name[20];   // 姓名
    char travel_date[11]; // 出行日期 YYYY-MM-DD
    char train_no[4];     // 车次，例如 G1
    char depart_time[6];  // 发车时间 HH:MM
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

// 二叉搜索树索引：按身份证后4位排序，data 指向链表中的原结点
typedef struct SearchTreeNode {
    Node *data;
    struct SearchTreeNode *left;
    struct SearchTreeNode *right;
} SearchTreeNode;

// B树索引：最小度数为2，每个结点最多保存3个身份证键
#define BTREE_MIN_DEGREE 2
#define BTREE_MAX_KEYS (2 * BTREE_MIN_DEGREE - 1)
#define BTREE_MAX_CHILDREN (2 * BTREE_MIN_DEGREE)
typedef struct BTreeNode {
    int key_count;
    int leaf;
    char keys[BTREE_MAX_KEYS][5];
    Node *values[BTREE_MAX_KEYS];
    struct BTreeNode *children[BTREE_MAX_CHILDREN];
} BTreeNode;

/* ------------------ 全局数据（内存里的"存储"） ------------------ */
Node *head = NULL;          // 链表头指针，空链表时是 NULL
SearchTreeNode *search_root = NULL;
BTreeNode *btree_root = NULL;

const char *stations[6] = {"上海", "苏州", "南京", "济南", "天津", "北京"};

#define MAX_SEAT 12                        // 每节车厢最多座位数（要增减座位改这里和 car_seats）

int car_type[6]  = {0, 1, 1, 2, 2, 2};    // 车厢1~5的类型：1=一等座 2=二等座
int car_seats[6] = {0, 8, 8, 12, 12, 12}; // 车厢1~5的座位数（稍设多一点）
int seat_taken[6][MAX_SEAT + 1];           // seat_taken[车厢][座位]=1 表示已售出

/* ============================================================
   第3步：操作 —— 一组函数，在这些字段上增删改查
   ============================================================ */

// 计算票价：每站150元，一等座再加100元（上下车站不分先后，取站数绝对值）
int calc_price(int board, int alight, int firstclass) {
    int seg = alight - board;
    if (seg < 0) seg = -seg;
    int p = seg * 150;
    if (firstclass) p += 100;
    return p;
}

// 校验身份证后4位：4位数字，或 3位数字 + 末尾 x/X
int valid_id(char *id) {
    if (strlen(id) != 4) return 0;                 // 必须是4位
    for (int i = 0; i < 3; i++)
        if (id[i] < '0' || id[i] > '9') return 0;  // 前3位必须是数字
    char last = id[3];
    return (last >= '0' && last <= '9') || last == 'x' || last == 'X';
}

// 校验真实日期，格式为 YYYY-MM-DD
int valid_date(const char *date) {
    int year, month, day;
    int days_in_month;

    if (strlen(date) != 10 || date[4] != '-' || date[7] != '-') return 0;
    for (int i = 0; i < 10; i++) {
        if (i == 4 || i == 7) continue;
        if (date[i] < '0' || date[i] > '9') return 0;
    }

    year = (date[0] - '0') * 1000 + (date[1] - '0') * 100 +
           (date[2] - '0') * 10 + (date[3] - '0');
    month = (date[5] - '0') * 10 + date[6] - '0';
    day = (date[8] - '0') * 10 + date[9] - '0';
    if (month < 1 || month > 12) return 0;

    days_in_month = 31;
    if (month == 4 || month == 6 || month == 9 || month == 11) {
        days_in_month = 30;
    } else if (month == 2) {
        int leap = (year % 400 == 0) || (year % 4 == 0 && year % 100 != 0);
        days_in_month = leap ? 29 : 28;
    }
    return day >= 1 && day <= days_in_month;
}

static void free_search_tree(SearchTreeNode *root) {
    if (root == NULL) return;
    free_search_tree(root->left);
    free_search_tree(root->right);
    free(root);
}

static SearchTreeNode *insert_search_tree(SearchTreeNode *root, Node *data) {
    if (root == NULL) {
        SearchTreeNode *tree_node = malloc(sizeof(SearchTreeNode));
        if (tree_node == NULL) return NULL;
        tree_node->data = data;
        tree_node->left = NULL;
        tree_node->right = NULL;
        return tree_node;
    }

    if (strcmp(data->data.id, root->data->data.id) < 0) {
        root->left = insert_search_tree(root->left, data);
    } else if (strcmp(data->data.id, root->data->data.id) > 0) {
        root->right = insert_search_tree(root->right, data);
    }
    return root;
}

static SearchTreeNode *find_search_tree(SearchTreeNode *root, const char *id) {
    while (root != NULL) {
        int comparison = strcmp(id, root->data->data.id);
        if (comparison == 0) return root;
        root = comparison < 0 ? root->left : root->right;
    }
    return NULL;
}

static void rebuild_search_index(void) {
    free_search_tree(search_root);
    search_root = NULL;
    for (Node *current = head; current != NULL; current = current->next) {
        search_root = insert_search_tree(search_root, current);
    }
}

static BTreeNode *create_btree_node(int leaf) {
    BTreeNode *node = calloc(1, sizeof(BTreeNode));
    if (node != NULL) node->leaf = leaf;
    return node;
}

static void free_btree(BTreeNode *root) {
    if (root == NULL) return;
    if (!root->leaf) {
        for (int i = 0; i <= root->key_count; i++) free_btree(root->children[i]);
    }
    free(root);
}

static Node *find_btree(BTreeNode *root, const char *id) {
    if (root == NULL) return NULL;
    int index = 0;
    while (index < root->key_count && strcmp(id, root->keys[index]) > 0) index++;
    if (index < root->key_count && strcmp(id, root->keys[index]) == 0) {
        return root->values[index];
    }
    return root->leaf ? NULL : find_btree(root->children[index], id);
}

static void split_btree_child(BTreeNode *parent, int child_index) {
    BTreeNode *full = parent->children[child_index];
    BTreeNode *right = create_btree_node(full->leaf);
    if (right == NULL) return;
    right->key_count = BTREE_MIN_DEGREE - 1;
    for (int j = 0; j < BTREE_MIN_DEGREE - 1; j++) {
        strcpy(right->keys[j], full->keys[j + BTREE_MIN_DEGREE]);
        right->values[j] = full->values[j + BTREE_MIN_DEGREE];
    }
    if (!full->leaf) {
        for (int j = 0; j < BTREE_MIN_DEGREE; j++) {
            right->children[j] = full->children[j + BTREE_MIN_DEGREE];
            full->children[j + BTREE_MIN_DEGREE] = NULL;
        }
    }
    full->key_count = BTREE_MIN_DEGREE - 1;
    for (int j = parent->key_count; j >= child_index + 1; j--) {
        parent->children[j + 1] = parent->children[j];
    }
    parent->children[child_index + 1] = right;
    for (int j = parent->key_count - 1; j >= child_index; j--) {
        strcpy(parent->keys[j + 1], parent->keys[j]);
        parent->values[j + 1] = parent->values[j];
    }
    strcpy(parent->keys[child_index], full->keys[BTREE_MIN_DEGREE - 1]);
    parent->values[child_index] = full->values[BTREE_MIN_DEGREE - 1];
    parent->key_count++;
}

static void insert_btree_nonfull(BTreeNode *node, Node *data) {
    int index = node->key_count - 1;
    if (node->leaf) {
        while (index >= 0 && strcmp(data->data.id, node->keys[index]) < 0) {
            strcpy(node->keys[index + 1], node->keys[index]);
            node->values[index + 1] = node->values[index];
            index--;
        }
        strcpy(node->keys[index + 1], data->data.id);
        node->values[index + 1] = data;
        node->key_count++;
        return;
    }
    while (index >= 0 && strcmp(data->data.id, node->keys[index]) < 0) index--;
    index++;
    if (node->children[index]->key_count == BTREE_MAX_KEYS) {
        split_btree_child(node, index);
        if (strcmp(data->data.id, node->keys[index]) > 0) index++;
    }
    insert_btree_nonfull(node->children[index], data);
}

static void insert_btree(Node *data) {
    if (btree_root == NULL) {
        btree_root = create_btree_node(1);
        if (btree_root == NULL) return;
        strcpy(btree_root->keys[0], data->data.id);
        btree_root->values[0] = data;
        btree_root->key_count = 1;
        return;
    }
    if (btree_root->key_count == BTREE_MAX_KEYS) {
        BTreeNode *new_root = create_btree_node(0);
        if (new_root == NULL) return;
        new_root->children[0] = btree_root;
        split_btree_child(new_root, 0);
        btree_root = new_root;
    }
    insert_btree_nonfull(btree_root, data);
}

static void rebuild_btree_index(void) {
    free_btree(btree_root);
    btree_root = NULL;
    for (Node *current = head; current != NULL; current = current->next) {
        insert_btree(current);
    }
}

// 尾插：新旅客加到链表末尾（保持上车先后顺序）
int insert_passenger(Passenger p) {
    Node *n = malloc(sizeof(Node));
    if (n == NULL) {
        printf("内存分配失败！\n");
        return 0;
    }
    n->data = p;
    n->next = NULL;

    if (head == NULL) {          // 空链表：直接当第一个
        head = n;
        rebuild_search_index();
        rebuild_btree_index();
        return 1;
    }
    Node *t = head;
    while (t->next != NULL)      // 走到最后一个结点
        t = t->next;
    t->next = n;                 // 把新结点挂上去
    rebuild_search_index();
    rebuild_btree_index();
    return 1;
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

// 释放链表中的全部旅客
void free_all_passengers(void) {
    free_search_tree(search_root);
    search_root = NULL;
    free_btree(btree_root);
    btree_root = NULL;
    Node *current = head;

    while (current != NULL) {
        Node *next = current->next;
        free(current);
        current = next;
    }
    head = NULL;
}

// 删除出行日期早于 today 的旅客，并释放对应座位和链表结点
int remove_expired_passengers(const char *today) {
    Node *current = head;
    Node *previous = NULL;
    int removed = 0;

    while (current != NULL) {
        Node *next = current->next;
        if (valid_date(current->data.travel_date) &&
            strcmp(current->data.travel_date, today) < 0) {
            if (previous == NULL) {
                head = next;
            } else {
                previous->next = next;
            }
            seat_taken[current->data.carriage][current->data.seat] = 0;
            free(current);
            removed++;
        } else {
            previous = current;
        }
        current = next;
    }

    if (removed > 0) {
        rebuild_search_index();
        rebuild_btree_index();
    }
    return removed;
}

// 保存当前旅客和座位信息到二进制文件
int save_passengers(const char *filename) {
    FILE *file = fopen(filename, "wb");
    if (file == NULL) return 0;

    int version = 3;
    int count = 0;
    for (Node *current = head; current != NULL; current = current->next) {
        count++;
    }
    if (fwrite(&version, sizeof(version), 1, file) != 1 ||
        fwrite(&count, sizeof(count), 1, file) != 1) {
        fclose(file);
        return 0;
    }
    for (Node *current = head; current != NULL; current = current->next) {
        if (fwrite(&current->data, sizeof(Passenger), 1, file) != 1) {
            fclose(file);
            return 0;
        }
    }
    fclose(file);
    return 1;
}

// 从二进制文件恢复旅客和座位信息
int load_passengers(const char *filename) {
    FILE *file = fopen(filename, "rb");
    if (file == NULL) return 0;

    int version = 0;
    int count = 0;
    if (fread(&version, sizeof(version), 1, file) != 1 || version != 3 ||
        fread(&count, sizeof(count), 1, file) != 1 || count < 0 || count > 10000) {
        fclose(file);
        return 0;
    }

    free_all_passengers();
    memset(seat_taken, 0, sizeof(seat_taken));
    for (int i = 0; i < count; i++) {
        Passenger passenger;
        if (fread(&passenger, sizeof(Passenger), 1, file) != 1 ||
            passenger.carriage < 1 || passenger.carriage > 5 ||
            passenger.seat < 1 || passenger.seat > car_seats[passenger.carriage]) {
            free_all_passengers();
            memset(seat_taken, 0, sizeof(seat_taken));
            fclose(file);
            return 0;
        }
        if (!insert_passenger(passenger)) {
            free_all_passengers();
            memset(seat_taken, 0, sizeof(seat_taken));
            fclose(file);
            return 0;
        }
        seat_taken[passenger.carriage][passenger.seat] = 1;
    }
    fclose(file);
    return 1;
}

// 按身份证查找，返回结点指针，找不到返回 NULL
Node *search_passenger(char *id) {
    return find_btree(btree_root, id);
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
            rebuild_search_index();      // 删除后更新二叉搜索树索引
            rebuild_btree_index();       // 删除后更新B树索引
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

// 按车厢统计：总座位 / 已售 / 余票
void stat_by_carriage() {
    printf("%-4s %-6s %-6s %-6s %-4s\n", "车厢", "类型", "总座位", "已售", "余票");
    for (int c = 1; c <= 5; c++) {
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
    for (int i = 1; i <= 5; i++) {
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
    printf("  5. 按车厢统计\n");
    printf("  6. 分类统计\n");
    printf("  0. 退出\n");
    printf("======================================\n");
}

int main() {
    system("chcp 65001 >nul");  // 把控制台切到 UTF-8，和源码编码一致，避免中文乱码
    int choice;
    do {
        menu();
        
        if (!read_int("请选择：", &choice)) {
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
