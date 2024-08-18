# Linux rbtree
内核实现文件：lib/rbtree.c
使用：
```c
#include <linux/rbtree.h>
```

## 创建一颗红黑树
### 定义节点
```c
struct mytype {
      struct rb_node node;
      char *keystring;
};
```

### 定义树根
```c
struct rb_root mytree = RB_ROOT;
```

## 查找
```c
struct mytype *my_search(struct rb_root *root, char *string)
{
      struct rb_node *node = root->rb_node;

      while (node) {
              struct mytype *data = container_of(node, struct mytype, node);
              int result;

              result = strcmp(string, data->keystring);

              if (result < 0)
                      node = node->rb_left;
              else if (result > 0)
                      node = node->rb_right;
              else
                      return data;
      }
      return NULL;
}
```

##  插入
```c
int my_insert(struct rb_root *root, struct mytype *data)
{
      struct rb_node **new = &(root->rb_node), *parent = NULL;

      /* Figure out where to put new node */
      while (*new) {
              struct mytype *this = container_of(*new, struct mytype, node);
              int result = strcmp(data->keystring, this->keystring);

              parent = *new;
              if (result < 0)
                      new = &((*new)->rb_left);
              else if (result > 0)
                      new = &((*new)->rb_right);
              else
                      return FALSE;
      }

      /* Add new node and rebalance tree. */
      rb_link_node(&data->node, parent, new);
      rb_insert_color(&data->node, root);

      return TRUE;
}
```

## 删除
```c
struct mytype *data = mysearch(&mytree, "walrus");

if (data) {
      rb_erase(&data->node, &mytree);
      myfree(data);
}
```

## 遍历（中序）
Four functions are provided for iterating through an rbtree’s contents in sorted order:
```c
struct rb_node *rb_first(struct rb_root *tree);
struct rb_node *rb_last(struct rb_root *tree);
struct rb_node *rb_next(struct rb_node *node);
struct rb_node *rb_prev(struct rb_node *node);
```

Example:
```c
struct rb_node *node;
for (node = rb_first(&mytree); node; node = rb_next(node))
      printk("key=%s\n", rb_entry(node, struct mytype, node)->keystring);
```