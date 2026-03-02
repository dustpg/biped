# Biped 2D Cache Algorithm

## 1. Overview

While algorithms like **Skyline** handle 2D packing (space management) and **S3-FIFO** handle cache eviction (time management), few solutions address both simultaneously. **Biped** fills this gap by managing tile placement, recycling, and eviction based on "hot/cold" access patterns within a 2D Atlas.

Inspired by the **Buddy algorithm**, Biped supports node splitting and merging for fast texture cache space management.

**Features:** Power-of-2 sized Atlas textures, Size Class Level system for tile classification, efficient allocation and recycling via MAP/BASE/LEVEL structures.

**Limitations:** Relatively lower space utilization.

---

## 2. Use Cases and Assumptions

### 2.1 Atlas Texture

- Uses **fixed power-of-2 sized** textures.
- **Target scenario**: 2048×2048 RGBA Atlas storing text glyphs ranging from 8×16 to 64×64, with thousands of nodes. Time complexity is expressed in terms of node count \(N\).
- RGB channels store MSDF or subpixel data; A channel stores grayscale. Two Biped caches can jointly manage a single RGBA Atlas.

---

## 3. Concepts and Notation

### 3.1 Size Class Level

The Level table is organized by size from `[1x1][1x2][2x2][2x4]` up to `[SxS]`:

- Area is expressed as a power of 2; the **exponent** serves as the LEVEL value.
- Requested tile size \((W, H)\) is **rounded up** to the nearest power of 2 (e.g., 7×8 → 8×8, 16×4 → 16×16).
- Nodes can **split** into left-foot and right-foot nodes; left and right foot nodes of the same "pair of legs" can **merge** back into one node. (Biped = two feet, so we use "foot" instead of "leaf" in terminology.)

### 3.2 Size → Level

- `rup` = round up to power of 2
- \(w = \log_2(\text{rup}(\text{width}))\)
- \(h = \log_2(\text{rup}(\text{height}))\)
- \(a = 1\) if \(h > w\), else \(a = 0\)
- \(b = \max(w, \, h-1)\)
- \(\text{level} = b \times 2 + a\)

### 3.3 Level → Size

- \(\text{width} = 2^{\lfloor \text{level} / 2 \rfloor}\)
- \(\text{height} = \text{width} \times (\text{level} \bmod 2 + 1)\)

### 3.4 Core Data Structures

- **MAP table**: Key-value pairs \([K, \, \text{NODE}]\) for finding nodes by key.
- **BASE table**: Supports node addition, eviction, and "cold node" queries. The specific strategy depends on the underlying cache algorithm (e.g., LRU can use a linked list).
- **LEVEL table**: Similar to BASE but partitioned by LEVEL. Simplest implementation is an array of length MAX_LEVEL, where each element is a BASE table.

---

## 4. Algorithm Flow Overview

### 4.1 Initialization

- Initialize MAP.
- Add the top-level \(S \times S\) node to the BASE table and to the corresponding LEVEL table based on its level value.

### 4.2 Object Lookup

- Look up node by key \(K\) in MAP.
- If found, mark the node as recently used: `MARK(NODE)`.
- If not found, return "not found".

### 4.3 Object Insertion

- Precondition: The object to insert is not currently in any table.
- Calculate LEVEL value \(lv\) based on the object's size.
- Try behaviors **A**, **B**, **C** in sequence; insertion succeeds if any one succeeds.
- If all three fail, insertion fails.

---

## 5. Sub-algorithms and Pseudocode

### 5.1 Behavior A (Attempt) - Direct Reuse of Same-Level Cold Node

- **Time complexity**: \(O(1)\).
- **Approach**: Get the coldest node from BASE. If its LEVEL matches the required \(lv\), evict that node and place the new node in its position; otherwise fail.

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

### 5.2 Behavior B (Browse) - Find Cold Enough Node from Higher Levels and Split

- **Time complexity**: Approximately \(O(\log N)\).
- **Approach**: In the LEVEL table, get the coldest node from each level from \(lv\) to MAX_LEVEL to find the "coldest" one. If it's cold enough, proceed to **D** (downward split) to obtain a node at target \(lv\); otherwise fail.

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

### 5.3 Behavior C (Combine) - Merge Small Blocks in Same Region

- **Time complexity**: Approximately \(O(N)\) (assuming single eviction is \(O(1)\)); if single eviction is \(O(N)\), total is approximately \(O(N \log N)\), where \(K\) is the number of evicted nodes, approximately \(\log N\). More precisely: \(O(2N + K \cdot \text{Evict})\).
- **Approach**:
  - Maintain a temporary table \(M[\ ]\) of size \((W/w) \times (H/h)\), representing accumulated area for each region at the target size.
  - Traverse BASE from cold to hot, accumulating area by region. When a region's area \(\ge\) target area, that region can be merged; record position and index.
  - If accumulated area **exceeds** target area, there exists a node larger than the target level; execute **D** operation similar to Behavior B.
  - If **equal to** target area, evict nodes in that region by index and rebuild a new node using the index.
- **Relationship**: In Biped, area is \(1 \ll \text{level}\), and Biped area \(\ge\) actual required area.

```
ALGORITHM C(key, width, height, lv)
  M[MAX_WIDTH / width * MAX_HEIGHT / height] <- {0} 
  area <- 1 << lv 
  index_target <- -1
  last_node <- NULL
  FOR cold_node IN BASE  // COLD -> WARM
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

**Implementation Optimization (Area Accumulation)**: Use integer approximation for area by "ratio" to reduce memory and computation. For example: use 8-bit unsigned integer when LEVEL difference from target is < 8, 16-bit when < 16, and so on. If level difference is \(N\), treat it as full value right-shifted by \(N\) bits (e.g., for 8-bit with level difference 2, use \(256 \gg 2 = 64\)). Overflow to 0 indicates target area reached, simplifying determination and improving access efficiency.

### 5.4 Helper: Index in M

The index of \(M\) represents a one-dimensional number for "a region at the target size". For example, with total area 4×4 and needing a 2×4 region: `MAX_WIDTH/2 = 2`, `MAX_HEIGHT/4 = 1`, \(M\) length is 2; left region is index 0, right is index 1. Two-dimensional \((x, y)\) can be mapped to one-dimensional index similar to pixel arrangement. `CalculatePositionFromIndex` is the inverse operation, omitted here.

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

### 5.5 Operation D (Down Split) - Downward Splitting

Repeatedly split to obtain a node at the target level; the core is calculating child node position offsets.

**Rules**: The offset of the new node after splitting is the "horizontal edge length after splitting" (i.e., the width after splitting). Based on the parity of the current level, the offset is applied to either the x-axis or y-axis. For example:

- 1024×1024 (lv=20) → two 512×1024 (lv=19), new node offset (512, 0)
- 512×1024 → two 512×512, new node offset (0, 512)

In other words: the **post-split** width is the offset, then choose x or y based on level parity.

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

The complete flow of Operation D: continuously call split on the node until its level matches the required \(lv\); complete pseudocode omitted here.

---

## 6. Performance Testing

Test environment: MSVC, small object allocation with `free(malloc(211))`. Below are typical path timings and relative speeds.

| Scenario | Operation | TIME | SPEED |
|----------|-----------|------|-------|
| **1. Cache Hit** | lock_key + unlock | 0.8 | 1.2× |
| **2. Miss (A)** Direct cold node reuse | lock_key + lock_key_value + unlock | 1.23 | 0.8× |
| **3. Miss (B)** High-level split | lock_key_value + unlock | 1.9 | 0.52× |
| **4. Miss (Fail)** No reusable space | lock_key_value | 72 | 0.012× |

- Overall: Performance is comparable to malloc/free operations.
- **No external fragmentation**
- Internal fragmentation depends on data (no fragmentation when using only 32/64 MSDF)
- **Cache Hit**: Only queries MAP and marks access — minimal overhead.
- **Miss (A)**: Evicts and reuses same-level cold node — slightly slower than hit.
- **Miss (B)**: Finds coldest node across levels and splits — involves LEVEL scanning and splitting.
- **Miss (Fail)**: Attempts A/B/C paths before failing — significantly higher latency.
- **Miss (C Success)**: Theoretically, worst-case timing doubles due to two traversals, but conditions are too strict to construct test data.

## 7. Segment Tree

By maintaining a segment tree, the time complexity of the most expensive **Behavior C** can be reduced to approximately O(log N). However, the cost of maintaining the segment tree is high, and it's not very friendly to lock operations. Still, it's worth trying when time permits — practice beats theory.
