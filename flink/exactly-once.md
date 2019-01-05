# exactly-once
## 语义
Exactly-once:每个输入的event对于最终的结果有且只有一次影响.
首先需要理解checkpoint,checkpoint是一种分布式一致性的快照:
* 当前应用的状态
* 输入流的位置
Flink定时持久化checkpoint.持久化的流程是异步的,这样在持久化checkpoint时,不会影响Flink中应用的进度.
在异常和重启时,Flink从最新的checkpoint进行恢复.1.4.0之前的版本,exactly-once的语义只能用在Flink Application上,不能扩展到接受处理后的数据的外部系统中.但是通常Flink需要和一系列的持久化系统进行对接,开发者需要维护exactly-once的语义.
为了提供端到端的exacly-once的语义,也就意味着外部系统必须能够协助提供提交或者回滚的功能,以便于保持和Flink的checkpoint一直的状态.


## End-to-end Exactly Once Applications with Apache Flink
下面通过一个读写kafka的实例阐述端到端的Exactly once的语义.

整个系统由以下几个部分组成:
* datasources- kakfaConsumer
* a windowed aggregation
* data sink- write data back to kafa kafkaProducer


pre-commit:checkpoint开始时,Flink的jobManager 注入一个checkpint barrier(用来)
数据源存储kakfa的offsets,在这个步骤结束后将checkpoint barrier传入下一步操作


保证exactly-once是需要源端，streaming系统和输出共同协作，主要的要求是
1.源端要支持重放, 比如Kafka，Amazon Kinesis
2. 中间streaming系统的容错处理保证task只会产生exactly-once的输出
3. 输出端要有transactional update下游输出幂等的情况比较好处理，streaming系统输出结果可以直接update下游输出不幂等，需要引入版本控制机制可以参考:

## TwoPhaseCommitSinkFunction & FlinkKafkaProducer011
TwoPhaseCommitSinkFunction 作为两阶段的一个抽象实现,规定了以下的接口:
https://ci.apache.org/projects/flink/flink-docs-release-1.4/api/java/org/apache/flink/streaming/api/functions/sink/TwoPhaseCommitSinkFunction.html

FlinkKafkaProducer011实现如下
```java
@Override
protected KafkaTransactionState beginTransaction() throws FlinkKafka011Exception {
    switch (semantic) {
        case EXACTLY_ONCE:
            FlinkKafkaProducer<byte[], byte[]> producer = createTransactionalProducer();
            producer.beginTransaction();
            return new KafkaTransactionState(producer.getTransactionalId(), producer);
            ```
    }
}

@Override
protected void preCommit(KafkaTransactionState transaction) throws FlinkKafka011Exception {
    switch (semantic) {
        case EXACTLY_ONCE:
        case AT_LEAST_ONCE:
            flush(transaction);
        ...
}

@Override
protected void commit(KafkaTransactionState transaction) {
    if (transaction.isTransactional()) {
        try {
            transaction.producer.commitTransaction();
        } finally {
            recycleTransactionalProducer(transaction.producer);
        }
    }
}

@Override
protected void abort(KafkaTransactionState transaction) {
    if (transaction.isTransactional()) {
        transaction.producer.abortTransaction();
        recycleTransactionalProducer(transaction.producer);
    }
}
```







##参考
https://github.com/apache/flink/blob/master/flink-connectors/flink-connector-kafka-0.11/src/main/java/org/apache/flink/streaming/connectors/kafka/FlinkKafkaProducer011.java