# stringbuffer & stringbuilder 比较

stringbuffer 是线程安全的,stringbuilder不是

## stringbuilder
StringBuilder 继承 AbstractStringBuilder
```java
abstract class AbstractStringBuilder implements Appendable, CharSequence {
    /**
     * The value is used for character storage.
     */
    char[] value;

    /**
     * The count is the number of characters used.
     */
    int count;
}

public AbstractStringBuilder append(String str) {
    if (str == null) str = "null";
    int len = str.length();
    ensureCapacityInternal(count + len);
    str.getChars(0, len, value, count);
    count += len;
    return this;
}

void expandCapacity(int minimumCapacity) {
    // 每次扩大min(minimumCapacity, value.length * 2 + 2)
    int newCapacity = value.length * 2 + 2;
    if (newCapacity - minimumCapacity < 0)
        newCapacity = minimumCapacity;
    if (newCapacity < 0) {
        if (minimumCapacity < 0) // overflow
            throw new OutOfMemoryError();
        newCapacity = Integer.MAX_VALUE;
    }
    value = Arrays.copyOf(value, newCapacity);
}
```

StringBuffer在上述函数的入口添加synchronized