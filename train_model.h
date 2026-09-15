#ifndef TRAIN_MODEL_H
#define TRAIN_MODEL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/* ============================================================
   第1步：数据 —— 定义"旅客"长什么样（对应 PPT 里的旅客车票信息）
   ============================================================ */
#define STATION_COUNT   6                     // 车站数量（0~5）
#define CARRIAGE_COUNT  5                     // 车厢数量（1~5）
#define MAX_SEAT        12                    // 每节车厢最多座位数
#define MAX_PASSENGERS  10000                 // 存档中允许的最大记录数

typedef struct Passenger {
    char id[5];           // 身份证后4位（字符串，留1位给'\0'）
    char name[20];        // 姓名
    char travel_date[11]; // 出行日期 YYYY-MM-DD
    char train_no[4];     // 车次，例如 G1
    char depart_time[6];  // 发车时间 HH:MM
    int  board;           // 上车车站编号（0~5）
    int  alight;          // 下车车站编号（0~5）
    int  price;           // 票价
    int  carriage;        // 车厢号（1~5）
    int  seat;            // 座位号
    int  firstclass;      // 0=二等座  1=一等座
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

/* ---------------- 全局数据（内存里的"存储"） ---------------- */
extern Node *head;                     // 链表头指针，空链表时是 NULL
extern SearchTreeNode *search_root;    // 二叉搜索树索引根
extern BTreeNode *btree_root;          // B树索引根

extern const char *stations[STATION_COUNT];
extern int car_type[CARRIAGE_COUNT + 1];     // 车厢1~5的类型：1=一等座 2=二等座
extern int car_seats[CARRIAGE_COUNT + 1];    // 车厢1~5的座位数
extern int seat_taken[CARRIAGE_COUNT + 1][MAX_SEAT + 1]; // [车厢][座位]=1 表示已售出

/* ============================================================
   第3步：操作 —— 一组函数，在这些字段上增删改查
   ============================================================ */

// 计算票价：每站150元，一等座再加100元（上下车站不分先后，取站数绝对值）
int calc_price(int board, int alight, int firstclass);

// 校验身份证后4位：4位数字，或 3位数字 + 末尾 x/X
int valid_id(char *id);

// 校验真实日期，格式为 YYYY-MM-DD
int valid_date(const char *date);

/* ---------------- 链表与索引 ---------------- */
int   insert_passenger(Passenger p);          // 尾插，返回 0 表示内存分配失败
int   delete_passenger(char *id);             // 按身份证删除，返回 0 表示未找到
Node *search_passenger(char *id);             // 走B树索引查找，找不到返回 NULL
void  free_all_passengers(void);              // 释放链表与两棵索引树

// 删除出行日期早于 today 的旅客，返回删除条数
int remove_expired_passengers(const char *today);

/* ---------------- 存档 ---------------- */
int save_passengers(const char *filename);    // 返回 0 表示写失败
int load_passengers(const char *filename);    // 返回 0 表示读失败

#endif /* TRAIN_MODEL_H */
