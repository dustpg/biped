# Biped 2D Cache Algorithm

## 1. 概述

目前已有天际线(Skyline)等 2D pack 类算法负责**空间管理**, 也有 S3 FIFO 等负责**时间管理**(如缓存淘汰), 但少有同时兼顾空间与时间的方案. 这里即针对这一缺口:在 2D Atlas 上既做图块摆放与回收, 又按"冷热"做淘汰与复用.

本算法受 Buddy(伙伴)算法启发, 支持节点的分裂与合并, 用于快速管理纹理缓存空间.

**特点**: 基于 2 幂次尺寸的 Atlas 纹理, 通过 Size Class Level 将图块分级, 结合 MAP/BASE/LEVEL 等结构实现分配与回收.

**局限**: 空间利用率相对不高.

---

## 2. 应用场景与假设

### 2.1 Atlas 纹理

- 使用**固定 2 幂次大小**的纹理.
- **预设场景**: 在 2048x2048, RGBA 的 Atlas 上, 存放 8x16 至 64x64 的文本, 节点数量为数千量级. 下文中时间复杂度以节点数量 \(N\) 为单位.
- RGB 通道存 Msdf 或次像素信息, A 通道存灰度信息;  因此设计上可用两个 Biped 缓存共同管理一个 RGBA Atlas.

---

## 3. 概念与记号

### 3.1 Size Class Level

Level 表按尺寸从 `[1x1][1x2][2x2][2x4]` 直至 `[SxS]` 组织:

- 面积按 2 为底的幂数表示, 其**指数**作为 LEVEL 值.
- 图块申请尺寸 \((W, H)\) 会**向上取整**到"向上的最近 2 整数次幂"(如 7x8 -> 8x8, 16x4 -> 16x16).
- 节点可**分裂**为左足, 右足节点; 同一"双腿"的左右足节点可**合并**回一个节点. (Biped 双足, 那就表述中用"足"代替"叶".)

### 3.2 Size -> Level

- `rup` = round up to power of 2
- \(w = \log_2(\text{rup}(\text{width}))\)
- \(h = \log_2(\text{rup}(\text{height}))\)
- \(a = 1\) if \(h > w\), else \(a = 0\)
- \(b = \max(w, \, h-1)\)
- \(\text{level} = b \times 2 + a\)

### 3.3 Level -> Size

- \(\text{width} = 2^{\lfloor \text{level} / 2 \rfloor}\)
- \(\text{height} = \text{width} \times (\text{level} \bmod 2 + 1)\)

### 3.4 核心数据结构(Core)

- **MAP 表**: 键值对 \([K, \, \text{NODE}]\), 用于根据 key 找到节点.
- **BASE 表**: 需支持节点的添加, 驱逐以及"冷节点"查询, 具体策略由底层 cache 算法决定(如 LRU 可用链表实现).
- **LEVEL 表**: 与 BASE 类似, 但按 LEVEL 分区;  最简实现为长度为 MAX_LEVEL 的数组, 每个元素为一个 BASE 表.

---

## 4. 算法流程概览

### 4.1 初始化(A. 初始化)

- 初始化 MAP.
- 将顶层 \(S \times S\) 节点加入 BASE 表, 并按其 level值 加入对应 LEVEL 表.

### 4.2 查找对象(B. 查找对象)

- 在 MAP 中按 key \(K\) 查找节点.
- 若找到, 则将该节点标记为最近使用:`MARK(NODE)`.
- 若未找到, 则返回"未找到".

### 4.3 插入对象(C. 插入对象)

- 约定: 待插入对象当前不在任何表中.
- 根据插入对象尺寸计算 LEVEL 值 \(lv\).
- 依次尝试行为 **A**, **B**, **C**, 任一成功即完成插入.
- 若三者均未成功, 则插入失败.

---

## 5. 子算法与伪代码

### 5.1 Behavior A(Attempt)- 直接复用同 Level 冷节点

- **时间复杂度**: \(O(1)\).
- **思路**: 取 BASE 中最冷节点, 若其 LEVEL 与需求 \(lv\) 一致, 则驱逐该节点并将新节点放在其位置;  否则失败.

```
ALGORITHM A(key, width, height, lv)
  coldest_node <- GetColdestNode(BASE)

  IF coldest_node = NULL THEN
    RETURN FAILURE
  END IF

  IF GetLevel(coldest_node) != lv THEN
    RETURN FAILURE
  END IF

  evict_key <- FindKeyInMap(coldest_node)
  IF evict_key != NULL THEN
    DELETE MAP[evict_key]
  END IF
  
  REMOVE coldest_node FROM BASE
  node_level <- GetLevel(coldest_node)
  REMOVE coldest_node FROM LEVEL[node_level]

  new_node <- CreateNode(key, width, height, lv)
  // XY position
  new_node.position <- coldest_node.position

  MAP[key] <- new_node

  INSERT new_node INTO BASE
  INSERT new_node INTO LEVEL[lv]

  RETURN SUCCESS
```

### 5.2 Behavior B(Browse)- 从高 Level 找足够冷的节点再分裂

- **时间复杂度**: 约 \(O(\log N)\).
- **思路**: 在 LEVEL 表中, 从需求 \(lv\) 到 MAX_LEVEL 各层取最冷节点, 得到"最冷"的一个; 若其足够冷, 则进入 **D**(向下分裂)得到目标 \(lv\) 的节点;  否则失败.

```
ALGORITHM B(key, width, height, lv)
  max_coldest = 0
  coldest_node = NULL
  
  FOR current_level <- level TO MAX_LEVEL DO
    temp_node <- GetColdestNode(LEVEL[current_level])
    
    IF temp_node != NULL THEN
      IF temp_node.cold > max_coldest THEN
        max_coldest <- temp_node.cold
        coldest_node <- temp_node
      END IF
    END IF
  END FOR

  IF coldest_node = NULL THEN
    RETURN FAILURE
  END IF

  IF NOT IsColdEnough(coldest_node) THEN
    RETURN FAILURE
  END IF
  
  RETURN D(key, width, height, lv, coldest_node)
```

### 5.3 Behavior C(Combine)- 合并同区域小块

- **时间复杂度**:约 \(O(N)\)(在单次驱逐为 \(O(1)\) 的前提下); 若单次驱逐为 \(O(N)\), 则总体约 \(O(N \log N)\), 其中 \(K\) 为驱逐节点数, 约 \(\log N\).更精确为 \(O(2N + K \cdot \text{Evict})\).
- **思路**:
  - 维护临时表 \(M[\ ]\), 大小为 \((W/w) \times (H/h)\), 表示目标尺寸下各区域的面积累加.
  - 按从冷到热遍历 BASE, 按节点所在区域累加面积; 当某区域面积 \(\ge\) 目标面积时, 该区域可合并, 记录位置与 index.
  - 若累加面积**大于**目标面积, 说明存在超过目标 level 的节点, 可与行为 B 类似执行 **D** 操作.
  - 若**等于**目标面积, 则按 index 驱逐该区域内节点, 用 index 重建一个新节点.
- **关系**:Biped 中面积为 \(1 \ll \text{level}\), 且 Biped 面积 \(\ge\) 实际需求面积.

```
ALGORITHM C(key, width, height, lv)
  M[MAX_WIDTH / width * MAX_HEIGHT / height] <- {0} 
  area <- 1 << lv 
  index_target <- -1
  last_node <- NULL
  FOR cold_node IN BASE  // COLD -> WRAM
    index_m <- GetIndexM(cold_node, width, height)
    IF GetLevel(cold_node) >= lv THEN
      RETURN D(key, width, height, lv, cold_node)
    ELSE
      node_area <- 1 << GetLevel(cold_node)
      M[index_m] <- M[index_m] + node_area
      IF M[index_m] = area THEN
        index_target <- index_m
        last_node <- cold_node
        BREAK
      END IF
    END IF
  END FOR

  IF index_target = -1 THEN
    RETURN FAILURE
  END IF

  new_position <- CalculatePositionFromIndex(index_target, lv)
  
  evict_list = []
  FOR cold_node IN BASE 
    index_m <- GetIndexM(cold_node, lv)
    IF index_m = index_target THEN
      evict_key <- FindKeyInMap(cold_node)
      IF evict_key != NULL THEN
        DELETE MAP[evict_key]
      END IF
      node_level <- GetLevel(cold_node)
      REMOVE cold_node FROM LEVEL[node_level]
      APPEND cold_node TO evict_list
      IF cold_node = last_node THEN
        BREAK
      END IF
    END IF
  END FOR

  // Remove all items in evict_list from BASE
  BASE <- BASE - evict_list

  new_node <- CreateNode(key, width, height, lv)
  new_node.position <- new_position
  
  MAP[key] <- new_node
  INSERT new_node INTO BASE
  INSERT new_node INTO LEVEL[lv]

  RETURN SUCCESS
```

**实现优化(面积累加)**:可按"比例"用整数近似面积, 减少内存与运算.例如:LEVEL 与目标相差 <  8 用 8 bit 无符号整数, <  16 用 16 bit, 以此类推; 若等级差为 \(N\), 可视为满值右移 \(N\) 位(如 8 bit, 等级差 2 时用 \(256 \gg 2 = 64\)), 溢出回 0 即表示达到目标面积, 便于判定且提高访问效率.

### 5.4 辅助:Index in M

\(M\) 的 index 表示"目标尺寸下的一块区域"的一维编号.例如总面积 4x4, 需要 2x4 区域时:`MAX_WIDTH/2 = 2`, `MAX_HEIGHT/4 = 1`, \(M\) 长度为 2; 左侧区域 index 0, 右侧 index 1.可由二维 \((x, y)\) 按类似像素排列方式映射为一维 index.`CalculatePositionFromIndex` 为其逆运算, 此处略.

```
ALGORITHM GetIndexM(node, level)
  x_shift <- level >> 1
  y_shift <- (level + 1) >> 1
  diff <- MAX_LEVEL - level
  width_power <- (diff + 1) >> 1
  x <- node.position.x >> x_shift
  y <- node.position.y >> y_shift
  RETURN x | (y << width_power)
```

### 5.5 Operation D(Down split)- 向下分裂

通过反复分裂得到目标 level 的节点; 核心是计算子节点位置偏移.

**规则**:分裂后新节点的偏移量为"分裂后的水平边长"(即分裂后的 width); 根据当前 level 的奇偶性决定偏移施加在 x 轴还是 y 轴.例如:

- 1024x1024(lv=20)-> 两个 512x1024(lv=19), 新节点偏移 (512, 0); 
- 512x1024 -> 两个 512x512, 新节点偏移 (0, 512).

即:**分裂后的** width 为偏移量, 再按 level 奇偶性选择 x 或 y.

```
ALGORITHM SPLIT(node)
  node.level <- node.level - 1
  // COPY 
  new_node <- node
  offset <- 1 << (node.level / 2)
  if node.level & 1 THEN
    new_node.position.x <- node.position.x + offset; 
  ELSE
    new_node.position.y <- node.position.y + offset; 
  END IF
  RETURN new_node
```

Operation D 的完整流程为:对节点持续调用分裂, 直到其 level 与需求 \(lv\) 一致; 完整伪代码此处略.

---

## 6. 性能测试

测试环境: MSVC, `free(malloc(211))` 小对象申请操作. 以下为典型路径的耗时与相对速度.

| 场景 | 调用 | TIME | SPEED |
|------|---------|------|-------|
| **1. Cache Hit** | lock_key + unlock | 0.8 | 1.2 |
| **2. Miss (A)** 直接复用冷节点 | lock_key + lock_key_value + unlock | 1.23 | 0.8 |
| **3. Miss (B)** 高 Level 分裂 | lock_key_value + unlock | 1.9 | 0.52 |
| **4. Miss (失败)** 无可复用空间 | lock_key_value | 72 | 0.012 |

- 总体来看: 可以视为malloc/free相同代价的操作. 
- 但是不存在外部碎片
- 内部碎片则视数据而定(只用32/64 Msdf将不会有碎片)
- **Cache Hit**: 仅查 MAP 并 MARK, 开销最小.
- **Miss (A)**: 同 Level 冷节点驱逐与复用, 略慢于 Hit.
- **Miss (B)**: 跨 Level 取最冷节点再分裂, 涉及 LEVEL 扫描与分裂.
- **Miss (失败)**: 需尝试 A/B/C 均失败后的路径, 耗时显著升高.
- **Miss (C 成功)**: 理论上由于会遍历两次 C路径成功最差情况耗时将翻倍, 但是条件过于苛刻, 很难构造测试数据


## 7. 线段树
可以通过维护一颗线段树, 可以将最耗时的**行为C**的时间复杂度降低到约O(logN), 但是维护的线段树代价较高, 同时对lock操作不太友好. 不过有时间的话还是可以试试, 纸上谈兵不如进行实际对比. 
