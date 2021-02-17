# Flower
一个可以为大文件构建索引，加快查询字符串出现位置的工具。支持查询出现在某一行、某一列、还有在整个文件当中的第几个字节。

使用方法：
  1、需要包含interface.h头文件
  2、构建索引方法：
  调用:bool BuildDstIndex(const char* fileName, bool needBuildLineIndex = false, char delimiter = '\n');函数
  参数:fileName需要构建索引的文件名，needBuildLineIndex是否需要构建行索引如果不需要搜索具体到某一行的某一列则不需要填这个参数否则填true，delimiter分隔符以哪个字符当作行的结尾默认用\n
  返回值:构建成功返回true,失败返回false
  3、搜索方法:
  先创建一个SearchContext对象
  然后调用bool SearchContext::init(const char* fileName, unsigned long threadNum, bool searchLine)函数
  参数:fileName搜索的文件名,threadNum是搜索的时候使用的线程数量如果不填就用cpu使用的逻辑线程数,searchLine搜索的时候是否需要搜索具体到某一行某一列
  返回值:初始化成功返回true,失败返回false
  如果不需要搜索某一行和某一列调用bool SearchContext::search(const char* searchTarget, unsigned int targetLen, std::set<unsigned long long>* set)
  参数:searchTarget 需要搜索的字符串第一个字符的指针targetLen需要搜索的字符串长度set来储存搜索到的结果值的集合位置从0开始
  返回值:搜索成功返回true,失败返回false
  如果需要搜索到具体到某一行和某一列则调用bool SearchContext::search(const char* searchTarget, unsigned int targetLen, ResultMap* map)
  参数:searchTarget需要搜索的字符串第一个字符的指针targetLen需要搜索的字符串长度map用来存储结果的map,key是在文件当中的位置从0开始、值是第几行第几列可以调用GetLineNum和GetColumnNum得到都是从0开始
  返回值:搜索成功返回true,失败返回false
  两个search函数可以反复调用,不要构建索引和搜索索引同时进行,结束的时候把SearchContext对象销毁

例子:
      程序里面的main.cpp是使用例子,我在测试的时候使用上了tcmalloc，需要修改WORKSPACE文件中name为com_google_tcmalloc的local_repository把path改成tcmalloc源文件的根目录，创建一个/test文件，代码根目录运行bazel run //Flower:flower --cxxopt='-std=c++17'
