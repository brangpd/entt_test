# EnTT

## 基本概念

### Entity

EnTT 中，一个 entity 为一个整数类型，默认为 32 位整数（可通过宏定义修改）。它不是一个 class。

```cpp
enum class entity : uint32_t {};
```

### Component

component 为自行定义的 struct：

```cpp
struct position {
  float x, y;
};
struct velocity {
  float dx, dy;
};
struct name {
  std::string name;
};
```

### Registry

一个 registry 为一个 entities + components 的集合。可以理解为 ssjj2 中的 context。

```cpp
template <typename> class basic_registry;
using registry = basic_registry<entity>;
```

## 实现细节

### Registry

一个 registry 类，主要维护几样东西，并提供读写接口：

1. entities 存储
1. components 存储
1. contexts vars （略）

### Entity

entity 的类型为一个整数，默认为 32 位整数，前 12 位为版本号（用 `to_version` 接口取得），后 20 位为 entity 真正编号（用 `to_entity` 取得），但 entity 被定义为 enum class，因此类型不等于 uint32_t，也不能隐式转换为数字，必须通过 `to_integral` 接口转化为 uint32_t。

entity 用一个萃取模板定义，可以自行定义并修改：

```cpp
template<>
struct entt_traits<std::uint32_t> {
    using entity_type = std::uint32_t;
    using version_type = std::uint16_t;

    static constexpr entity_type entity_mask = 0xFFFFF;
    static constexpr entity_type version_mask = 0xFFF;
    static constexpr std::size_t entity_shift = 20u;
};

template<>
struct entt_traits<std::uint64_t> {
    using entity_type = std::uint64_t;
    using version_type = std::uint32_t;

    static constexpr entity_type entity_mask = 0xFFFFFFFF;
    static constexpr entity_type version_mask = 0xFFFFFFFF;
    static constexpr std::size_t entity_shift = 32u;
};
```

存在两个特殊的 entity：null 和 tombstone。这两者只用于做相等不相等的比较。如果版本号为 -1，则与 tombstone 相等，如果编号为 -1，则与 null 相等。

基本思想是用 vector 存储所有创建的 entities，删除 entity 时，直接删去 vector 中的对应值，并用一个链表 free list 存储被删除的 entity 的下标回收利用。所以对所有 entities 进行遍历时，需要遍历整个 vector 并且可能跳过某些不合法的 entity，效率可能不高，但是关系不大，应该没有对所有 entities 进行遍历的需求。

在这个思路的基础上做了一些优化。

每个创建的 entity 的编号都与在 vector 中的下标相等。不论版本号是多少，entity 编号为 i 则在 vector 中下标一定为 i，否则该 entity 不合法。版本号则用于判断该 entity 是否仍然有效。

free list 实际上只是一个链表的表头，所以只用一个整数存储，剩下的表里的其他值，都巧妙地安放到 entities 的 vector 中。

详细细节跑一遍示例程序来理解。初始状态如下。括号中，左边为版本号，右边为 entity 的真正编号，`*` 表示 -1。其中 free list 实际上只存储表头：

```txt
N = 0
freelist: *
entities:

create: (0, 0)
N = 1
freelist: *
entities: (0, 0)

create: (0, 1)
N = 2
freelist: *
entities: (0, 0) (0, 1)

create: (0, 2)
N = 3
freelist: *
entities: (0, 0) (0, 1) (0, 2)

create: (0, 3)
N = 4
freelist: *
entities: (0, 0) (0, 1) (0, 2) (0, 3)

create: (0, 4)
N = 5
freelist: *
entities: (0, 0) (0, 1) (0, 2) (0, 3) (0, 4)

destroy (0, 0): 将下标 [0] 赋值给 free list 表头，原 free list 表头 * 赋值给 entities[0]
N = 4
freelist: 0 [0 -> *]
entities: (1, *) (0, 1) (0, 2) (0, 3) (0, 4)

destroy (0, 2): 将下标 [2] 赋值给 free list 表头，原 free list 表头 0 赋值给 entities[2]
N = 3
freelist: 2 [2 -> 0 -> *]
entities: (1, *) (0, 1) (1, 0) (0, 3) (0, 4)

create: (1, 2)
N = 4
freelist: 0 [0 -> *]
entities: (1, *) (0, 1) (1, 2) (0, 3) (0, 4)

create: (1, 0)
N = 5
freelist: *
entities: (1, 0) (0, 1) (1, 2) (0, 3) (0, 4)
```

另外，当一个 entity 被删除时，该 entity 的所有 component 都会被删除。

### Component

registry 中 component 的存储包括两部分：某一特定类型 componet 的所有 components 的存储（component storage）；每种组件的 component 类型到这一类型 component 存储的映射（component storage map）。

类似于：

```cpp
using entity_type = int;
std::unordered_map<std::type_index, std::unordered_map<entity_type, std::any>> component_storage_map;
std::unordered_map<entity_type, std::any> &position_storage = component_storage_map[typeid(position)];
std::unordered_map<entity_type, std::any> &velocity_storage = component_storage_map[typeid(velocity)];
entity_type my_entity_id = 0;
position_storage[my_entity_id] = position{1, 2};
velocity_storage[my_entity_id] = velocity{3, 4};
```

component storage map 部分，就是用一个哈希表进行实现（没用 STL，用了优化过的 `entt::dense_map`。本质上是个拉链法实现的哈希表），key 为代表某个 component 类型的唯一 ID，value 为这一种 component 的 component storage。

component storage 部分：`entt::basic_storage`，本质上是 `entt::basic_sparse_set`。需要先了解一种数据结构：sparse set：

定义 sparse set 为一个集合 $A = \{ a_0, a_1, ..., a_N \}$，具有以下性质：

1. 集合中的每个元素唯一，不重复
1. 集合中的元素总数上限为 $M$，则对 $\forall a_i \in A, 0 \leq a_i < M$
1. 对集合的操作：增、删、查，时间复杂度为 $O(1)$；遍历所有元素，时间复杂度为 $O(N)$，$N$ 为集合的元素总个数

实现细节：

一个 sparse set 包含两个数组，分别称为 packed array （或称 dense array）与 sparse array。初始时集合为空，状态如下：N 即当前集合元素总数为 0。初始时，packed array 和 sparse array 中可能包含脏数据，不需要清零，原因后面解释。

```txt
N = 0    v-- packed array push back pointer
packed:  *   *   *   *   *   *   *   *   *
sparse:  *   *   *   *   *   *   *   *   * 
        [0] [1] [2] [3] [4] [5] [6] [7] [8] ...
```

现在往 sparse set 中添加一个值 4：首先在 packed array 中进行 push back 操作，4 将被放到 packed array 下标为 [0] 的位置：

```txt
N = 0        v
packed:  4   *   *   *   *   *   *   *   *
sparse:  *   *   *   *   *   *   *   *   * 
        [0] [1] [2] [3] [4] [5] [6] [7] [8] ...
```

然后在 sparse array 中，下标为 [4] 的位置上填上 4 这个值在 packed array 中的下标 [0]。

```txt
N = 0        v
packed:  4   *   *   *   *   *   *   *   *
sparse:  *   *   *   *   0   *   *   *   * 
        [0] [1] [2] [3] [4] [5] [6] [7] [8] ...
```

然后令 N = 1：

```txt
N = 1        v
packed:  4   *   *   *   *   *   *   *   *
sparse:  *   *   *   *   0   *   *   *   * 
        [0] [1] [2] [3] [4] [5] [6] [7] [8] ...
```

这样就完成了数字 4 的添加。可以按照此种方法依次再添加几个数 6、2、0、8：

```txt
N = 2            v
packed:  4   6   *   *   *   *   *   *   *
sparse:  *   *   *   *   0   *   1   *   * 
        [0] [1] [2] [3] [4] [5] [6] [7] [8] ...
```

```txt
N = 3                v
packed:  4   6   2   *   *   *   *   *   *
sparse:  *   *   2   *   0   *   1   *   * 
        [0] [1] [2] [3] [4] [5] [6] [7] [8] ...
```

```txt
N = 4                    v
packed:  4   6   2   0   *   *   *   *   *
sparse:  3   *   2   *   0   *   1   *   * 
        [0] [1] [2] [3] [4] [5] [6] [7] [8] ...
```

```txt
N = 5                        v
packed:  4   6   2   0   8   *   *   *   *
sparse:  3   *   2   *   0   *   1   *   4 
        [0] [1] [2] [3] [4] [5] [6] [7] [8] ...
```

此时两个数组具有以下性质：对于任意下标 $i \in [0, N), sparse[packed[i]] = i$。对 packed array 进行遍历操作就是对这个集合进行遍历操作。

查询操作：要查找任意值 $x$ 是否在 sparse set 中，只需检验 $sparse[x] \in [0, N), packed[sparse[x]] = x$ 是否同时成立即可。

这个集合中的每两个元素可以进行顺序的互换，时间复杂度为 $O(1)$，例如需要交换 0 和 8 的位置：先在 sparse array 中找到下标为 [0] 和 [8] 的位置上的两个数，即 0 和 8 在 packed array 中的下标 [3] [4]。同时交换 packed array 中下标为 [3] [4]，以及 sparse array 中下标为 [0] [8] 的值，结果如下：

```txt
N = 5                        v
packed:  4   6   2   8   0   *   *   *   *
sparse:  4   *   2   *   0   *   1   *   3 
        [0] [1] [2] [3] [4] [5] [6] [7] [8] ...
```

删除操作，例如要删除 6 这个值，有两种算法，第一种也就是 entt 默认的算法：

首先找到 6 在 packed array 中的下标为 [1]，然后将 packed array 中 [1] 与 [N-1] 即最后一项做交换：

```txt
N = 5                        v
packed:  4   0   2   8   6   *   *   *   *
sparse:  1   *   2   *   0   *   4   *   3 
        [0] [1] [2] [3] [4] [5] [6] [7] [8] ...
```

然后直接将 N 的值减一：

```txt
N = 4                    v
packed:  4   0   2   8   6   *   *   *   *
sparse:  1   *   2   *   0   *   4   *   3 
        [0] [1] [2] [3] [4] [5] [6] [7] [8] ...
```

这样就完成了删除操作，此时 packed array 中 6 的值依然存在，sparse array 中下标为 [6] 的值也没有改动，但是不会有任何影响。因为查询操作要求 $sparse[x] \in [0, N), packed[sparse[x]] = x$ 同时成立，如果要查询 6 是否在集合中，此时检查 $sparse[6] = 4 \notin [0, 4)$，所以 6 不在集合中。所以前文所说，两个数组初始化时可能有脏数据，但对正确性并无影响。

所以，如果要清空这个集合，将 N 设置为 0 即可。

第二种算法不做交换，直接在相应位置上删除对应的 ID，并维护一个 free list 来分配下一次加入的新值的位置，并且两个数组分别用版本号或者其他方法标记数值是否合法，这就和 entities 存储类似，略。

遍历操作：取出 packed 数组依次遍历即可（如果删除采用不做交换的算法，需要额外判断每个值是否合法）。

有了 sparse set 之后，对于 component storage，进行一些适配即可，例如下列状态代表 `struct position` 的 component storage，在基础的 sparse set 上加入存放 position 的数据：

```txt
position component storage:
N = 4                    v
packed:  0,4 0,0 0,2 0,8 *   *   *   *   *   <- entity (version and id)
         0,1 2,3 4,5 6,7 *   *   *   *   *   <- struct position data
sparse:  1   *   2   *   0   *   *   *   3 
        [0] [1] [2] [3] [4] [5] [6] [7] [8] ...
```

同时注意到，entity 的 id，也就是 sparse set 的下标是任意的，比如有可能一上来就加一个 id 为 10000 的 entity，所以 sparse set 不可能用一个 vector 来实现。entt 的解决方案是用类似于 `uint32_t** sparse` 的分页的方法，例如对于 entity id 为 10000，按 4096 个 `uint32_t` 为一组，最终取到 `sparse[10000 / 4096][10000 % 4096]`。

实际在 entt 中，采用 basic_sparse_set 来存 entity，并且用 basic_storage 来存 position 数据。

### View

view 的作用类似于 Entitas 中的 Group，通常用于遍历拥有某一类 component 的所有 entities。

对于单个 component 的 view 和多个 component 的 view，实现略有不同。

```cpp
auto velocity_view = registry.view<velocity>();
for (auto &&[entity, velocity] : velocity_view.each()) {
  // ...
}
auto velocity_position_view = registry.view<velocity, position>();
for (auto &&[entity, velocity, position] : velocity_view.each()) {
  // ...
}
```

单 component 的 view：实现非常简单，根据前文 component 的数据结构，只需拿到 component storage 即 sparse set 的 packed array 进行遍历即可，因而构造和析构也几乎没有任何代价，可以在需要的时候即用即删，不需要保存。时间复杂度为 $O(n)$，$n$ 为 entities 总数。

多 component 的 view：在创建这个 view 时，首先找到 `struct position` 和 `struct velocity` 这两种 component，哪一种包含的 entities 个数少，例如有 4 个 entities 拥有 position，有 3 个 entities 拥有 velocity，那么对这个 view 进行遍历时，使用 velocity 的 component storage 进行遍历，并对每个拥有 velocity 的 entity 进行是否拥有 position 的判断。遍历的时间复杂度为 $O(m \times n)$，$m = 1$ 即总共有 1 个额外的 component 筛选条件，$n = 3$ 即拥有 velocity 的 entities 总数。

另外，以上所讲是拥有某类 component（entt 中称为 get 筛选），还可以添加对 component 的 exclude 筛选，即不拥有某类 component 的 entities 去除，会在每次遍历时判断一下是否不拥有。时间复杂度为 $O(m \times n)$，$n$ 为所有筛选条件中，某一类含 entities 数量最少的 component 的 entities 数量，$m$ 为其余筛选条件（get + exclude）的总数。

### Group

group 是另外一种用于遍历 components 的工具。用法和 view 有些类似，例如：

```cpp
entt::group<entt::owned_t<position, velocity>, entt::get_t<>, entt::exclude_t<>>
    position_group = registry.group<position, velocity>(entt::get<>, entt::exclude<>);
auto &&[position0, velocity0] = position_group.get<position, velocity>(e[1]);
for (auto &&[entity, the_position, the_velocity] : position_group.each()) {
  std::cout << entt::to_entity(entity) << ", ";
}
```

但是，group 的用途是用于快速地获取多个 components 的，用于一些性能瓶颈的优化，一般情况下只需要用 view 即可。

group 的使用上有一些限制：必须声明这个 group “拥有”（owned）的 component storage，上例中是 position 和 velocity。而且，不允许有多个 group 同时 owned 同一种 component storage，例如在上例中，不允许再声明一个 owned position 的 group（即使旧的 group 已经被析构了也不允许）。此外，owned get exclude 的参数数量需要满足以下条件：

```cpp
static_assert(sizeof...(Owned) + sizeof...(Get) > 0);
static_assert(sizeof...(Owned) + sizeof...(Get) + sizeof...(Exclude) > 1);
```

它的实现细节如下：

假设我们有两种 component，它们的 component storage 如下，packed array 即 entity 的 id（sparse array 与具体的 position、velocity 的值不重要，略）：

```txt
position
N = 4                    v
packed:  3   7   8   6   *   *   *   *   *

velocity
N = 2            v
packed:  4   5   *   *   *   *   *   *   *
```

然后我们现在定义一个 group 为：owned position，owned velocity。设置一个 group end，它表示它指向的位置之前的所有 entities 即为这个 group 包含的 entities。于是现在这个 group 为空，因为两个 component storage 中没有相同 entity。

```txt
position
N = 4                    v
packed:  3   7   8   6   *   *   *   *   *
         ^-- group end iterator
         v-- group end iterator
velocity
N = 2            v
packed:  4   5   *   *   *   *   *   *   *
```

然后，现在我们给 entity 7 添加一个 velocity：

```txt
position
N = 4                    v
packed:  3   7   8   6   *   *   *   *   *
         ^
         v
velocity
N = 3                v
packed:  4   5   7   *   *   *   *   *   *
```

然后，此时，7 同时出现在 position 的 storage 中。

然后，检查发现 7 同时出现在 position 的 storage 中，于是同时将两个 storage 中的 7 移动至 group end 指向的位置（与已有 entity 交换位置），同时 group end 后移一位。

```txt
position
N = 4                    v
packed:  7   3   8   6   *   *   *   *   *
             ^
             v
velocity
N = 3                v
packed:  7   5   4   *   *   *   *   *   *
```

此时 group 拥有一个元素 7。同理可以给 4 添加一个 position，此时 group 就拥有两个元素 4 和 7：

```txt
position
N = 5                        v
packed:  7   4   8   6   3   *   *   *   *
                 ^
                 v
velocity
N = 3                v
packed:  7   4   5   *   *   *   *   *   *
```

删除操作类似，例如删除 7 的 velocity 组件，首先在这两个 storage 中，同时将 7 与 group end 的前一位 4 交换，然后 group end 减一即可：

```txt
position
N = 5                        v
packed:  4   7   8   6   3   *   *   *   *
             ^
             v
velocity
N = 3                v
packed:  4   7   5   *   *   *   *   *   *
```

当然，在这之后需要执行 velocity storage 中的删除操作（7 与 5 互换位置并删去 7）：

```txt
position
N = 5                        v
packed:  4   7   8   6   3   *   *   *   *
             ^
             v
velocity
N = 2            v
packed:  4   5   *   *   *   *   *   *   *
```

这就是对 owned 的组件的添加和删除 entity 时所做的调整，这样一来每次遍历所有具有 position 和 velocity 的组件的 entities 速度非常快。至于 get 和 exclude 条件，采用的是普通的 sparse set 查询操作，时间复杂度为 $O(1)$。

因为这种对存储的调整，每种 component storage 只能同时被一个 group 所 owned。

## 参考资料

1. <https://skypjack.github.io/2019-03-21-ecs-baf-part-2-insights/>
2. <https://manenko.com/2021/05/23/sparse-sets.html>
3. <https://github.com/skypjack/entt/wiki>
