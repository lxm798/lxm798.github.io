# drf 

## 引言
DRF 用来解决多资源类型多用户系统中，资源公平分配的问题。
1. DRF 通过让公平分配资源的方式鼓励用户共享资源
2. DRF 的策略保证用户不能通过误报需求来占用更多资源
3. DRF 是envy-free，没有用户能和其他用户交换资源
4. DRF 分配是Pareto efficient, 其他用户不主动减少资源分配的情况下，指定用户不能增加资源。

## Introduce
当前最流行的分配方式是max-min faireness，(max-min是单资源类型的分配)。
 In a nutshell, DRF seeks to maximize the minimum dominant share across all users
 用户有相同的优先级，能够公平的获取资源

 ## Motivation
 挑战： 异构、多类型资源
 统计数据中：CPU 超用或者不足够，但是内存经常是足够的

 ## Allocation Properties
 machine<9CPUS, 18GB RAM>
 A: <1CPUS, 4GB RAM>
 B: <3CPUS, 1GB>
 为了解决公平性问题，首先假设任何唯独的资源都应该被满足。

 ## DRF
 用户所有占有率中的最大值称作用户的dominant share，与dominant share对应的资源被称作dominant resource
```
R = <r1; · · · ; rm> . total resource capacities
C = <c1; · · · ; cm> . consumed resources, initially 0
si (i = 1::n) . user i’s dominant shares, initially 0
Ui = hui;1; · · · ; ui;mi (i = 1::n) . resources given to
user i, initially 0
pick user i with lowest dominant share si
Di demand of user i’s next task
if C + Di ≤ R then
C = C + Di . update consumed vector
Ui = Ui + Di . update i’s allocation vector
si = maxm j=1fui;j=rjg
else
return . the cluster is full
end if
```
```cpp
double DRFSorter::calculateShare(const Node* node) const
{
  double share = 0.0;
  // 遍历所有的资源类型，total_.total是当前资源的总和，包含使用和未使用的
  foreachpair (const string& resourceName,
               const Value::Scalar& scalar,
               total_.totals) {
    // Filter out the resources excluded from fair sharing.
    if (fairnessExcludeResourceNames.isSome() &&
        fairnessExcludeResourceNames->count(resourceName) > 0) {
      continue;
    }
    // 已经分配的资源包括需要遍历的资源类型
    if (scalar.value() > 0.0 &&
        node->allocation.totals.contains(resourceName)) {
    // 已经分配的资源,可能是已经使用或者没有使用的资源
      const double allocation =
        node->allocation.totals.at(resourceName).value();
    // 当前占比最大的资源类型
      share = std::max(share, allocation / scalar.value());
    }
  }

  return share / findWeight(node);
}
```
