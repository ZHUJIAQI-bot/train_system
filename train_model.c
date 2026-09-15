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
// 前半段统一按大写比较的规范形式存放，避免 "123x" 与 "123X" 被当成两个不同的人
int valid_id(const char *id) {
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
   记录完整性校验（存档读取与入库漏斗共用）
   ============================================================ */

// 定长字段界内是否存在 NUL 终止符。
// 必须先查这个再调 strlen：损坏文件里 id[5]/train_no[4] 等数组可能整段无 NUL，
// 直接 strlen 会一路读到相邻字段乃至结构体之外。
static int field_terminated(const char *field, size_t size) {
    return memchr(field, '\0', size) != NULL;
}

int validate_passenger(const Passenger *p, char *err, size_t errsz) {
    // 1) 先确认所有定长字段界内有 NUL，后面才敢用 strlen
    if (!field_terminated(p->id, sizeof(p->id))) {
        snprintf(err, errsz, "身份证字段缺少终止符");
        return 0;
    }
    if (!field_terminated(p->name, sizeof(p->name))) {
        snprintf(err, errsz, "姓名字段缺少终止符");
        return 0;
    }
    if (!field_terminated(p->travel_date, sizeof(p->travel_date))) {
        snprintf(err, errsz, "日期字段缺少终止符");
        return 0;
    }
    if (!field_terminated(p->train_no, sizeof(p->train_no))) {
        snprintf(err, errsz, "车次字段缺少终止符");
        return 0;
    }
    if (!field_terminated(p->depart_time, sizeof(p->depart_time))) {
        snprintf(err, errsz, "发车时间字段缺少终止符");
        return 0;
    }

    // 2) 逐字段形状
    if (!valid_id(p->id)) {
        snprintf(err, errsz, "身份证后4位格式不正确");
        return 0;
    }
    if (p->name[0] == '\0') {
        snprintf(err, errsz, "姓名为空");
        return 0;
    }

    // 3) 车次、日期、车站、方向、等级 —— 与售票走同一套规则
    char trip_err[128];
    if (!validate_trip(p->train_no, p->travel_date, p->board, p->alight,
                       p->firstclass, trip_err, sizeof(trip_err))) {
        snprintf(err, errsz, "%s", trip_err);
        return 0;
    }

    // 4) 车厢与座位
    if (p->carriage < 1 || p->carriage > CARRIAGE_COUNT) {
        snprintf(err, errsz, "车厢号应在 1~%d 之间", CARRIAGE_COUNT);
        return 0;
    }
    if (p->seat < 1 || p->seat > car_seats[p->carriage]) {
        snprintf(err, errsz, "座位号应在 1~%d 之间", car_seats[p->carriage]);
        return 0;
    }
    if (car_type[p->carriage] != (p->firstclass ? 1 : 2)) {
        snprintf(err, errsz, "%d号车厢是%s，与所购等级不符",
                 p->carriage, car_type[p->carriage] == 1 ? "一等座" : "二等座");
        return 0;
    }

    // 5) 发车时间必须与车次派生值一致
    char expected_time[6];
    train_depart_time(train_number_of(p->train_no), expected_time);
    if (strcmp(p->depart_time, expected_time) != 0) {
        snprintf(err, errsz, "发车时间 %s 与车次 %s 不符（应为 %s）",
                 p->depart_time, p->train_no, expected_time);
        return 0;
    }
    return 1;
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

// 分配座位：在同一等级的车厢里选「当前旅客人数最少」的那节，再取该车厢最小可用座位号，
// 从而保证每个等级内部各车厢人数平均分配。平局取车厢号小的。
//
// 判据必须用「人数」而不是「余票数」：区段复用下两者会背离 ——
// 某车厢可能坐了 5 个人却只占 5 个座位（余票多），另一车厢坐 3 人占 3 个座位（余票少），
// 按余票选会挑中人数更多的那节，人数就不平均了。
//
// 注意 best_seat 必须保持「该车厢内最小可用座位号」，不能是 -1：
// 车厢若对本区间无可用座位则直接跳过（first_seat < 0），不参与比较。
int assign_seat(const char *train_no, const char *date, int firstclass,
                int board, int alight, int *out_carriage, int *out_seat) {
    int best_carriage = -1;
    int best_people = 0;
    int best_seat = -1;

    for (int c = 1; c <= CARRIAGE_COUNT; c++) {
        if (car_type[c] != (firstclass ? 1 : 2)) continue;   // 等级硬约束

        int first_seat = -1;
        for (int s = 1; s <= car_seats[c]; s++) {            // 上界用本车厢实际座位数
            if (seat_available(train_no, date, c, s, board, alight)) {
                first_seat = s;
                break;
            }
        }
        if (first_seat < 0) continue;                        // 本车厢对该区间已无可用座位

        int people = carriage_headcount(train_no, date, c);
        if (best_carriage < 0 || people < best_people) {      // 严格 <，平局取小车厢号
            best_carriage = c;
            best_people = people;
            best_seat = first_seat;
        }
    }
    if (best_carriage < 0) return 0;                         // 该区间该等级已售罄
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

// 某车厢在某车次某日期下的实际旅客人数（选厢平均分配的依据）
int carriage_headcount(const char *train_no, const char *date, int carriage) {
    int n = 0;
    for (Node *t = head; t != NULL; t = t->next) {
        if (t->data.carriage != carriage) continue;
        if (strcmp(t->data.train_no, train_no) != 0) continue;
        if (strcmp(t->data.travel_date, date) != 0) continue;
        n++;
    }
    return n;
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

    int order = strcmp(data->data.id, root->data->data.id);
    if (order == 0) return root;          // 重复键：与 B 树保持一致的「忽略」语义
    if (order < 0) {
        // 必须接住递归结果再赋值：直接 root->left = ... 时，
        // 一旦深层 malloc 失败返回 NULL，父结点会把整棵已有左子树置空并泄漏
        SearchTreeNode *child = insert_search_tree(root->left, data);
        if (child != NULL) root->left = child;
    } else {
        SearchTreeNode *child = insert_search_tree(root->right, data);
        if (child != NULL) root->right = child;
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

// 分裂成功返回 1；分配失败返回 0，此时父结点保持原样，调用方必须放弃本次插入。
// 若分配失败后仍继续插入，父结点仍指向满结点，叶子分支会写 keys[3] ——
// 越过 keys[3][5] 覆盖到 values[] 指针数组，造成指针损坏。
static int split_btree_child(BTreeNode *parent, int child_index) {
    BTreeNode *full = parent->children[child_index];
    BTreeNode *right = create_btree_node(full->leaf);
    if (right == NULL) return 0;
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
    return 1;
}

static void insert_btree_nonfull(BTreeNode *node, Node *data) {
    int index = node->key_count - 1;
    if (node->leaf) {
        while (index >= 0 && strcmp(data->data.id, node->keys[index]) < 0) {
            strcpy(node->keys[index + 1], node->keys[index]);
            node->values[index + 1] = node->values[index];
            index--;
        }
        // 重复键：与 BST 一样忽略，两棵索引在异常输入下也会「一致地少一个」
        if (index >= 0 && strcmp(data->data.id, node->keys[index]) == 0) return;
        strcpy(node->keys[index + 1], data->data.id);
        node->values[index + 1] = data;
        node->key_count++;
        return;
    }
    while (index >= 0 && strcmp(data->data.id, node->keys[index]) < 0) index--;
    index++;
    if (node->children[index]->key_count == BTREE_MAX_KEYS) {
        if (!split_btree_child(node, index)) return;   // 分裂失败则放弃，绝不带着满结点继续下沉
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
        if (!split_btree_child(new_root, 0)) {
            free(new_root);            // 分裂失败：保留原根，丢弃这个空壳
            return;
        }
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

// 同时重建两棵索引（批量安装时用一次，避免逐条重建的 O(n²)）
static void rebuild_indexes(void) {
    rebuild_search_index();
    rebuild_btree_index();
}

// 尾插：新旅客加到链表末尾（保持上车先后顺序）。
// 这是唯一的入库漏斗 —— 统一重算票价、规范化身份证大小写、拒绝重复身份证。
// 索引改为增量插入，不再每次整树重建（原来加载 n 条是 O(n²)）。
int insert_passenger(Passenger p) {
    p.price = calc_price(p.board, p.alight, p.firstclass);  // 票价是派生量，不信任外部值

    // 身份证末尾的 x 统一成大写，"123x" 与 "123X" 必须是同一个人
    for (size_t i = 0; i + 1 < sizeof(p.id) && p.id[i] != '\0'; i++) {
        if (p.id[i] == 'x') p.id[i] = 'X';
    }

    // 重复身份证必须挡在入库前：两棵索引都以 id 为唯一键，
    // 一旦重复，BST 会静默丢弃而 B 树照常插入，两棵索引将永久不一致。
    if (find_btree(btree_root, p.id) != NULL) return INSERT_DUPLICATE;

    Node *n = malloc(sizeof(Node));
    if (n == NULL) return INSERT_NO_MEMORY;
    n->data = p;
    n->next = NULL;

    if (head == NULL) {
        head = n;
    } else {
        Node *t = head;
        while (t->next != NULL) t = t->next;   // 交互式单张售票，n 很小
        t->next = n;
    }
    search_root = insert_search_tree(search_root, n);
    insert_btree(n);
    return INSERT_OK;
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

    if (removed > 0) rebuild_indexes();
    return removed;
}

// 按身份证查找，返回结点指针，找不到返回 NULL
Node *search_passenger(const char *id) {
    return find_btree(btree_root, id);
}

// 删除旅客（退票）：改 next 指针并回收内存。
// 座位占用是从链表演生的，删除结点即自动释放，无需额外清理。
// 删除后仍走整树重建（O(n)）—— 写 B 树删除算法的风险远大于这点开销。
int delete_passenger(const char *id) {
    Node *t = head, *prev = NULL;
    while (t != NULL) {
        if (strcmp(t->data.id, id) == 0) {
            if (prev == NULL)            // 删除的是头结点
                head = t->next;
            else                         // 让前一个结点跳过它
                prev->next = t->next;

            free(t);
            rebuild_indexes();
            return 1;                    // 成功
        }
        prev = t;
        t = t->next;
    }
    return 0;                            // 没找到
}

/* ============================================================
   存档 v4：字段级序列化 + 全量校验 + 原子加载 + v3 迁移

   为什么不再直接 fwrite 整个结构体：v3 用 sizeof(Passenger) 落盘，
   结构体内部的 padding 字节是未定义的，换个编译器布局就可能对不上。
   v4 逐字段写固定长度，记录恒为 70 字节，与 sizeof(Passenger) 解耦。
   ============================================================ */
#define SAVE_MAGIC         "TRNP"
#define SAVE_VERSION       4
#define SAVE_HEADER_SIZE   16   /* magic(4) + version(4) + count(4) + record_size(4) */
#define SAVE_RECORD_SIZE   70   /* 5+20+11+4+6 = 46 字节字符 + 6 × int32 */
#define LEGACY_V3_RECORD_SIZE 72 /* v3 直接写 sizeof(Passenger)，含 padding */

/* v3 迁移路径依赖旧记录布局与当前 Passenger 完全一致 */
_Static_assert(sizeof(Passenger) == LEGACY_V3_RECORD_SIZE,
               "Passenger 布局已改变，v3 迁移路径需要同步更新");

static int write_i32(FILE *file, int value) {
    unsigned char bytes[4];
    bytes[0] = (unsigned char)(value & 0xFF);
    bytes[1] = (unsigned char)((value >> 8) & 0xFF);
    bytes[2] = (unsigned char)((value >> 16) & 0xFF);
    bytes[3] = (unsigned char)((value >> 24) & 0xFF);
    return fwrite(bytes, 1, 4, file) == 4;
}

static int read_i32(FILE *file, int *out) {
    unsigned char bytes[4];
    if (fread(bytes, 1, 4, file) != 4) return 0;
    *out = (int)((unsigned)bytes[0] | ((unsigned)bytes[1] << 8) |
                 ((unsigned)bytes[2] << 16) | ((unsigned)bytes[3] << 24));
    return 1;
}

// 定长字符字段写盘：NUL 之后的字节一律补 0。
// 不要把结构体 padding 里的垃圾写进文件，否则同样的数据换个编译器就reproduce不出来。
static int write_fixed(FILE *file, const char *text, size_t size) {
    unsigned char buffer[32];
    if (size > sizeof(buffer)) return 0;
    memset(buffer, 0, size);
    memcpy(buffer, text, bounded_len(text, size));
    return fwrite(buffer, 1, size, file) == size;
}

static int read_fixed(FILE *file, char *out, size_t size) {
    return fread(out, 1, size, file) == size;
}

static int write_record(FILE *file, const Passenger *p) {
    if (!write_fixed(file, p->id, sizeof(p->id))) return 0;
    if (!write_fixed(file, p->name, sizeof(p->name))) return 0;
    if (!write_fixed(file, p->travel_date, sizeof(p->travel_date))) return 0;
    if (!write_fixed(file, p->train_no, sizeof(p->train_no))) return 0;
    if (!write_fixed(file, p->depart_time, sizeof(p->depart_time))) return 0;
    if (!write_i32(file, p->board)) return 0;
    if (!write_i32(file, p->alight)) return 0;
    if (!write_i32(file, p->price)) return 0;
    if (!write_i32(file, p->carriage)) return 0;
    if (!write_i32(file, p->seat)) return 0;
    if (!write_i32(file, p->firstclass)) return 0;
    return 1;
}

static int read_record(FILE *file, Passenger *p) {
    memset(p, 0, sizeof(*p));
    if (!read_fixed(file, p->id, sizeof(p->id))) return 0;
    if (!read_fixed(file, p->name, sizeof(p->name))) return 0;
    if (!read_fixed(file, p->travel_date, sizeof(p->travel_date))) return 0;
    if (!read_fixed(file, p->train_no, sizeof(p->train_no))) return 0;
    if (!read_fixed(file, p->depart_time, sizeof(p->depart_time))) return 0;
    if (!read_i32(file, &p->board)) return 0;
    if (!read_i32(file, &p->alight)) return 0;
    if (!read_i32(file, &p->price)) return 0;
    if (!read_i32(file, &p->carriage)) return 0;
    if (!read_i32(file, &p->seat)) return 0;
    if (!read_i32(file, &p->firstclass)) return 0;
    return 1;
}

static long file_size_of(FILE *file) {
    long current = ftell(file);
    if (current < 0) return -1;
    if (fseek(file, 0, SEEK_END) != 0) return -1;
    long size = ftell(file);
    fseek(file, current, SEEK_SET);
    return size;
}

// 拒收时把坏文件改名留档，绝不就地覆盖
static void backup_rejected_file(const char *filename) {
    char backup[700];
    time_t now = time(NULL);
    struct tm stamp = *localtime(&now);
    snprintf(backup, sizeof(backup), "%s.bad-%04d%02d%02d-%02d%02d%02d",
             filename, stamp.tm_year + 1900, stamp.tm_mon + 1, stamp.tm_mday,
             stamp.tm_hour, stamp.tm_min, stamp.tm_sec);
    remove(backup);
    rename(filename, backup);
}

static void free_node_chain(Node *first) {
    while (first != NULL) {
        Node *next = first->next;
        free(first);
        first = next;
    }
}

// 对读入的记录做全量校验，并在通过后统一重算票价、规范化身份证大小写
static int validate_records(Passenger *records, int count, char *err, size_t errsz) {
    for (int i = 0; i < count; i++) {
        char reason[160];
        if (!validate_passenger(&records[i], reason, sizeof(reason))) {
            snprintf(err, errsz, "第 %d 条记录无效：%s", i + 1, reason);
            return 0;
        }
        records[i].price = calc_price(records[i].board, records[i].alight,
                                      records[i].firstclass);
        for (size_t k = 0; k + 1 < sizeof(records[i].id) && records[i].id[k] != '\0'; k++) {
            if (records[i].id[k] == 'x') records[i].id[k] = 'X';
        }
    }

    // 跨记录约束：身份证必须唯一，同车次同日期同座位的区段不得重叠。
    // O(n²)，但内层先用整数比较短路，且只在加载时做一次，n 上限 10000，可以接受。
    for (int i = 0; i < count; i++) {
        for (int j = 0; j < i; j++) {
            if (strcmp(records[i].id, records[j].id) == 0) {
                snprintf(err, errsz, "第 %d 条与第 %d 条身份证重复", j + 1, i + 1);
                return 0;
            }
            if (records[i].carriage != records[j].carriage) continue;
            if (records[i].seat != records[j].seat) continue;
            if (strcmp(records[i].train_no, records[j].train_no) != 0) continue;
            if (strcmp(records[i].travel_date, records[j].travel_date) != 0) continue;
            if (seat_segments_overlap(records[i].board, records[i].alight,
                                      records[j].board, records[j].alight)) {
                snprintf(err, errsz,
                         "第 %d 条与第 %d 条在同一车次同一座位且乘车区段重叠",
                         j + 1, i + 1);
                return 0;
            }
        }
    }
    return 1;
}

const char *default_data_file(void) {
    return "D:/train_system/passengers.dat";
}

// 保存到 filename：先写 filename.tmp，成功后才替换原文件。
// 这样即使写到一半崩溃，原文件仍然完好，不会留下截断的存档。
int save_passengers(const char *filename) {
    char tmp_path[600];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", filename);

    FILE *file = fopen(tmp_path, "wb");
    if (file == NULL) return 0;

    int ok = 1;
    int count = passenger_count();

    if (fwrite(SAVE_MAGIC, 1, 4, file) != 4) ok = 0;
    if (ok && !write_i32(file, SAVE_VERSION)) ok = 0;
    if (ok && !write_i32(file, count)) ok = 0;
    if (ok && !write_i32(file, SAVE_RECORD_SIZE)) ok = 0;
    for (Node *current = head; ok && current != NULL; current = current->next) {
        if (!write_record(file, &current->data)) ok = 0;
    }
    if (fclose(file) != 0) ok = 0;

    if (!ok) {
        remove(tmp_path);
        return 0;
    }
    // Windows 的 rename 不覆盖已存在文件，故先删目标再改名。
    // 该窗口极短（远小于整个写入过程），且坏文件已由备份路径兜底。
    remove(filename);
    if (rename(tmp_path, filename) != 0) {
        remove(tmp_path);
        return 0;
    }
    return 1;
}

// 读取 filename。全过程先读到临时数组并校验通过，最后才切换全局状态，
// 因此「文件损坏」绝不会破坏内存中已有的数据。
int load_passengers(const char *filename) {
    FILE *file = fopen(filename, "rb");
    if (file == NULL) return LOAD_NO_FILE;

    long size = file_size_of(file);
    unsigned char header[4];
    Passenger *records = NULL;
    int count = 0;
    int parsed = 0;
    int legacy = 0;

    if (size >= 4 && fread(header, 1, 4, file) == 4) {
        if (memcmp(header, SAVE_MAGIC, 4) == 0) {
            /* ---- v4 ---- */
            int version = 0, record_size = 0;
            if (read_i32(file, &version) && read_i32(file, &count) &&
                read_i32(file, &record_size) &&
                version == SAVE_VERSION && record_size == SAVE_RECORD_SIZE &&
                count >= 0 && count <= MAX_PASSENGERS &&
                size == SAVE_HEADER_SIZE + (long)count * SAVE_RECORD_SIZE) {
                records = malloc((size_t)(count > 0 ? count : 1) * sizeof(Passenger));
                if (records != NULL) {
                    parsed = 1;
                    for (int i = 0; i < count && parsed; i++) {
                        if (!read_record(file, &records[i])) parsed = 0;
                    }
                    if (parsed && fgetc(file) != EOF) parsed = 0;  /* 尾部不得有多余字节 */
                }
            }
        } else {
            /* ---- v3 迁移：老格式没有 magic，前 4 字节就是 version ---- */
            int version = (int)((unsigned)header[0] | ((unsigned)header[1] << 8) |
                                ((unsigned)header[2] << 16) | ((unsigned)header[3] << 24));
            if (version == 3 && read_i32(file, &count) &&
                count >= 0 && count <= MAX_PASSENGERS &&
                size == 8 + (long)count * LEGACY_V3_RECORD_SIZE) {
                records = malloc((size_t)(count > 0 ? count : 1) * sizeof(Passenger));
                if (records != NULL) {
                    parsed = 1;
                    legacy = 1;
                    for (int i = 0; i < count && parsed; i++) {
                        if (fread(&records[i], LEGACY_V3_RECORD_SIZE, 1, file) != 1) parsed = 0;
                    }
                }
            }
        }
    }
    fclose(file);

    if (!parsed) {
        free(records);
        backup_rejected_file(filename);
        return LOAD_REJECTED;
    }

    char reason[192];
    if (!validate_records(records, count, reason, sizeof(reason))) {
        free(records);
        backup_rejected_file(filename);
        return LOAD_REJECTED;
    }

    // 先把临时链表建完，全部成功再动全局状态
    Node *tmp_head = NULL, *tmp_tail = NULL;
    for (int i = 0; i < count; i++) {
        Node *n = malloc(sizeof(Node));
        if (n == NULL) {
            free_node_chain(tmp_head);
            free(records);
            return LOAD_NO_MEMORY;
        }
        n->data = records[i];
        n->next = NULL;
        if (tmp_tail != NULL) tmp_tail->next = n;
        else tmp_head = n;
        tmp_tail = n;
    }
    free(records);

    free_all_passengers();
    head = tmp_head;
    rebuild_indexes();

    // 从 v3 迁移过来的数据立即以 v4 重写回盘
    if (legacy) save_passengers(filename);
    return LOAD_OK;
}
