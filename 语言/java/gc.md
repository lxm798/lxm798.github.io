# Gc

## 内存区域
* 程序计数器 :当前线程所执行的字节码的行号指示器
* JAVA堆 : Eden, From Survivor, To Survivor, TLB
* 虚拟机栈:线程私有,生命周期和线程相同.
* 本地方法栈:
* 方法区
* 运行时常量池
* 直接内存/堆外内存:NIO Buffer

## 分代信息
* Eden/young
* old
* survive
* 永久代
* code

## GC 算法
### 过程
####  标记-清除
产生大量的不连续的内存碎片
#### 标记-整理
将清理的数据都向一端移动
#### 复制
将内存划分为相等的两块, Eden->Survivor
#### 分代收集
对前面的方案进行组合,新生代,有担保,复制,老年代,没有担保,标记-整理

GCROOT的枚举一定会发生STW, 使用OopMap记录GCROOT

### 新生代
Serial : 单线程,复制,简单高效, 老年代标记整理
Parallel Scavenge: 复制算法,可控制的吞吐量
ParNew: Serial 的多线程版本, 唯一和CMS配合,新生代复制,老年代标记整理, -XX:ParallelGCThreads
G1:
### 老年代
SerialOld: 标记-整理,搭配Parallel-Scavenge或者作为CMS的后备
Parallel old:Parallel Scavenge的老年代版本,标记整理,只能和Parallel Scavenge 搭配
Cms:

  最短停顿为目标,标记-清除,初始标记(STW,GCRoots能直接关联到的对象)->并发标记(GCROOT TRACING)->重新标记(STW,新增GCROOT)->并发清除

  碎片的问题可以有两种方案解决: full gc, 强制开启碎片整理

  浮动垃圾:在并发清理阶段产生的垃圾

  参数: -XXCMSInitiatingOccupancyFraction 老年代空间使用达到一定比例触发垃圾回收
G1:
  初始标记->并发标记->最终标记->筛选回收(优先回收价值对打的Region)

## 内存分配策略
* 优先分配Eden
* 大对象直接进入老年代 -XX:PretenureSizeThreshold
* 长期存活的对象进入老年代 -XX:MaxTenuringThreshold
* 空间分配担保: 老年代最大可用的连续空间是否大于新生代所有对象总空间
## 问题
### 如何处理循环引用
通过扫描是否有当前被引用的GCROOT到达,如果没有则可以清除

### shallow heap 和 retained heap
shallow heap是自身的内存开销
retained heap 是对象被清除后能回收多少内存

### major gc & fullgc
作者：RednaxelaFX
链接：https://www.zhihu.com/question/41922036/answer/93079526
来源：知乎
著作权归作者所有。商业转载请联系作者获得授权，非商业转载请注明出处。

针对HotSpot VM的实现，它里面的GC其实准确分类只有两大种：Partial GC：并不收集整个GC堆的模式Young GC：只收集young gen的GCOld GC：只收集old gen的GC。只有CMS的concurrent collection是这个模式Mixed GC：收集整个young gen以及部分old gen的GC。只有G1有这个模式Full GC：收集整个堆，包括young gen、old gen、perm gen（如果存在的话）等所有部分的模式。Major GC通常是跟full GC是等价的，收集整个GC堆。但因为HotSpot VM发展了这么多年，外界对各种名词的解读已经完全混乱了，当有人说“major GC”的时候一定要问清楚他想要指的是上面的full GC还是old GC。最简单的分代式GC策略，按HotSpot VM的serial GC的实现来看，触发条件是：young GC：当young gen中的eden区分配满的时候触发。注意young GC中有部分存活对象会晋升到old gen，所以young GC后old gen的占用量通常会有所升高。full GC：当准备要触发一次young GC时，如果发现统计数据说之前young GC的平均晋升大小比目前old gen剩余的空间大，则不会触发young GC而是转为触发full GC（因为HotSpot VM的GC里，除了CMS的concurrent collection之外，其它能收集old gen的GC都会同时收集整个GC堆，包括young gen，所以不需要事先触发一次单独的young GC）；或者，如果有perm gen的话，要在perm gen分配空间但已经没有足够空间时，也要触发一次full GC；或者System.gc()、heap dump带GC，默认也是触发full GC。HotSpot VM里其它非并发GC的触发条件复杂一些，不过大致的原理与上面说的其实一样。当然也总有例外。Parallel Scavenge（-XX:+UseParallelGC）框架下，默认是在要触发full GC前先执行一次young GC，并且两次GC之间能让应用程序稍微运行一小下，以期降低full GC的暂停时间（因为young GC会尽量清理了young gen的死对象，减少了full GC的工作量）。控制这个行为的VM参数是-XX:+ScavengeBeforeFullGC。这是HotSpot VM里的奇葩嗯。可跳传送门围观：JVM full GC的奇怪现象，求解惑？ - RednaxelaFX 的回答并发GC的触发条件就不太一样。以CMS GC为例，它主要是定时去检查old gen的使用量，当使用量超过了触发比例就会启动一次CMS GC，对old gen做并发收集。