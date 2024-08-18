# Linux Cached rbtrees
Computing the leftmost (smallest) node is quite a common task for binary search trees, such as for traversals or users relying on a the particular order for their own logic. To this end(为此目的), users can use ‘struct rb_root_cached’ to optimize O(logN) rb_first() calls to a simple pointer fetch avoiding potentially expensive tree iterations. This is done at negligible(不值一提的) runtime overhead for maintanence; albeit(尽管) larger memory footprint(占用空间).
> 降低计算最小元素的时间：O(log(N)) --> O(1)

## 主要结构
```c
struct rb_root_cached {
	struct rb_root rb_root;
	struct rb_node *rb_leftmost; // 缓存 leftmost
};
```

## 主要接口
```c
struct rb_node *rb_first_cached(struct rb_root_cached *tree);
void rb_insert_color_cached(struct rb_node *, struct rb_root_cached *, bool);
void rb_erase_cached(struct rb_node *node, struct rb_root_cached *);
```

## 示例
```c
// 定义节点
struct entry {
    int key;
    struct rb_node node;
};
// 定义空树
struct rb_root_cached rbtree = RB_ROOT_CACHED;

// 插入
static int 
insert(struct rb_root_cached *root, struct entry *data)
{
    struct rb_node **p = &root->rb_root.rb_node, *parent = NULL;
    bool leftmost = true;
    
    while (*p) {
        struct entry *entry = container_of(*p, struct entry, node);

        parent = *p;

        if (data->key < entry->key) {
            p = &parent->rb_left;
        } else if (data->key > entry->key) {
            p = &parent->rb_right;
            leftmost = false;
        } else {
            return -1;
        }
    }

    rb_link_node(&data->node, parent, p);
    rb_insert_color_cached(&data->node, root, leftmost);

    return 0;
}

// Traverse the RBTree in order
static void 
traverse(struct rb_root_cached *root) 
{
    for (struct rb_node *p = rb_first_cached(root); p; p = rb_next(p)) {
        struct entry * e = container_of(p, struct entry, node);
        printk(KERN_INFO "key: %d\n", e->key);
    }
}

// 查询
static struct rb_node *
search(struct rb_root_cached *root, int key) 
{
    struct rb_node *p = root->rb_root.rb_node;

    while (p) {
        struct entry *e = container_of(p, struct entry, node);

        if (key < e->key) {
            p = p->rb_left;
        } else if (key > e->key) {
            p = p->rb_right;
        } else {
            return p;
        }
    }

    return NULL;
}

// 销毁
static void
destroy(struct rb_root_cached *root)
{
    struct entry *e;
    struct rb_node *p;

    while ((p = rb_first_cached(root))) {
        e = container_of(p, struct entry, node);
        rb_erase_cached(p, root);
        kfree(e);
    }
}
```