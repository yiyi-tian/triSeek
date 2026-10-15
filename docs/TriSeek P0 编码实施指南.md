# TriSeek P0 编码实施指南

> 适用对象：TriSeek 五人小组及组长  
> 规范来源：《Linux 操作系统编程：TriSeek 系统设计文档》v1.5  
> 适用代码：虚拟机 `~/triSeek` 中的真实项目  
> 文档性质：设计到代码之间的实施合同，不代表功能已经实现

## 1. P0 范围

### 1.1 本轮必须实现

- 单 `epoll` LT 主线程；
- 一个固定 Worker 线程池；
- 有界任务队列和有界结果队列；
- 目录发现与整文件流式搜索；
- 每查询待派发队列、公平轮询和并行配额；
- 取消、断连、背压、唯一任务终态和优雅退出；
- 三节点文件级静态 LPT 分配；
- 10 用户脚本、小数据正确性对照和单机/三机一致性测试。

### 1.2 本轮明确不实现

- 大文件分块和乱序重排（P1）；
- 心跳、自动重试、任务重派（P2）；
- 动态线程池、工作窃取、多 Reactor；
- 协调节点故障迁移；
- 正则表达式、认证和跨查询缓存。

允许在结构体或配置中保留 `offset/length/attempt/enable_chunking` 扩展位，但 P0 不得启用相应行为。

---

## 2. 目标目录

第一步把 `include/triSeek` 改为全小写 `include/triseek`。Linux 文件名区分大小写，小写路径能避免组员写出不一致的 `#include`。

```text
triSeek/
├── Makefile
├── README.md
├── config/
│   ├── node1.conf
│   ├── node2.conf
│   ├── node3.conf
│   └── client.conf
├── docs/
│   ├── SYSTEM_DESIGN.md
│   ├── IMPLEMENTATION_GUIDE.md
│   ├── PROTOCOL.md
│   └── TEAM_PLAN.md
├── include/triseek/
│   ├── common.h
│   ├── config.h
│   ├── byte_buffer.h
│   ├── protocol.h
│   ├── connection.h
│   ├── blocking_queue.h
│   ├── thread_pool.h
│   ├── task.h
│   ├── result.h
│   ├── discovery.h
│   ├── search.h
│   ├── query.h
│   ├── scheduler.h
│   ├── distributed.h
│   └── server.h
├── src/
│   ├── common/
│   │   ├── common.c
│   │   ├── config.c
│   │   └── byte_buffer.c
│   ├── protocol/
│   │   ├── protocol.c
│   │   └── protocol_body.c
│   ├── worker/
│   │   ├── blocking_queue.c
│   │   ├── thread_pool.c
│   │   ├── discovery.c
│   │   └── search.c
│   ├── core/
│   │   ├── task.c
│   │   ├── result.c
│   │   ├── query.c
│   │   ├── scheduler.c
│   │   └── distributed.c
│   ├── server/
│   │   ├── connection.c
│   │   ├── message_router.c
│   │   ├── server.c
│   │   └── main.c
│   └── client/
│       └── main.c
├── tests/
│   ├── unit/
│   ├── integration/
│   └── fixtures/
└── scripts/
    ├── generate_testdata.sh
    ├── generate_manifest.sh
    ├── verify_manifest.sh
    ├── compare_with_grep.sh
    └── simulate_users.sh
```

不要一次创建大量内容为空的 `.c`。目录可以先建好，文件应在其所属里程碑开始时加入，并且同一提交中至少包含编译入口或测试。

---

## 3. 模块依赖和禁止关系

### 3.1 允许的依赖方向

```text
common / byte_buffer
        ↓
protocol / config
        ↓
task / result / discovery / search / blocking_queue / thread_pool
        ↓
query / scheduler / distributed
        ↓
connection / message_router / server
        ↓
client/main 与 server/main
```

`Makefile` 可以把这些源文件链接到不同可执行文件，但业务模块不能反向包含 `server.h`。

### 3.2 必须通过评审检查的禁止关系

- `protocol.c` 中不得出现 `send`、`recv`、`epoll_ctl`；
- `search.c`、`discovery.c` 和 Worker 回调中不得出现 Socket 操作；
- `server.c` 和 `message_router.c` 中不得扫描文件内容或递归目录；
- Worker 不得写 `QueryContext` 的状态、计数和统计；
- `SearchResult` 不得携带裸 `QueryContext *`；
- `epoll` 主线程不得调用会等待队列空间的阻塞入队函数；
- 不得使用 `send(fd, &struct, sizeof(struct))` 发送协议结构体；
- 不得把接收缓冲强转为协议头结构体指针。

---

## 4. 每个文件具体负责什么

### 4.1 公共基础

#### `common.h` / `common.c`

实现：

- `ts_error_t` 错误码；
- P0 全局上限和默认值；
- 单调时钟毫秒值；
- `query_id` 生成与拆分；
- 带 `node_id/query_id/task_id/thread_id/event` 字段的日志入口；
- 安全的大小加法和乘法，避免容量计算溢出。

不实现：查询状态、Socket、队列和文件搜索。

建议接口：

```c
typedef enum {
    TS_OK = 0,
    TS_E_NOENT = -1,
    TS_E_ACCES = -2,
    TS_E_IO = -3,
    TS_E_BADPROTO = -4,
    TS_E_TOO_LARGE = -5,
    TS_E_CANCELED = -6,
    TS_E_TIMEOUT = -7,
    TS_E_BUSY = -8,
    TS_E_NOMEM = -9,
    TS_E_CLOSED = -10
} ts_error_t;

uint64_t ts_now_monotonic_ms(void);
uint64_t ts_make_query_id(uint16_t node_id, uint64_t sequence);
uint16_t ts_query_coordinator(uint64_t query_id);
```

#### `config.h` / `config.c`

实现：

- 解析 `key=value`；
- 解析三台 peer 的 `node_id,address,port`；
- 默认值填充；
- 启动校验和派生值计算。

校验顺序固定为：

```text
workers >= 1
effective_reserved = min(reserved_control_slots, workers - 1)
search_slots = max(1, workers - effective_reserved)
effective_reserved <= floor(workers / 2)
min_quota <= max_quota
progress_interval_ms > 0
recv_buf_cap >= TS_HEADER_SIZE + TS_MAX_BODY_SIZE
enable_chunking == false
```

#### `byte_buffer.h` / `byte_buffer.c`

实现：

- 初始化、销毁、扩容；
- 尾部追加；
- 返回可读指针与长度；
- 消费前缀和必要时压缩；
- 容量上限检查。

它只保存字节，不理解协议。接收缓冲和发送缓冲都复用该模块。

最低接口：

```c
int ts_buffer_init(ts_byte_buffer_t *buf, size_t initial_cap, size_t max_cap);
int ts_buffer_append(ts_byte_buffer_t *buf, const void *data, size_t len);
const uint8_t *ts_buffer_read_ptr(const ts_byte_buffer_t *buf);
size_t ts_buffer_readable(const ts_byte_buffer_t *buf);
void ts_buffer_consume(ts_byte_buffer_t *buf, size_t len);
void ts_buffer_destroy(ts_byte_buffer_t *buf);
```

### 4.2 协议

#### `protocol.h` / `protocol.c`

P0 一开始就使用正式的 32 字节头，不先做另一套临时线上协议。

```text
0..1    magic       0x5453
2       version     1
3       type
4..5    flags       LAST=1, ERROR=2, REGISTER=4
6..7    header_len  32
8..11   body_len    <= 1 MiB
12..19  query_id
20..27  task_id
28..31  reserved    发送为0，接收忽略
```

建议冻结消息编号：

```c
typedef enum {
    TS_MSG_QUERY_REQUEST   = 1,
    TS_MSG_QUERY_ACCEPTED  = 2,
    TS_MSG_MATCH_RESULT    = 3,
    TS_MSG_QUERY_PROGRESS  = 4,
    TS_MSG_QUERY_COMPLETE  = 5,
    TS_MSG_ERROR_RESPONSE  = 6,

    TS_MSG_TASK_ASSIGN     = 16,
    TS_MSG_TASK_RESULT     = 17,
    TS_MSG_TASK_TERMINAL   = 18,
    TS_MSG_CANCEL_QUERY    = 19,
    TS_MSG_QUERY_RELEASE   = 20,

    TS_MSG_NODE_HEARTBEAT  = 32 /* P2保留，P0不处理 */
} ts_message_type_t;
```

如果小组已经在真实代码中使用了另一套稳定编号，只允许在 M0 评审时统一修改一次，并同步更新 `PROTOCOL.md` 和协议测试。

内存消息对象不等于线上头结构：

```c
typedef struct {
    uint8_t type;
    uint16_t flags;
    uint64_t query_id;
    uint64_t task_id;
    uint32_t body_len;
    uint8_t *body; /* message拥有 */
} ts_message_t;

void ts_message_init(ts_message_t *msg);
int ts_message_set_body(ts_message_t *msg, const void *body, uint32_t len);
void ts_message_destroy(ts_message_t *msg);
int ts_protocol_encode(const ts_message_t *msg, ts_byte_buffer_t *out);
int ts_protocol_try_decode(ts_byte_buffer_t *in, ts_message_t *out);
```

`try_decode` 返回值必须区分：完整一帧、数据不足、协议错误。数据不足不是错误，也不能消费半帧。

#### `protocol_body.c`

实现统一的 body writer/reader，再在其上实现业务消息 builder/parser：

```c
int ts_body_write_u32(ts_body_writer_t *w, uint32_t value);
int ts_body_write_u64(ts_body_writer_t *w, uint64_t value);
int ts_body_write_bytes(ts_body_writer_t *w, const void *p, uint32_t len);
int ts_body_read_u32(ts_body_reader_t *r, uint32_t *value);
int ts_body_read_bytes(ts_body_reader_t *r, const uint8_t **p, uint32_t len);
```

reader 必须检查剩余长度；parser 必须检查消息体恰好消费完，禁止忽略多余尾部。

### 4.3 并发基础设施

#### `blocking_queue.h` / `blocking_queue.c`

同一个实现同时支持：

- 条目数上限；
- 可选字节数上限；
- 阻塞 `push/pop`；
- 非阻塞 `try_push/try_pop`；
- `close` 后广播唤醒；
- 水位快照。

建议接口：

```c
typedef size_t (*ts_item_size_fn)(const void *item);

int ts_queue_init(ts_blocking_queue_t *q,
                  size_t item_cap,
                  size_t byte_cap,
                  ts_item_size_fn size_fn);
int ts_queue_push(ts_blocking_queue_t *q, void *item);       /* Worker可阻塞 */
int ts_queue_try_push(ts_blocking_queue_t *q, void *item);   /* epoll只能调用这个 */
int ts_queue_pop(ts_blocking_queue_t *q, void **item);
int ts_queue_try_pop(ts_blocking_queue_t *q, void **item);
void ts_queue_close(ts_blocking_queue_t *q);
void ts_queue_destroy(ts_blocking_queue_t *q,
                      void (*destroy_item)(void *));
```

所有权合同：成功 push 后所有权转给队列；失败时仍属于调用者。结果队列的 `size_fn` 必须计算实际动态负载，不能只返回 `sizeof(pointer)`。

#### `thread_pool.h` / `thread_pool.c`

实现固定线程创建、任务领取、停止和 join。线程池不知道查询、公平性和 Socket。

```c
int ts_thread_pool_init(ts_thread_pool_t *pool,
                        size_t workers,
                        ts_blocking_queue_t *task_queue,
                        ts_task_handler_fn handler,
                        void *handler_arg);
int ts_thread_pool_start(ts_thread_pool_t *pool);
int ts_thread_pool_submit_nowait(ts_thread_pool_t *pool,
                                  ts_search_task_t *task);
void ts_thread_pool_shutdown(ts_thread_pool_t *pool);
```

`submit_nowait` 只能转调队列的 `try_push`，不能因为名字是线程池接口而偷偷阻塞。

### 4.4 Worker 输入输出

#### `task.h` / `task.c`

P0 任务只有两类：`DISCOVER` 和 `SEARCH_FILE`。任务字段至少包括：

```c
typedef enum {
    TS_TASK_DISCOVER = 1,
    TS_TASK_SEARCH_FILE = 2
} ts_task_kind_t;

typedef struct {
    const uint8_t *bytes;
    uint32_t len;
} ts_pattern_view_t;

typedef struct ts_search_task {
    ts_task_kind_t kind;
    uint64_t query_id;
    uint64_t task_id;
    char *path;                 /* task拥有，NUL结尾供POSIX调用 */
    size_t path_len;
    dev_t dev;
    ino_t ino;
    uint64_t file_size;
    uint64_t offset;            /* P0=0 */
    uint64_t length;            /* P0=file_size */
    ts_pattern_view_t pattern;  /* 非拥有引用 */
    const _Atomic bool *cancel; /* 非拥有稳定引用 */
    uint32_t attempt;           /* P0=0 */
} ts_search_task_t;
```

任务成功提交后由 Worker 销毁。`pattern` 和 `cancel` 的有效期由查询生命周期保证。

#### `result.h` / `result.c`

结果至少分为：

- `DISCOVERY_COMPLETE`；
- `MATCH_BATCH`；
- `TASK_PROGRESS`；
- `TASK_TERMINAL`。

终态只允许 `COMPLETE/ERROR/DROPPED`。结果中只有 `query_id/task_id`，没有查询指针。

必须实现：

```c
size_t ts_result_memory_size(const void *result);
void ts_result_destroy(void *result);
```

第一个函数供结果队列执行字节上限核算。

### 4.5 目录发现与搜索

#### `discovery.h` / `discovery.c`

实现路径数组、目录和 glob 展开：

- 使用 `lstat/fstatat`，不信任 `dirent.d_type`；
- 不跟随符号链接；
- 按 `(st_dev, st_ino)` 去重；
- 普通 `ENOENT/EACCES` 记录到发现结果，其他输入继续；
- 输出文件路径、长度、设备号、inode 和文件大小；
- 不创建搜索任务，也不做 LPT。

#### `search.h` / `search.c`

实现一个完整文件的流式固定字节串搜索：

- 使用固定 1～4 MiB 读缓冲；
- LF 分行；CRLF 显示时剥离末尾 CR；孤立 CR 保留；
- 文件最后无 LF 仍生成最后一行；
- 按字节匹配，文件中的 NUL 不截断；
- 同一行多次命中只输出一条匹配行；
- 20 KiB 行在第 10 KiB 命中仍必须发现；
- 显示内容最多保留原始行前 4 KiB，并标记截断；
- 每批最多 1000 行且编码后最多 256 KiB；
- 定期产生进度结果；
- 在读循环安全点检查取消标志；
- 最后无论成功、错误还是取消，都生成一次终态。

搜索模块通过 `ts_result_sink_fn` 交付结果，不知道结果最终进入队列还是单元测试捕获器。

### 4.6 查询和调度

#### `query.h` / `query.c`

`QueryContext` 应在头文件中保持不透明：

```c
typedef struct ts_query_context ts_query_context_t;
```

只有 `query.c` 定义完整字段。主线程专用接口至少覆盖：

- 创建并深拷贝查询规格；
- 状态迁移；
- 添加待派发任务；
- 任务进入全局队列；
- 接收并去重终态；
- 统计匹配、错误、扫描字节和耗时；
- 取消并把未派发任务结算为 `DROPPED`；
- 判断完成；
- 最终释放。

状态只能按以下方向移动：

```text
CREATED
→ DISCOVERY_PENDING
→ DISCOVERING
→ SEARCH_READY
→ SEARCHING
→ COMPLETING
→ FINISHED
```

任意非终态可以进入 `CANCELLED` 或 `FAILED`。正常完成条件必须同时满足：

```text
discovery_done
&& pending_search_tasks == 0
&& running_search_tasks == 0
&& finished_search_tasks + dropped_search_tasks == total_search_tasks
```

#### `scheduler.h` / `scheduler.c`

调度器是 `epoll` 主线程中的逻辑模块，不创建调度线程。

每轮：

1. 优先检查发现任务；
2. `control_burst_limit` 后允许搜索任务；
3. 从轮询游标开始寻找未超配额的查询；
4. 检查本地搜索槽、任务队列或远程发送容量；
5. 只使用非阻塞提交；
6. 队列满则保留任务并转向下一个查询；
7. 最多成功派发 `dispatch_budget=32` 个任务就返回事件循环。

单查询文件顺序在进入 `SEARCH_READY` 时一次性确定：前 `K=min(当时配额, 文件数)` 个选最小文件，其余按大小降序，之后不重排。

### 4.7 网络和服务进程

#### `connection.h` / `connection.c`

每个连接保存：

- fd 和连接类型（监听、客户端、节点）；
- 连接/握手状态；
- 接收缓冲；
- 发送缓冲及已发送偏移；
- 是否订阅 `EPOLLOUT`；
- 写无进展时间；
- 客户端关联查询编号；
- 节点连接上的未结算远程任务集合。

发送缓冲从空变为非空时添加 `EPOLLOUT`；清空时取消。关闭节点连接前先为其未结算远程任务合成错误终态。

#### `message_router.c`

只处理已经完整解码的消息：

- 客户端只能发 `QUERY_REQUEST`；
- 协调节点向 Worker 发送 `TASK_ASSIGN/CANCEL_QUERY/QUERY_RELEASE`；
- Worker 返回 `TASK_RESULT/TASK_TERMINAL`；
- 未知类型、方向错误、flags 冲突返回 `TS_E_BADPROTO`；
- 路由器调用查询、调度或连接接口，不执行文件扫描。

#### `server.h` / `server.c`

服务端生命周期：

```text
读取并校验配置
→ 初始化队列和线程池
→ 创建 listen/epoll/eventfd/signalfd
→ 启动 Worker
→ 进入 epoll_wait 循环
→ 处理 accept/read/write/result/signal
→ 每轮末尾执行有预算的公平派发
→ 优雅退出
```

网络约定：LT 模式、所有 Socket 非阻塞、`accept4`/`recv` 循环到 `EAGAIN`、`EINTR` 重试、`send(MSG_NOSIGNAL)` 处理部分写。

Worker 成功把结果放入结果队列后写一次非阻塞 `eventfd`。主线程收到唤醒后执行“抽干队列→读 eventfd→再抽一次队列”。

#### `server/main.c`

只做命令行、配置加载、日志初始化、调用 `ts_server_init/run/destroy`。业务逻辑不得堆到 `main.c`。

#### `client/main.c`

实现：

- `--server`、`--port`、重复 `--path`、`--pattern`、`--sort`、`--progress`；
- 请求编码与部分写；
- 流式收帧；
- stall 和 wall-clock 两类看门狗；
- 默认 stdout 只包含匹配行和唯一统计行；
- progress/verbose 只写 stderr；
- `--sort` 同时受结果条数和字节数限制。

---

## 5. 三节点 P0 实现边界

#### `distributed.h` / `distributed.c`

输入为已发现的完整文件清单和三个可用节点；输出为每个文件所属节点。

静态 LPT 算法：

```text
按 file_size 从大到小排序
for 每个文件:
    选择 assigned_bytes 最小的节点
    分配文件
    assigned_bytes[node] += file_size
```

相同累计字节时按 `node_id` 小者优先，保证测试确定性。它只计算计划，不发送网络消息。

远程准入规则必须写入 `message_router` 测试：

- 未知查询且首个 `TASK_ASSIGN` 带 `REGISTER`：创建远端只读查询规格；
- 未知查询且未带 `REGISTER`：回复 `TASK_TERMINAL(ERROR, TS_E_CANCELED)`；
- `remote_pending` 已满：回复 `TS_E_BUSY`；
- 已登记查询收到不一致模式串：回复 `TS_E_BADPROTO`，发送完成后关闭连接；
- 每个接受的远程任务最终必须有一个终态；
- P0 收到错误或断连后直接结算失败，不自动重派；
- 一个查询在该节点的任务全部结算后，协调节点发送 `QUERY_RELEASE`。

---

## 6. 里程碑和提交顺序

### M0：接口冻结

组长应按以下顺序合并：

1. 目录重命名、Makefile、空的测试入口；
2. `common + byte_buffer`；
3. `protocol + protocol_body + protocol tests`；
4. `blocking_queue + queue tests`；
5. `task/result/query` 公共接口和所有权注释；
6. `discovery/search` 独立单测；
7. `thread_pool` 并发和关闭测试。

M0 出口条件：

- 所有公共头可被一个最小 C 文件单独包含并编译；
- 32 字节帧的黄金字节测试固定下来；
- 队列满、关闭、字节上限测试通过；
- `ts_search_task_execute` 不依赖网络模块；
- ASan/UBSan 单测通过；
- 公共接口由组长和对应调用方共同签字确认。

### M1：单机单查询闭环

固定事件流：

```text
QUERY_REQUEST
→ 创建 QueryContext
→ DISCOVER 进入查询待派发列表
→ try_push 到全局任务队列
→ Worker 返回 FileList 和发现终态
→ 主线程生成 SEARCH_FILE 任务
→ Worker 流式返回 MATCH_BATCH/PROGRESS/TERMINAL
→ 主线程更新统计并写发送缓冲
→ 唯一 QUERY_COMPLETE
```

M1 只允许一个活动查询，但必须使用最终线程池、队列和对象所有权，避免 M2 推倒重写。

### M2：单机多查询

加入查询表、公平轮询、动态配额、保留槽、取消、慢客户端和优雅退出。先测 2 个查询，再扩展到 10 个；不在这一阶段加入节点协议。

### M3：三节点

先在一台虚拟机启动三个端口不同的服务进程，再部署三台服务器。先测单远程任务，再测完整查询；最后注入断连和准入满。

### M4：数据与实验

数据生成器使用仓库内 POSIX Shell/C 工具。先生成小型确定性 fixtures，再生成稀疏或可控大文件压力集。100 GB/1 TB 实验必须记录实际分配空间、逻辑大小、manifest、缓存口径和命令。

---

## 7. 五人分工合同

| 角色 | 直接维护 | 不得单独修改 | 第一阶段交付 |
|---|---|---|---|
| 组长 | `query/scheduler/server`、公共接口、集成 | 已冻结协议字段 | 状态机、依赖图、合并门禁 |
| 成员1 网络 | `protocol/connection/message_router` | 查询计数语义 | 编解码、半包粘包、非阻塞收发 |
| 成员2 并发 | `blocking_queue/thread_pool` | 调度公平策略 | 双上限队列、关闭唤醒、线程池 |
| 成员3 检索 | `discovery/search` | Socket 和查询状态 | 文件发现、流式匹配、结果批次 |
| 成员4 测试 | `client/tests/scripts` | 服务端内部结构 | CLI、基准对照、10用户、manifest |

任何修改 `include/triseek` 的提交，必须列出：

1. 哪些调用方受影响；
2. 所有权是否改变；
3. 失败返回时谁释放对象；
4. 对应测试如何更新。

---

## 8. Makefile 必须提供的入口

```text
make debug             # -O0 -g3 -fno-omit-frame-pointer
make release           # -O2
make test              # 全部单元测试
make test-integration  # 端到端测试
make asan              # AddressSanitizer + UBSan
make tsan              # ThreadSanitizer，单独运行
make clean
```

公共编译参数：

```make
CPPFLAGS += -Iinclude -D_GNU_SOURCE -D_POSIX_C_SOURCE=200809L
CFLAGS   += -std=c11 -Wall -Wextra -Wpedantic -pthread
```

源码统一使用：

```c
#include <triseek/protocol.h>
```

不要通过 `-Iinclude/triseek` 继续保留大小写不一致的旧 include 写法。

---

## 9. 测试清单

### 9.1 协议

- 头部逐字节喂入；
- 头完整、body 不完整；
- 两帧粘在同一缓冲；
- 一帧之后保留下一帧前缀；
- 错误 magic/version/type/flags/header_len；
- body 等于 1 MiB 和超过 1 MiB；
- query/task 64 位网络字节序；
- parser 拒绝字段长度越界和多余尾部。

### 9.2 队列和线程池

- 空队列 `try_pop`；
- 条目数满；
- 字节数满；
- `try_push` 立即返回 `TS_E_BUSY`；
- 阻塞生产者被 pop 唤醒；
- 阻塞消费者被 push 唤醒；
- close 广播唤醒全部线程；
- close 后不接受新条目；
- destroy 释放残留对象；
- 多生产者/多消费者无丢失、无重复；
- TSan 无数据竞争。

### 9.3 发现与搜索

- 单文件、空目录、多层目录和 glob；
- 重复路径和硬链接只计一次；
- 符号链接环不跟随；
- 路径不存在、无权限和遍历中删除；
- LF、CRLF、孤立 CR、末行无 LF；
- NUL 字节；
- 模式跨读缓冲；
- 超长行后半段命中；
- 同行多次命中只计一行；
- 取消时仍产生一次 `DROPPED` 终态。

### 9.4 查询和调度

- 合法状态迁移；
- 重复终态不重复计数；
- 待派发任务取消后全部转为 `DROPPED`；
- 运行任务终态归还配额；
- 单查询不超过 quota；
- 两个和十个查询均有进展；
- 任务队列满时主线程不阻塞；
- `dispatch_budget` 生效；
- `workers=1` 时保留槽自动为0；
- 8个长任务时新查询发现仍能获得控制槽。

### 9.5 网络和集成

- 部分读、部分写、`EINTR/EAGAIN`；
- 发送缓冲清空后取消 `EPOLLOUT`；
- 慢客户端达到发送上限后只取消自身查询；
- 客户端断连不会泄漏查询；
- stdout 只有匹配和唯一统计行；
- progress 只写 stderr；
- 单机结果与 `grep -r -n --fixed-strings` 对照；
- 三进程结果与单机结果一致；
- 未 REGISTER、remote_pending 满、模式冲突、节点断连均有明确终态。

---

## 10. 组长每次合并前检查

```text
[ ] 这个提交属于 M0/M1/M2/M3/M4 中的哪一步？
[ ] 是否修改公共头？调用方和测试是否同步？
[ ] epoll 主线程是否可能等待锁、队列或文件 I/O？
[ ] Worker 是否接触了 Socket 或可变 QueryContext？
[ ] 新建对象在每个失败分支由谁释放？
[ ] 每个已提交任务是否最终只有一个终态？
[ ] 新缓冲是否同时有条目/字节或明确容量上限？
[ ] 当前代码证据是否足以把看板状态改成“已实现”？
[ ] make test 是否通过？
[ ] sanitizer 是否按模块风险运行？
```

---

## 11. 从当前骨架开始的第一个工作日

只完成以下最小闭环，不开始写线程池或三机：

1. 在虚拟机为当前代码建立可回退提交；
2. 把 `include/triSeek` 改成 `include/triseek`，同步修改 include；
3. 建立目标目录，但不创建无意义空文件；
4. 建立 Makefile 的 `debug/test` 两个入口；
5. 编写一个“每个公共头都能独立 include”的编译测试；
6. 对现有 `common/byte_buffer/protocol` 做迁移审查；
7. 冻结 32 字节头、消息生命周期和黄金字节测试；
8. 只有协议测试通过后，才开始队列和搜索模块。

现有五个骨架的处理原则：

| 当前文件 | 处理 | 审查重点 |
|---|---|---|
| `common.h/.c` | 保留并收窄 | 是否混入业务状态；错误码是否与v1.5一致 |
| `byte_buffer.h/.c` | 保留并补上限 | 扩容溢出、消费/压缩、最大容量 |
| `protocol.h` | 按32字节协议冻结 | 是否仍为旧头；body所有权；半包返回值 |
| `client/main.c` | 先保留骨架 | 参数与输出合同，不提前实现排序 |
| `server/main.c` | 只留启动入口 | 业务逻辑逐步下沉到 `server.c` |

完成这一天后，项目仍可能没有端到端搜索能力，但目录、协议和团队协作边界已经稳定，其他成员可以在不互相覆盖代码的前提下开始 M0。

---

## 12. 实施状态看板初值

在未读取虚拟机源码、未运行测试前统一填写：

| 模块 | 优先级 | 当前状态 | 变为已实现所需证据 |
|---|---|---|---|
| 工程目录与Makefile | P0 | 待核验 | 虚拟机可重复构建 |
| 32字节协议编解码 | P0 | 待核验 | 黄金字节、半包、粘包测试 |
| 双上限队列与线程池 | P0 | 待实现 | 并发、满队列、关闭测试 |
| 目录发现和整文件搜索 | P0 | 待实现 | 与基准结果一致 |
| 单查询网络闭环 | P0 | 待实现 | M1端到端测试 |
| 多查询公平调度 | P0 | 待实现 | 10用户和配额断言 |
| 三节点整文件协同 | P0 | 待实现 | 三进程/三机一致性与断连测试 |
| POSIX数据与实验脚本 | P0 | 待实现 | 可复现命令、manifest和日志 |
| 大文件分块 | P1 | 预留 | 不进入P0 |
| 心跳、重试和重派 | P2 | 预留 | 不进入P0 |

这张表只能由“代码 + 测试 + 运行证据”推动，不能因为设计文档写过就改成“已实现”。
