#include "train_model.h"

/* ============================================================
   全局数据 —— 内存里的"存储"
   ============================================================ */
Node *head = NULL;
SearchTreeNode *search_root = NULL;
BTreeNode *btree_root = NULL;

const char *stations[STATION_COUNT] = {"上海", "苏州", "南京", "济南", "天津", "北京"};

int car_type[CARRIAGE_COUNT + 1]  = {0, 1, 1, 2, 2, 2};      // 1=一等座 2=二等座
int car_seats[CARRIAGE_COUNT + 1] = {0, 8, 8, 12, 12, 12};   // 各车厢座位数
int seat_taken[CARRIAGE_COUNT + 1][MAX_SEAT + 1];            // 已售座位标记

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
    memset(seat_taken, 0, sizeof(seat_taken));
    for (int i = 0; i < count; i++) {
        Passenger passenger;
        if (fread(&passenger, sizeof(Passenger), 1, file) != 1 ||
            passenger.carriage < 1 || passenger.carriage > CARRIAGE_COUNT ||
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
