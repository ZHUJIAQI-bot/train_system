#include "train_model.h"
#include <time.h>

/* ============================================================
   全局数据 —— 内存里的"存储"
   ============================================================ */
Node *head = NULL;
SearchTreeNode *search_root = NULL;
BTreeNode *btree_root = NULL;

const char *stations[STATION_COUNT] = {"上海", "苏州", "南京", "济南", "天津", "北京"};

int car_type[CARRIAGE_COUNT + 1]  = {0, 1, 1, 2, 2, 2};      // 1=一等座 2=二等座
int car_seats[CARRIAGE_COUNT + 1] = {0, 8, 8, 12, 12, 12};   // 各车厢座位数

/* ============================================================
   基础计算与校验
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

/* ============================================================
   车次与日期
   ============================================================ */

// 有界取长度：避免对无 NUL 终止的定长字段直接 strlen 造成越界读
static size_t bounded_len(const char *s, size_t max) {
    size_t n = 0;
    while (n < max && s[n] != '\0') n++;
    return n;
}

// 解析车次编号：G1~G10 → 1~10，任何非法形式返回 0
int train_number_of(const char *train_no) {
    if (train_no[0] != 'G') return 0;
    size_t len = bounded_len(train_no, 4);          // train_no 字段宽度为 4
    if (len != 2 && len != 3) return 0;             // "G1" 或 "G10"
    if (len == 3 && train_no[1] == '0') return 0;   // 拒绝 G01 这类前导 0
    int number = 0;
    for (size_t i = 1; i < len; i++) {
        if (train_no[i] < '0' || train_no[i] > '9') return 0;
        number = number * 10 + (train_no[i] - '0');
    }
    return (number >= 1 && number <= TRAIN_COUNT) ? number : 0;
}

int valid_train_no(const char *train_no) {
    return train_number_of(train_no) > 0;
}

// 奇数车次为 北京(5) → 上海(0) 方向
int train_is_northbound(int train_number) {
    return (train_number % 2) == 1;
}

// 发车时间：奇数车次整点 :00，偶数车次半点 :30，都从 06:00 起逐班递增
void train_depart_time(int train_number, char out[6]) {
    if (train_number < 1 || train_number > TRAIN_COUNT) {
        out[0] = '\0';
        return;
    }
    if (train_is_northbound(train_number)) {
        snprintf(out, 6, "%02d:00", 6 + (train_number - 1) / 2);
    } else {
        snprintf(out, 6, "%02d:30", 6 + (train_number - 2) / 2);
    }
}

void date_offset_string(int offset, char out[11]) {
    time_t now = time(NULL);
    struct tm date = *localtime(&now);
    date.tm_mday += offset;
    mktime(&date);                       // 归一化越界的月/日字段
    strftime(out, 11, "%Y-%m-%d", &date);
}

void today_string(char out[11]) {
    date_offset_string(0, out);
}

/* ============================================================
   行程校验（GUI 与控制台共用的唯一入口）
   ============================================================ */
int validate_trip(const char *train_no, const char *date, int board, int alight,
                  int firstclass, char *err, size_t errsz) {
    int number = train_number_of(train_no);
    if (number == 0) {
        snprintf(err, errsz, "车次不合法：应为 G1~G%d。", TRAIN_COUNT);
        return 0;
    }
    if (!valid_date(date)) {
        snprintf(err, errsz, "日期不合法：应为 YYYY-MM-DD 的真实日期。");
        return 0;
    }
    if (board < 0 || board >= STATION_COUNT || alight < 0 || alight >= STATION_COUNT) {
        snprintf(err, errsz, "车站编号应在 0~%d 之间。", STATION_COUNT - 1);
        return 0;
    }
    if (board == alight) {
        snprintf(err, errsz, "上车站与下车站不能相同。");
        return 0;
    }
    if (firstclass != 0 && firstclass != 1) {
        snprintf(err, errsz, "座位等级只能是 0（二等座）或 1（一等座）。");
        return 0;
    }
    // 方向必须与车次奇偶一致：奇数 北京→上海（站号递减），偶数 上海→北京（站号递增）
    int wants_northbound = board > alight;
    if (wants_northbound != train_is_northbound(number)) {
        snprintf(err, errsz, "车次 %s 是%s方向，与本次行程不符。",
                 train_no, train_is_northbound(number) ? "北京→上海" : "上海→北京");
        return 0;
    }
    return 1;
}

/* ============================================================
   座位：按「车次 + 日期 + 区段」从链表派生
   ============================================================ */

// 把 (board, alight) 归一化成半开区间 [lo, hi)。
// 必须归一化：奇数车次 board > alight，直接用 board/alight 比较
// 得到的是反向区间的补集，会把完全重叠的两个行程误判为不重叠。
static void normalize_segment(int board, int alight, int *lo, int *hi) {
    if (board < alight) { *lo = board;  *hi = alight; }
    else                { *lo = alight; *hi = board;  }
}

// 半开区间 [lo,hi)：旅客在 alight 站下车，不占用该站之后的区段。
// 因此 上海→南京 与 南京→北京 不重叠，但 上海→南京 与 苏州→北京 重叠。
int seat_segments_overlap(int board_a, int alight_a, int board_b, int alight_b) {
    int lo_a, hi_a, lo_b, hi_b;
    normalize_segment(board_a, alight_a, &lo_a, &hi_a);
    normalize_segment(board_b, alight_b, &lo_b, &hi_b);
    return lo_a < hi_b && lo_b < hi_a;
}

// 某车厢某座位在指定车次+日期下，对本次行程是否可售
int seat_available(const char *train_no, const char *date,
                   int carriage, int seat, int board, int alight) {
    if (carriage < 1 || carriage > CARRIAGE_COUNT) return 0;
    if (seat < 1 || seat > car_seats[carriage]) return 0;
    for (Node *t = head; t != NULL; t = t->next) {
        if (t->data.carriage != carriage || t->data.seat != seat) continue;
        if (strcmp(t->data.train_no, train_no) != 0) continue;
        if (strcmp(t->data.travel_date, date) != 0) continue;
        if (seat_segments_overlap(board, alight, t->data.board, t->data.alight)) return 0;
    }
    return 1;
}

// 指定车次+日期下，符合等级且对本次行程可售的座位总数
int seats_available(const char *train_no, const char *date, int firstclass,
                    int board, int alight) {
    int total = 0;
    for (int c = 1; c <= CARRIAGE_COUNT; c++) {
        if (car_type[c] != (firstclass ? 1 : 2)) continue;
        for (int s = 1; s <= car_seats[c]; s++)
            if (seat_available(train_no, date, c, s, board, alight)) total++;
    }
    return total;
}

// 分配座位：在符合等级的车厢里选余票最多的那节，再取该车厢最小可用座位号。
// 注意 best_count 必须初值小于任何合法值并显式判 0，否则第一节车厢会以 0 票被选中，
// best_seat 保持 -1 被当成座位号写入。
int assign_seat(const char *train_no, const char *date, int firstclass,
                int board, int alight, int *out_carriage, int *out_seat) {
    int best_carriage = -1;
    int best_count = -1;
    int best_seat = -1;

    for (int c = 1; c <= CARRIAGE_COUNT; c++) {
        if (car_type[c] != (firstclass ? 1 : 2)) continue;   // 等级硬约束
        int count = 0, first_seat = -1;
        for (int s = 1; s <= car_seats[c]; s++) {            // 上界用本车厢实际座位数
            if (seat_available(train_no, date, c, s, board, alight)) {
                count++;
                if (first_seat < 0) first_seat = s;
            }
        }
        if (count > best_count) {                            // 严格 >，平局取小车厢号
            best_count = count;
            best_carriage = c;
            best_seat = first_seat;
        }
    }
    if (best_carriage < 0 || best_count == 0) return 0;      // 该区间该等级已售罄
    *out_carriage = best_carriage;
    *out_seat = best_seat;
    return 1;
}

/* ============================================================
   统计
   ============================================================ */
int passenger_count(void) {
    int n = 0;
    for (Node *t = head; t != NULL; t = t->next) n++;
    return n;
}

// 当前在车旅客的票款合计（退票后随之减少）
int total_fare(void) {
    int total = 0;
    for (Node *t = head; t != NULL; t = t->next) total += t->data.price;
    return total;
}

// 区段 [segment, segment+1) 上的载客人数，恒在 0~52 之间
int segment_load(const char *train_no, const char *date, int segment) {
    if (segment < 0 || segment >= SEGMENT_COUNT) return 0;
    int n = 0;
    for (Node *t = head; t != NULL; t = t->next) {
        if (strcmp(t->data.train_no, train_no) != 0) continue;
        if (strcmp(t->data.travel_date, date) != 0) continue;
        int lo, hi;
        normalize_segment(t->data.board, t->data.alight, &lo, &hi);
        if (lo <= segment && segment < hi) n++;
    }
    return n;
}

// 该座位是否被任何行程占用（区段复用下「部分占用」也算占用）
static int seat_has_any_use(const char *train_no, const char *date,
                            int carriage, int seat) {
    for (Node *t = head; t != NULL; t = t->next) {
        if (t->data.carriage != carriage || t->data.seat != seat) continue;
        if (strcmp(t->data.train_no, train_no) != 0) continue;
        if (strcmp(t->data.travel_date, date) != 0) continue;
        return 1;
    }
    return 0;
}

int carriage_occupied_seats(const char *train_no, const char *date, int carriage) {
    if (carriage < 1 || carriage > CARRIAGE_COUNT) return 0;
    int n = 0;
    for (int s = 1; s <= car_seats[carriage]; s++)
        if (seat_has_any_use(train_no, date, carriage, s)) n++;
    return n;
}

int carriage_fully_free_seats(const char *train_no, const char *date, int carriage) {
    if (carriage < 1 || carriage > CARRIAGE_COUNT) return 0;
    return car_seats[carriage] - carriage_occupied_seats(train_no, date, carriage);
}

// 去重统计某日期下被占用的 (车次, 车厢, 座位) 组合数
int occupied_seat_total(const char *date) {
    int n = 0;
    for (Node *t = head; t != NULL; t = t->next) {
        if (strcmp(t->data.travel_date, date) != 0) continue;
        int seen = 0;
        for (Node *u = head; u != t; u = u->next) {   // 只与前驱比较，避免重复计数
            if (u->data.carriage == t->data.carriage &&
                u->data.seat == t->data.seat &&
                strcmp(u->data.train_no, t->data.train_no) == 0 &&
                strcmp(u->data.travel_date, t->data.travel_date) == 0) {
                seen = 1;
                break;
            }
        }
        if (!seen) n++;
    }
    return n;
}

/* ============================================================
   二叉搜索树索引（按身份证后4位排序）
   ============================================================ */
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

/* ============================================================
   B树索引（按身份证后4位排序）
   ============================================================ */
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

// 统计 B 树中保存的键总数（用于索引一致性自检）
static int btree_key_count(BTreeNode *root) {
    if (root == NULL) return 0;
    int n = root->key_count;
    if (!root->leaf) {
        for (int i = 0; i <= root->key_count; i++) n += btree_key_count(root->children[i]);
    }
    return n;
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

/* 诊断用：校验两棵索引与链表是否一致。
   重复身份证键会让 BST 静默丢弃、B树照常插入，导致两棵索引永久不一致 ——
   那时 search_passenger（走B树）与 delete_passenger（走链表）会命中不同结点，
   留下查不到也删不掉的幽灵记录。测试用这个函数把该不变量钉死。 */
int indexes_are_consistent(void) {
    int list_count = 0;
    for (Node *t = head; t != NULL; t = t->next) {
        list_count++;
        if (find_btree(btree_root, t->data.id) != t) return 0;
        SearchTreeNode *found = find_search_tree(search_root, t->data.id);
        if (found == NULL || found->data != t) return 0;
    }
    return btree_key_count(btree_root) == list_count;
}

/* ============================================================
   链表操作
   ============================================================ */

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
            /* 座位占用是从链表演生的，删除结点即自动释放，无需额外清理 */
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

            free(t);                     // 回收内存（座位占用随结点一并释放）
            rebuild_search_index();      // 删除后更新二叉搜索树索引
            rebuild_btree_index();       // 删除后更新B树索引
            return 1;                    // 成功
        }
        prev = t;
        t = t->next;
    }
    return 0;                            // 没找到
}

/* ============================================================
   存档：二进制读写
   ============================================================ */

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
        fread(&count, sizeof(count), 1, file) != 1 || count < 0 || count > MAX_PASSENGERS) {
        fclose(file);
        return 0;
    }

    free_all_passengers();
    for (int i = 0; i < count; i++) {
        Passenger passenger;
        if (fread(&passenger, sizeof(Passenger), 1, file) != 1 ||
            passenger.carriage < 1 || passenger.carriage > CARRIAGE_COUNT ||
            passenger.seat < 1 || passenger.seat > car_seats[passenger.carriage]) {
            free_all_passengers();
            fclose(file);
            return 0;
        }
        if (!insert_passenger(passenger)) {
            free_all_passengers();
            fclose(file);
            return 0;
        }
    }
    fclose(file);
    return 1;
}
