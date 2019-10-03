# nio 快速入门
nio 个人理解是java版本的多路复用实现。如果有linux下相关编程经验，可以认为是对于epoll/poll的封装。
nio 主要有三个概念
## Channel
Channel可以理解为通道，对应socket，channel读出来的数据保存在ByteBuffer中

## Byteffer
ByteBuffer　是一段缓冲器，但是没有规定实现，满足对应接口就行

## Selector
但是有了上诉的概念之后，并不够，什么时候需要触发读数据呢？这时候就需要Selector，Selector用于收集读写事件,通过select函数触发。
```java
    public abstract int select(long timeout) throws IOException;
```


## 代码实例
```java
    while (selector.isOpen()) {
        try {
            int keys = selector.select(10_000);
            if (keys == 0) {
                continue;
            }
            Set<SelectionKey> selectedKeys = selector.selectedKeys();
            for (SelectionKey key : selectedKeys) {
                if (key.isAcceptable()) {
                    doSomething
                } else if (key.isReadable()) {
                    doSomething
                } else if (key.isWritable()) {
                    doSomething
                } else if (key.isConnectable()) {
                    doSomething
                }
            }
            selectedKeys.clear();
        } catch (Exception e) {
            selector.close();
        }
    }
```
注意selectedKeys 一定要清空，否者会一直触发select数据，但是keys = 0

## 坑
1. selectedKeys每次都要清空，否者有可能读不到数据