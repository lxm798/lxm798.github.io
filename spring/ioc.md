# ioc/Inversion of Control

## 引言
ioc直译过来就是控制反转.那么这个反转是反转了什么呢?
我们写代码的时候通常是我们的代码来调用javalib中的函数,但是如何让javalib中的函数调用我们的函数呢?这个就是控制反转.

为了达到控制反转的目的,即让spring控制我们的代码,需要做什么呢?
首先不能什么都不告诉spring,这样spring就完全无法了解我们的代码.所以就需要有协议,但是这个协议有谁定义呢?
一定是由spring来定义,因为spring只是按照自己定义的一些协议/规则来处理代码,并不需要了解外部的代码,所以
必须是spring来定义.
为了减少对代码的侵入spring采用了注解和XML配置的方式来处理用户定义的代码:
Controller,Service,PostConstruct,PlacerHodlerConfigure等.

这些本质上都是按照设计模式原则来设计:
* 依赖倒转: 这个是spring的核心,实现方式是DI
* 接口隔离: Ioc不仅仅根据class注入bean,还会根据接口类型自动装备一个bean ??
* 里氏替换: 对于实现了DataSource接口的工具,都可以用来作为DataSource的配置
* 开闭原则: spring中的基本原则,对于扩展开放,对修改封闭.spring 大部分采用注入和组合的方式完成功能,比如对bean的postprocess,等 不要修改原有代码即可完成功能添加, 这可是建立在依赖倒转,里氏替换的基础上完成.
ioc 本身是XXX.使用主要使用代理模式,工厂模式,模板方法.
代理模式:对对象做一层基础封装,满足注入时的要
工厂模式:通过工厂创建对象,避免用户手动添加对象
模板方法:好莱坞发展,底层调用上层代码的一种方式,用户按照framework的协议实现方法,在注入后,framwork按照协议调用方法
策略模式: 完成同一项事情通常有多种方法,把这些方法封装为不同的策略,即为策略模式.spring中的UrlResource,ClassPathResource,FileSystemResource 都是针对不同的资源采用不同的模式来处理
装饰器模式:增加一个修饰类包裹原来的类,修饰类必须和原来的类有相同的接口。BeanWrapper包装Bean

## 部分注解说明
## beanfactory
beanfactory 作为ioc主要实现类,通过getBean 获取对象
```java
protected <T> T doGetBean(
    final String name, final Class<T> requiredType, final Object[] args, boolean typeCheckOnly)
    throws BeansException {

    final String beanName = transformedBeanName(name);
    Object bean;

    // Eagerly check singleton cache for manually registered singletons.
    Object sharedInstance = getSingleton(beanName);
    if (sharedInstance != null && args == null) {
        if (logger.isDebugEnabled()) {
            if (isSingletonCurrentlyInCreation(beanName)) {
                logger.debug("Returning eagerly cached instance of singleton bean '" + beanName +
                        "' that is not fully initialized yet - a consequence of a circular reference");
            }
            else {
                logger.debug("Returning cached instance of singleton bean '" + beanName + "'");
            }
        }
        bean = getObjectForBeanInstance(sharedInstance, name, beanName, null);
    }

    else {
        // Fail if we're already creating this bean instance:
        // We're assumably within a circular reference.
        if (isPrototypeCurrentlyInCreation(beanName)) {
            throw new BeanCurrentlyInCreationException(beanName);
        }

        // Check if bean definition exists in this factory.
        BeanFactory parentBeanFactory = getParentBeanFactory();
        if (parentBeanFactory != null && !containsBeanDefinition(beanName)) {
            // Not found -> check parent.
            String nameToLookup = originalBeanName(name);
            if (args != null) {
                // Delegation to parent with explicit args.
                return (T) parentBeanFactory.getBean(nameToLookup, args);
            }
            else {
                // No args -> delegate to standard getBean method.
                return parentBeanFactory.getBean(nameToLookup, requiredType);
            }
        }

        if (!typeCheckOnly) {
            markBeanAsCreated(beanName);
        }

        try {
            final RootBeanDefinition mbd = getMergedLocalBeanDefinition(beanName);
            checkMergedBeanDefinition(mbd, beanName, args);

            // Guarantee initialization of beans that the current bean depends on.
            String[] dependsOn = mbd.getDependsOn();
            if (dependsOn != null) {
                for (String dep : dependsOn) {
                    if (isDependent(beanName, dep)) {
                        throw new BeanCreationException(mbd.getResourceDescription(), beanName,
                                "Circular depends-on relationship between '" + beanName + "' and '" + dep + "'");
                    }
                    registerDependentBean(dep, beanName);
                    try {
                        getBean(dep);
                    }
                    catch (NoSuchBeanDefinitionException ex) {
                        throw new BeanCreationException(mbd.getResourceDescription(), beanName,
                                "'" + beanName + "' depends on missing bean '" + dep + "'", ex);
                    }
                }
            }

            // Create bean instance.
            if (mbd.isSingleton()) {
                sharedInstance = getSingleton(beanName, new ObjectFactory<Object>() {
                    @Override
                    public Object getObject() throws BeansException {
                        try {
                            return createBean(beanName, mbd, args);
                        }
                        catch (BeansException ex) {
                            // Explicitly remove instance from singleton cache: It might have been put there
                            // eagerly by the creation process, to allow for circular reference resolution.
                            // Also remove any beans that received a temporary reference to the bean.
                            destroySingleton(beanName);
                            throw ex;
                        }
                    }
                });
                bean = getObjectForBeanInstance(sharedInstance, name, beanName, mbd);
            }

            else if (mbd.isPrototype()) {
                // It's a prototype -> create a new instance.
                Object prototypeInstance = null;
                try {
                    beforePrototypeCreation(beanName);
                    prototypeInstance = createBean(beanName, mbd, args);
                }
                finally {
                    afterPrototypeCreation(beanName);
                }
                bean = getObjectForBeanInstance(prototypeInstance, name, beanName, mbd);
            }

            else {
                String scopeName = mbd.getScope();
                final Scope scope = this.scopes.get(scopeName);
                if (scope == null) {
                    throw new IllegalStateException("No Scope registered for scope name '" + scopeName + "'");
                }
                try {
                    Object scopedInstance = scope.get(beanName, new ObjectFactory<Object>() {
                        @Override
                        public Object getObject() throws BeansException {
                            beforePrototypeCreation(beanName);
                            try {
                                return createBean(beanName, mbd, args);
                            }
                            finally {
                                afterPrototypeCreation(beanName);
                            }
                        }
                    });
                    bean = getObjectForBeanInstance(scopedInstance, name, beanName, mbd);
                }
                catch (IllegalStateException ex) {
                    throw new BeanCreationException(beanName,
                            "Scope '" + scopeName + "' is not active for the current thread; consider " +
                            "defining a scoped proxy for this bean if you intend to refer to it from a singleton",
                            ex);
                }
            }
        }
        catch (BeansException ex) {
            cleanupAfterBeanCreationFailure(beanName);
            throw ex;
        }
    }

    // Check if required type matches the type of the actual bean instance.
    if (requiredType != null && bean != null && !requiredType.isInstance(bean)) {
        try {
            return getTypeConverter().convertIfNecessary(bean, requiredType);
        }
        catch (TypeMismatchException ex) {
            if (logger.isDebugEnabled()) {
                logger.debug("Failed to convert bean '" + name + "' to required type '" +
                        ClassUtils.getQualifiedName(requiredType) + "'", ex);
            }
            throw new BeanNotOfRequiredTypeException(name, requiredType, bean.getClass());
        }
    }
    return (T) bean;
}
```
对于singleton和prototype分别有不同的处理方式,spring 内置对于单例的循环处理,对于非单例则不支持
### 于循环引用的处理
spring 处理循环通过集中数据结构来处理,对象创建时有以下几个阶段
1.未创建
2.创建但是未初始化
3.对象初始化
4.创建完成
对于循环的引用的情况发生在
A对象创建后,开始初始化成员B,成员B中有A,在B对象初始化时依赖的A对象还没有初始化完成.这样就导致了循环依赖

针对上面的情况,spring 分别记录每一阶段对象的状态
* singletonObjects: 已经创建完成的对象集合
* earlySingletonObjects: 初始化过程中的对象
* singletonFactories: 获取初始化过程的对象
* singletonsCurrentlyInCreation: 当前在创建中标志
对于上面的流程:
1. 所有集合都是空
2. A创建但不初始化,singletonsCurrentlyInCreation.add(A),singletonFactories.add(AEarlyFactory)
3. A初始化对象B,B执行A中同样的流程,但是A创建完成,查找A的singletonFactories,如果发现工厂对象,创建对象
    执行singletonFactories.remove(AEarlyFactory), earlySingletonObjects(A), 由于A是单例,这样后续
    就不需要在创建对象.
4. B对象创建完成,加入singletonObjects中,然后返回,A对象继续完成对象的初始化,直到完成加入加入singletonObjects中.
