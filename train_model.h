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
#define TRAIN_COUNT     10                    // 车次数量（G1~G10）
#define MAX_PASSENGERS  10000                 // 存档中允许的最大记录数
#define SEGMENT_COUNT   (STATION_COUNT - 1)   // 相邻车站构成的区段数（0~4）

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

/* 注意：座位占用不再是独立状态。seat_taken[][] 已被删除，
   改为按「车次 + 日期 + 乘车区段」从链表实时派生，见下方座位函数。 */

/* ============================================================
   第3步：操作 —— 一组函数，在这些字段上增删改查
   ============================================================ */

// 计算票价：每站150元，一等座再加100元（上下车站不分先后，取站数绝对值）
int calc_price(int board, int alight, int firstclass);

// 校验身份证后4位：4位数字，或 3位数字 + 末尾 x/X
int valid_id(const char *id);

// 校验真实日期，格式为 YYYY-MM-DD
int valid_date(const char *date);

/* ---------------- 记录完整性校验 ---------------- */
// 校验一条旅客记录的全部字段（含定长字段界内是否有 NUL 终止符）。
// 通过返回 1；失败返回 0 并把原因写入 err。
// 注意：price 不参与校验 —— 它是派生量，由 insert_passenger 统一重算。
int validate_passenger(const Passenger *p, char *err, size_t errsz);

/* ---------------- 车次 ---------------- */
// 车次规则：G1~G10。奇数车次 北京(5)→上海(0)，偶数车次 上海(0)→北京(5)
int  valid_train_no(const char *train_no);              // 形如 G1..G10 返回 1
int  train_number_of(const char *train_no);             // 解析出 1~10，非法返回 0
int  train_is_northbound(int train_number);             // 1 = 北京→上海（奇数车次）
void train_depart_time(int train_number, char out[6]);  // 奇数 :00、偶数 :30，从 06:00 起

/* ---------------- 日期工具 ---------------- */
void today_string(char out[11]);                 // 写入今天的 YYYY-MM-DD
void date_offset_string(int offset, char out[11]); // 相对今天偏移若干天

/* ---------------- 行程校验 ---------------- */
// 统一校验入口：车次、日期、上下车站、方向、等级。
// 通过返回 1；失败返回 0 并把原因写入 err。
int validate_trip(const char *train_no, const char *date, int board, int alight,
                  int firstclass, char *err, size_t errsz);

/* ---------------- 座位：从链表演生，无独立状态 ----------------
   冲突判定为「同车次 + 同日期 + 同车厢座位，且乘车区段有重叠」。
   因此 上海→南京 与 济南→北京 可以复用同一个座位。 */
int seat_segments_overlap(int board_a, int alight_a, int board_b, int alight_b);
int seat_available(const char *train_no, const char *date,
                   int carriage, int seat, int board, int alight);
int seats_available(const char *train_no, const char *date, int firstclass,
                    int board, int alight);
// 选「当前旅客人数最少」的合格车厢，再取该车厢最小可用座位号；成功返回 1
// 平局取车厢号小的，保证同等级各车厢人数差不超过 1。
int assign_seat(const char *train_no, const char *date, int firstclass,
                int board, int alight, int *out_carriage, int *out_seat);

/* ---------------- 统计 ---------------- */
int passenger_count(void);
int total_fare(void);                                            // 当前在车旅客票款合计
int segment_load(const char *train_no, const char *date, int segment); // 覆盖该区段的人数
int carriage_headcount(const char *train_no, const char *date, int carriage);
int carriage_fully_free_seats(const char *train_no, const char *date, int carriage);
int carriage_occupied_seats(const char *train_no, const char *date, int carriage);
int occupied_seat_total(const char *date);                       // 去重 车次×车厢×座位
int indexes_are_consistent(void);                                // 诊断：两棵索引与链表是否一致

// 链表内容的修订号，每次增删改后递增。
// 统计类函数是 O(n)~O(n²)，界面每帧都要画，数据没变时不该反复重算：
// 调用方缓存上一次的修订号，变了才重新计算。
unsigned model_revision(void);

/* ---------------- 链表与索引 ---------------- */
// insert_passenger 是唯一的入库漏斗：会重算 price 并拒绝重复身份证。
// 返回值用 INSERT_OK = 0 表示成功，调用方必须显式比较，避免 !ret 把错误码当成成功。
typedef enum {
    INSERT_OK = 0,
    INSERT_DUPLICATE,
    INSERT_NO_MEMORY
} InsertResult;

int   insert_passenger(Passenger p);
int   delete_passenger(const char *id);       // 按身份证删除，返回 0 表示未找到
Node *search_passenger(const char *id);       // 走B树索引查找，找不到返回 NULL
void  free_all_passengers(void);              // 释放链表与两棵索引树

// 删除出行日期早于 today 的旅客，返回删除条数
int remove_expired_passengers(const char *today);

/* ---------------- 存档 ---------------- */
// load 的返回值同样用 0 表示成功，便于区分「没有文件」与「文件损坏被拒收」
typedef enum {
    LOAD_OK = 0,
    LOAD_NO_FILE,        // 文件不存在（首次运行）
    LOAD_REJECTED,       // 文件存在但未通过校验，已备份为 <名字>.bad-<时间戳>
    LOAD_NO_MEMORY
} LoadResult;

int save_passengers(const char *filename);    // 返回 0 表示写失败
int load_passengers(const char *filename);    // 返回 LoadResult

/* ---------------- 存档路径 ----------------
   路径不再写死，而是按以下顺序查找首个可用位置，结果缓存在静态缓冲里：
     1. 环境变量 TRAIN_DATA_FILE
     2. 可执行文件同级目录/passengers.dat   ← 整个项目挪到别的目录也能用
     3. 项目根目录/passengers.dat           ← 相对编译期记录的 __FILE__ 推导
     4. 当前工作目录/passengers.dat         ← 最后兜底
   返回的是进程内静态缓冲的指针，不要 free。 */
const char *default_data_file(void);

/* ---------------- 过期票清理保护 ----------------
   直接调 remove_expired_passengers 有误删风险：它按 travel_date < today
   全量删除，而 today 来自系统时钟。时钟被改错、或跨时区旅行，
   都可能把还没出行的票整批删掉。

   改用这个入口，它会：
     - 要求 today 的年份落在 [2020, 2099]，越界则拒绝清理
     - 在真正删除前，把当前存档另存为 <archive_path>.before-expire-<时间戳>，
       误删之后还能捞回来

   archive_path 传 NULL 表示不做备份。返回实际删除条数；
   被拒绝或无需清理时返回 0，原因写入 reason（可为 NULL）。 */
int purge_expired_passengers(const char *today, const char *archive_path,
                             char *reason, size_t reasonsz);

// 最近一次清理写出的备份文件路径；没写则为空串。
// 界面可以据此告诉用户去哪里找回误删的数据。
const char *last_purge_backup_path(void);

#endif /* TRAIN_MODEL_H */
