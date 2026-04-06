# Flower

Flower 是一个面向大文件的字符串索引与搜索工具。它先为目标文件构建磁盘索引，再基于索引做并发查找，从而避免每次查询都全量扫描原文件。

这个项目当前对外接口保持不变，仍然以 `BuildDstIndex(...)` 和 `SearchContext` 作为主要使用入口。

## 项目能做什么

- 为超大文本文件构建离线索引
- 查询字符串在文件中的字节偏移
- 可选返回命中的起止行号、起止列号
- 支持跨行匹配，不要求搜索串必须落在单行内
- 构建和查询都支持多线程

## 一分钟理解

如果只想先抓住这个项目的目的，可以把 Flower 理解成两层索引：

- 第一层是主索引 `.idx`，负责回答“某个字符串出现在哪些字节偏移”
- 第二层是行号索引 `.kvi`，负责回答“这些字节偏移落在哪一行、哪一列”

整个项目的目标不是替代一次性全文扫描，而是让“同一个大文件被反复查询”这件事变快。

对外只有两个核心动作：

1. `BuildDstIndex(...)` 先把原文件转成索引文件
2. `SearchContext::search(...)` 再利用索引查询结果

所以你可以把 Flower 看成一个“大文件离线建索引 + 在线并发查询”的引擎。

## 执行链路

最核心的执行链路如下：

```text
原文件
  -> BuildDstIndex(...)
  -> 生成 .idx 主索引
  -> 可选生成 .kvi 行号索引
  -> SearchContext::init(...)
  -> SearchContext::search(...)
  -> 返回字节偏移
  -> 按需再映射为行号/列号
```

如果再压缩成一句话，就是：

- 构建阶段解决“怎么把原文件组织成适合查询的索引结构”
- 查询阶段解决“怎么用索引尽量少碰原文件就找到所有命中”

## 原理图

### 1. 整体架构图

```text
                         构建阶段

                +----------------------+
                |      原始文件        |
                |     bigfile.txt      |
                +----------+-----------+
                           |
                           | BuildDstIndex(...)
                           v
         +-----------------+------------------+
         |                                    |
         v                                    v
 +---------------+                    +---------------+
 |  主索引 .idx  |                    | 行号索引 .kvi |
 | 字符串 -> 偏移|                    | 偏移 -> 行号  |
 +-------+-------+                    +-------+-------+
         |                                    |
         +-----------------+------------------+
                           |
                           v
                         查询阶段
                           |
                           | SearchContext::search(...)
                           v
              +------------+-------------+
              |                          |
              v                          v
      字节偏移结果集合             行号/列号结果映射
```

这张图可以直接对应项目的设计目标：

- `.idx` 解决“找到字符串出现在哪里”
- `.kvi` 解决“把位置翻译成第几行第几列”
- 两层职责拆开后，主搜索路径更轻，返回行列也更灵活

### 2. 主索引的抽象结构图

可以把主索引理解成一棵“压缩前缀树”，每条路径代表一批拥有共同前缀的后缀。

```text
原文件中的多个起点：

pos=100  abcdefghijkl...
pos=240  abcdxyz.....
pos=801  abcmnop.....

压缩后大致变成：

               [公共前缀: abc]
                  /      \
                 /        \
          [公共前缀: d]   [公共前缀: m]
             /     \            \
            /       \            \
      [后续: ef..] [后续: xy..]  [后续: no..]
         |             |            |
      叶子:100      叶子:240     叶子:801
```

这里要注意两点：

- 节点里保存的是“公共前缀来自原文件哪一段”，不是复制整段字符串
- 叶子里保存的是“候选起始偏移”，不是完整文本内容

这就是它比直接保存“字符串到位置映射表”更省空间的原因。

### 3. 查询路径图

查询时不是扫描整个文件，而是沿着索引树只走可能命中的路径。

```text
搜索串: abcdxy

                根节点
                   |
          比较公共前缀 / 选择分支
                   |
            [前缀: abc]   <- 保留
                   |
             [前缀: d]    <- 保留
                   |
           [后续分支: xy] <- 命中
                   |
              候选叶子位置
                   |
          回原文件做最终字节校验
                   |
                输出偏移
```

也就是说，Flower 的搜索过程本质上是：

1. 先在索引树里做大范围剪枝
2. 再对少量候选位置回原文件验证
3. 最后按需再查 `.kvi` 把偏移转成行列

### 4. `.kvi` 行号定位图

`.kvi` 不是保存“每个字节属于哪一行”，而是保存“每一行从哪个字节开始”。

例如原文件是：

```text
line0: hello
line1: world
line2: flower
```

把换行也算进字节流后，可以抽象成：

```text
字节偏移:

0         6         12
|---------|---------|----------->
hello\n   world\n   flower

行起始偏移:

line 0 -> 0
line 1 -> 6
line 2 -> 12
```

当主索引返回一个命中起点 `filePos = 8` 时，`.kvi` 不需要精确存 `8`，只需要找到它落在哪两个行起始偏移之间：

```text
已知行起始:

0 ---- 6 ---- 12
        ^
        |
     filePos=8

lowerBound = 6
upperBound = 12

因此可知：

8 位于 line 1
column = 8 - 6 = 2
```

如果匹配串长度会跨行，例如：

```text
startPos = 8
targetLen = 7
endPos = 14
```

那么再对 `endPos` 做一次同样的区间查询：

```text
0 ---- 6 ---- 12 ---- ...
                  ^
                  |
               endPos=14

endLowerBound = 12

因此可知：

endLine = 2
endColumn = 14 - 12 = 2
```

于是一个跨行命中就能被表示成：

```text
start: line 1, column 2
end:   line 2, column 2
```

这就是 `.kvi` 的核心用途：

- 主索引负责找命中起点
- `.kvi` 负责把起点和终点翻译成行列范围

## 对外接口

### 1. 构建索引

需要包含头文件：

```cpp
#include "interface.h"
```

调用接口：

```cpp
bool BuildDstIndex(const char* fileName, bool needBuildLineIndex = false, char delimiter = '\n');
```

参数说明：

- `fileName`：目标文件路径
- `needBuildLineIndex`：是否额外构建行号索引。如果只关心字节偏移，可以传 `false`
- `delimiter`：行分隔符，默认是 `'\n'`

返回值：

- 成功返回 `true`
- 失败返回 `false`

构建完成后，默认会在原文件旁边生成：

- `原文件名.idx`：主字符串索引
- `原文件名.kvi`：行号 KV 索引，仅在 `needBuildLineIndex=true` 时生成

### 2. 初始化搜索上下文

```cpp
SearchContext searchContext;
bool ok = searchContext.init(fileName, threadNum, searchLine);
```

接口定义：

```cpp
bool SearchContext::init(const char* fileName, unsigned long threadNum = 0, bool searchLine = false);
```

参数说明：

- `fileName`：目标文件路径
- `threadNum`：搜索时使用的线程数。传 `0` 时默认取逻辑 CPU 数
- `searchLine`：是否需要返回行列号。若为 `true`，要求此前已经构建 `.kvi` 行索引

### 3. 仅返回字节偏移

```cpp
std::set<unsigned long long> result;
bool ok = searchContext.search(target, targetLen, &result);
```

接口定义：

```cpp
bool SearchContext::search(const char* searchTarget, unsigned int targetLen, std::set<unsigned long long>* set);
```

结果说明：

- `set` 中保存所有命中的起始字节偏移
- 偏移从 `0` 开始计数

### 4. 返回字节偏移 + 行列范围

```cpp
ResultMap result;
bool ok = searchContext.search(target, targetLen, &result);
```

接口定义：

```cpp
bool SearchContext::search(const char* searchTarget, unsigned int targetLen, ResultMap* map);
```

结果说明：

- `ResultMap` 的 key 是命中的起始字节偏移，从 `0` 开始
- value 为 `LineAndColumn`
- 可通过以下接口读取结果：

```cpp
GetLineNum()
GetColumnNum()
GetEndLineNum()
GetEndColumnNum()
```

它们分别表示：

- 命中起始行号
- 命中起始列号
- 命中结束行号
- 命中结束列号

以上编号都从 `0` 开始。

### 5. 使用约束

- 同一个 `SearchContext` 可以重复调用多次 `search`
- 搜索结束后销毁 `SearchContext` 即可
- 不要在同一个目标文件上同时进行“构建索引”和“查询索引”

## 快速示例

示例程序在 `Flower/main.cpp` 中。它演示了：

- 构建普通索引
- 构建行号索引
- 单行匹配
- 跨两行匹配
- 跨多行匹配

典型使用方式：

```cpp
#include "interface.h"
#include <set>

int main()
{
	const char* fileName = "example.txt";

	if (!BuildDstIndex(fileName, true, '\n'))
	{
		return 1;
	}

	SearchContext ctx;
	if (!ctx.init(fileName, 0, true))
	{
		return 1;
	}

	ResultMap result;
	const char* target = "hello";
	if (!ctx.search(target, 5, &result))
	{
		return 1;
	}

	return 0;
}
```

## 构建方式

### Bazel

在仓库根目录执行：

```bash
bazel build //Flower:flower
bazel run //Flower:flower
```

说明：

- `bazel run` 会运行 `Flower/main.cpp` 里的示例与当前回归测试逻辑
- 当前 Bazel 配置依赖 `tcmalloc`，定义见 `MODULE.bazel`

### CMake

项目现在也支持使用 CMake 构建。

在仓库根目录执行：

```bash
cmake -S . -B build
cmake --build build -j
```

可执行文件默认输出到：

```bash
build/Flower/flower
```

如果机器安装了 `tcmalloc`，CMake 会自动链接；未安装时会给出提示，但仍可继续构建。

### Make

仓库根目录也保留了传统构建入口：

```bash
make
```

## 底层原理

这一部分是项目的核心。Flower 不是简单地保存一份“字符串到位置”的哈希表，而是把目标文件本身视为一组后缀起点，然后构造成一棵压缩索引树，再通过树上的共享前缀快速裁剪搜索空间。

### 1. 主索引的基本思想

对文件中的任意起始位置 `pos`，都可以看成一个“从 `pos` 开始直到文件末尾”的后缀串。Flower 的主索引，本质上是在管理这些后缀串的公共前缀关系。

它不是把整个后缀完整复制一遍，而是：

- 节点只记录“这一段公共前缀来自原文件的哪一段”
- 叶子只记录“某个匹配起点的文件偏移”
- 真正需要验证剩余内容时，再回到原文件读取

这样做的直接收益是：

- 避免复制大块文本
- 索引文件更小
- 搜索时可以先按前缀快速剪枝，只对少量候选做原文校验

### 2. 节点结构

核心节点类型定义在 `Flower/IndexNode.h`。每个节点包含几类关键信息：

- `start`：这个节点对应的公共前缀在原文件中的起始位置
- `len`：当前节点代表的公共前缀长度
- `preCmpLen`：到达本节点前，已经匹配过多少字节
- `leafSet`：能够直接落到文件尾部或可直接导出结果的一组叶子起点
- `children`：按照下一段 key 分叉的子节点

主索引节点有四种类型：

- `NODE_TYPE_ONE`：按 8 字节分叉
- `NODE_TYPE_TWO`：按 4 字节分叉
- `NODE_TYPE_THREE`：按 2 字节分叉
- `NODE_TYPE_FOUR`：按 1 字节分叉

这四种节点类型不是四套不同算法，而是同一棵压缩树在不同分叉密度下的表示形式。

设计原因是：

- 大步长比较可以减少树高
- 小步长比较可以降低节点膨胀
- 当节点过大时，系统会自动把节点降级为更细粒度的类型

### 3. 为什么要有 8/4/2/1 四种分叉宽度

如果所有节点都固定按 8 字节做 key，树会比较浅，但某些分支较多的节点会非常大，导致：

- 内存占用高
- 序列化节点成本高
- 磁盘写入和读取代价高

因此构建阶段会做节点裁剪：

- 优先使用大 key 宽度提高比较速度
- 当节点超过阈值时，尝试降级为 4 字节、2 字节、1 字节节点
- 如果仍然过大，就把节点再切开，提升一层父节点，把一部分公共前缀上提

对应逻辑主要在 `BuildIndex::cutNodeSize()` 中。

这相当于在“查询深度”和“节点大小”之间做动态平衡。

### 4. 主索引如何构建

主索引构建入口是：

```cpp
BuildDstIndex(fileName, needBuildLineIndex, delimiter)
```

核心路径在 `Flower/interface.cpp` 和 `Flower/BuildIndex.cpp`。

构建过程可以概括为以下几步：

1. 把目标文件按固定大小切分为多个段
2. 每段独立构建一棵局部根节点
3. 节点记录公共前缀，不复制完整后缀文本
4. 叶子记录命中起点或下级节点引用
5. 最后把所有根节点写入 `.idx`

当前默认分段大小在 `Flower/common.h` 中定义：

```cpp
const unsigned int DST_SIZE_PER_ROOT = 8 * 1024 * 1024;
```

也就是每 `8MB` 左右文件内容对应一个根分段。这么设计有两个作用：

- 构建时便于并行化
- 查询时可以把不同根分配给不同线程组

### 5. 索引文件 `.idx` 存的是什么

`.idx` 不是文本索引列表，而是序列化后的索引节点集合，外加根节点列表。

从搜索代码可以看到，查询阶段首先会从 `.idx` 文件头读取根节点数量，然后按根节点顺序分配搜索任务。

因此 `.idx` 主要承载两类信息：

- 根节点元数据
- 各个压缩索引节点的二进制内容

运行时通过 `IndexFile` 负责：

- 节点读写
- 缓存
- 根节点 ID 管理
- 节点落盘与回收

### 6. 为什么查询可以并发

查询入口是 `SearchContext::search(...)`。

它的并发策略分两层：

- 第一层：把多个根节点区间分给多个 helper 线程
- 第二层：每个 helper 内再固定启动 8 个搜索线程，分别处理不同的首字节跳过策略

具体实现位于 `Flower/SearchContext.cpp`。

其中一个关键点是 `skipCharNum`。搜索线程并不是所有人都从搜索串第 0 个字节开始做完全相同的工作，而是按不同偏移切开，减少重复路径展开，并让不同线程命中不同的索引分支。

### 7. 查询是如何命中的

真正的索引遍历在 `Flower/SearchIndex.cpp` 中。

它的工作方式不是“把目标串和每个叶子逐个比较”，而是：

- 先根据当前节点已压缩的公共前缀决定下一步还能跳过多少比较
- 再根据节点类型，用 8/4/2/1 字节 key 找到可能匹配的子分支
- 只把仍然可能命中的路径继续压入任务队列
- 到叶子或候选位置时，再用原文件做最终校验

最终命中的字节偏移通过 `AddFindPos(...)` 写入结果集合。

这也是 Flower 性能的核心来源：

- 大量不可能命中的路径在树上就被剪掉了
- 只有少数候选需要真正回源文件比较

### 8. 为什么支持跨行匹配

Flower 的主索引是基于“字节流”构建的，而不是基于“单行文本”构建的。也就是说，在主索引眼里，整个文件就是一串连续字节。

所以搜索串里即使包含跨越换行的位置，只要字节序列连续存在，主索引就能命中。

这也是 `main.cpp` 中能验证：

- 单行匹配
- 跨两行匹配
- 跨多行匹配

的原因。

### 9. 行号和列号为什么要单独建 `.kvi`

按字节偏移搜索和按行列返回结果，本质上是两类问题。

主索引擅长回答：

- 某个字符串出现在哪些字节偏移

但它不直接回答：

- 这个偏移属于第几行第几列

如果每个主索引叶子都附带完整行号信息，会明显增加索引体积，也会让构建和更新逻辑更复杂。因此项目把“偏移 -> 行号区间”的映射单独做成了 KV 索引文件 `.kvi`。

### 10. `.kvi` 的本质是什么

`.kvi` 记录的是一系列行起始偏移到行号的映射。可以理解为：

- key：某一行的起始字节偏移
- value：该行的行号

查询某个命中位置 `filePos` 时，并不是要求 `.kvi` 里恰好有 `filePos` 这个 key，而是要找到：

- 不大于 `filePos` 的最大行起始偏移 `lowerBound`
- 大于 `filePos` 的最小下一行起始偏移 `upperBound`

于是就能得到：

- `startLine = value`
- `startColumn = filePos - lowerBound`

如果命中串末尾还在同一行，就直接算结束列；
如果已经跨行，就再对命中结束位置做一次同样的 KV 查询。

对应逻辑在 `Flower/KVContent.cpp` 和 `Flower/SearchContext.cpp`。

### 11. KV 行索引为什么也做成树，而不是直接数组

如果只是处理普通文本文件，确实也可以维护一个“每行起始偏移数组”。但当前实现选择把 KV 行索引复用同一套索引节点体系，原因主要有三点：

- 可以复用节点序列化、缓存、落盘和并发构建能力
- 对超大文件时不必把整张行号表完整常驻内存
- 查询时可以沿着树找上下界，避免一次性加载整个行号数组

从 `KVContent::get(...)` 可以看到，它做的是带上下界追踪的树查找，而不是简单的哈希命中。

### 12. 多线程构建是怎么做的

`BuildDstIndex(...)` 在文件较大时会进入多线程构建路径。

主要分两部分：

- 主索引按文件段并行构建多个 root
- 行号索引在需要时先并行统计各段产生的行起点数，再并行构建 KV 段，最后归并

这里的一个关键基础设施是 `UniqueGenerator`：

- 多个构建线程共享同一套节点 ID 生成器
- 保证不同线程创建的节点 ID 不冲突

这样所有线程各自写自己的节点，但最终仍能落到同一个索引文件体系中。

### 13. 缓存和内存管理

索引节点不会每次都直接从磁盘重新构造。运行时有两层重要的内存管理机制。

第一层是 `Index` 的节点缓存：

- 维护 `indexNodeCache`
- 用 `preCmpLen` 做优先级
- 在内存不足时优先回收“后续收益较低”的节点

第二层是 `MemoryPool`：

- 为四类 `IndexNode` 分别维护对象池
- 降低频繁 `new/delete` 的开销
- 减少碎片
- 提高缓存局部性

当系统内存紧张时，代码会根据内存水位做：

- 局部缓存回收
- 紧急清空缓存
- 必要时清理内存池

相关阈值定义在 `Flower/common.h`，补充说明见 `Flower/MEMORY_POOL_README.md`。

### 14. 为什么接口简单但内部复杂

从使用者角度看，Flower 只有两个关键动作：

1. `BuildDstIndex(...)`
2. `SearchContext::search(...)`

但为了让这两个接口在大文件场景下仍然可用，内部实际上做了这些事：

- 分段构建
- 压缩前缀树
- 动态节点降级
- 磁盘节点序列化
- 多线程搜索
- 行号区间索引
- 节点缓存
- 内存池管理

也就是说，外部接口故意保持简单，复杂度被集中吸收到索引构建和索引文件格式里了。

## 适用场景

这个项目更适合以下场景：

- 日志文件离线检索
- 超大文本文件的重复模式查找
- 需要大量重复查询同一个文件
- 既要字节偏移，又要行列范围定位

如果只是一次性搜索一个很小的文件，直接顺序扫描可能更简单；Flower 的优势主要体现在“文件大”和“重复查询多”。

## 当前限制

- 默认更适合文本型数据，尤其是存在明确行分隔符的文件
- 若要返回行列号，必须在构建时开启 `needBuildLineIndex`
- 不建议在构建同一文件索引的同时并发查询该文件
- 目前没有独立单元测试框架，回归样例主要在 `Flower/main.cpp`

## 建议的验证方式

修改后可用以下方式验证：

```bash
bazel run //Flower:flower
```

如果还想兼容检查传统编译链，可以再执行：

```bash
make
```

## 源码导读

如果你准备继续维护这个项目，建议按下面顺序阅读：

- `Flower/interface.h` / `Flower/interface.cpp`
  外部 API 与总入口
- `Flower/BuildIndex.h` / `Flower/BuildIndex.cpp`
  主索引与 KV 索引构建
- `Flower/SearchContext.h` / `Flower/SearchContext.cpp`
  查询调度、多线程组织、结果整合
- `Flower/SearchIndex.h` / `Flower/SearchIndex.cpp`
  主索引遍历与候选校验
- `Flower/KVContent.h` / `Flower/KVContent.cpp`
  字节偏移到行列范围的定位
- `Flower/IndexNode.h`
  节点类型与索引树结构
- `Flower/IndexFile.h`
  索引文件读写与根节点管理
- `Flower/Index.h` / `Flower/MemoryPool.*`
  缓存、淘汰、内存池

## 总结

Flower 可以把“大文件字符串搜索”拆成两层问题：

- 主索引解决“这个字节串在哪些偏移出现”
- KV 行索引解决“这些偏移对应哪几行哪几列”

对外接口保持简单不变，但内部通过压缩前缀树、磁盘索引、分段并行和内存池，把原本接近全量扫描的问题变成了可复用、可并发、可定位的索引查询问题。
