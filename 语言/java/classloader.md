# Classloader
## 加载
### 双亲委派
Classloader分为多个层级
* 启动类加载器BootStrap Classloader加载JAVA_HOME/lib中或者-Xbootclasspath指定的路径中,并且是虚拟机识别的
如果需要把加载请求委派给引导类加载器直接使用null即可
* 扩展类加载器Extension Classloader: 由sun.misc.Launcher$ExtClassLoader实现,加载JAVA_HOME/lib/ext目录中,或者被java.ext.dirs系统便你那个指定的路径中所有的类库,开发者可以直接使用类扩展加载器
* 应用类加载器Application Classloader,y
类加载时优先去父ClassLoader中查找,如果父Classloader中没有找到则到到子Classloader中查找.
#### 好处
* 类随着它的类加载器一起具备了一种带有优先级的层次关系
#### 用法

类层次划分\OGSI\热部署\代码加密
保证类是同一个
自定义classloader时可以先拦截class的加载过程,以确定是否需要交给父Classloader

### 破坏
父classloader依赖子Classloader,ThreadLocal

代码热替换