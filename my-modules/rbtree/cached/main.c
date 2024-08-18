#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/rbtree.h>

MODULE_LICENSE("GPL");

const int times = 10;

// 顶节点
struct entry {
    int key;
    struct rb_node node;
};

// 初始化空树
static struct rb_root_cached rbtree = RB_ROOT_CACHED;

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

// 删除
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

static int __init 
test_rbtree_init(void)
{
    for (int i = 0; i < times; ++i) {
        struct entry *e = kmalloc(sizeof(struct entry), GFP_KERNEL);
        e->key = i;
        insert(&rbtree, e);
    } 
    
    printk(KERN_INFO "original rbtree: \n");

    traverse(&rbtree);

    for (int i = 0; i < times + 1; ++i) {
        printk(KERN_INFO "Key=%d in tree: %s\n", 
                    i, (search(&rbtree, i))?"true":"false");
    }

    for (int i = 1; i < times; i *= 2) {
        struct rb_node *p = search(&rbtree, i);
        struct entry *e = container_of(p, struct entry, node);

        rb_erase_cached(p, &rbtree);
        kfree(e);
        printk(KERN_INFO "Erase key=%d\n", i);
    }

    printk(KERN_INFO "After erasing: \n");
    traverse(&rbtree);

    destroy(&rbtree);

    return 0;
}


static void __exit 
test_rbtree_exit(void)
{
}

module_init(test_rbtree_init);
module_exit(test_rbtree_exit);
