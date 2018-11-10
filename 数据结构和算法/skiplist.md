# skiplist
## 引言
去年在redis中看到skiplist,作为Hash结构的一种实现,了解了下原理.现在在java中又看到ConcurrentSkipList, 这里回头看看整体实现.
skiplist 作为一种概率型索引结构,经常用来和平衡二叉树作为对比,个人实现后感觉实现比各类平衡二叉树要好很多

## 数据结构说明
对于有header的结构来说,总体描述如下:

## 实现
Node和skipList定义
```cpp

template<typename T, typename V>
class Node {
public:de(int level, const T & t, const V &v) {
    this->t = t;
    this->v = v;
    this->level = level;
    this->nodes = (Node<T,V> **) malloc(sizeof(void *) * level);
    memset(this->nodes, 0, sizeof(void *) * level);
}
T t;
V v;
int level;
Node<T, V> **nodes;
};
template<typename T, typename V>
class SkipList {
public:

private:
    Node<T, V> * head;
    int level;
};
}
```
增删改查
```cpp
SkipList(int level) {
    this->level = level;
    head = new Node<T, V>(level, -1000, -1000);
}

void insert(const T &t, const V &v) {
    Node<T, V> * node2Insert = new Node<T, V>(level, t, v);
    Node<T, V> * node = head;
    //待插入数据每个level的前驱节点
    Node<T, V> *update[level];
    memset(update, 0 ,sizeof(void *) *level);
    for (int l = level - 1 ; l >= 0; --l) {
        // 如果当前节点的l层没有后继,则跳过
        while (node->nodes[l] && node->nodes[l]->t < t){
            node = node->nodes[l];
        }
        update[l] = node;
    }
    int totalLevel = 1;
    while (totalLevel < level && rand()%2) {
        ++totalLevel;
    }
    for (int i = 0; i < totalLevel; ++i) {
        node2Insert->nodes[i] = update[i]->nodes[i];
        update[i]->nodes[i] = node2Insert;
    }
}

Node<T, V> * findNode(T t) {
    Node<T, V> * node = head;
    for (int l = level - 1 ; l >= 0; --l) {
        while (node->nodes[l] && node->nodes[l]->t < t){
            std::cout << "goon" << std::endl;
            node = node->nodes[l];
        }
        if (node->nodes[l]) {
            std::cout << node->nodes[l]->t << l << std::endl;
        }
        if (node->nodes[l] && !(node->nodes[l]->t < t) && !(t <node->nodes[l]->t)) {
            std::cout << "find inner" << node->nodes[l]->t << l << std::endl;
            return node->nodes[l];
        }
    }
    return NULL;
}

int update(const T &t, const V &v) {
    Node<T, V> * node = findNode(t);
    if (!node) {
        return 0;
    }
    node->v = v;
    return 1;
}

int remove(const T &t) {
    Node<T, V> *update[level];
    memset(update, 0, sizeof(void*) * level);
    Node<T,V>* node = head;
    for (int i = level - 1; i >=0; --i) {
        while(node->nodes[i] && node->nodes[i]->t < t) {
            node = node->nodes[i];
        }
        update[i] = node; 
    }
    Node<T,V> *toDelete = update[0]->nodes[0];
    if (!toDelete || toDelete->t != t) {
        return false;
    }
    for (int i =0 ; i < level; ++i) {
        update[i]->nodes[i] = toDelete->nodes[i];
    }
    free(toDelete->nodes);
    free(toDelete);
    return true;
}

void debug() {
    Node<T,V> * node = head;
    std::cout << "debug::start" << std::endl;
    while (node) {
        std::cout<< node->t << ":" << node->v << std::endl;
        node = node->nodes[0];
    }
    std::cout << "debug::stop" << std::endl;
}
```