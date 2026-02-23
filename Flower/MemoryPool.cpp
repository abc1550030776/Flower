#include "MemoryPool.h"
#include "IndexNode.h"

IndexNodePoolManager::IndexNodePoolManager()
{
    // 为每种类型的节点创建内存池
    // 预分配2048个节点，当耗尽时每次增长1024个
    poolTypeOne = new MemoryPool<IndexNodeTypeOne>(2048, 1024);
    poolTypeTwo = new MemoryPool<IndexNodeTypeTwo>(2048, 1024);
    poolTypeThree = new MemoryPool<IndexNodeTypeThree>(2048, 1024);
    poolTypeFour = new MemoryPool<IndexNodeTypeFour>(2048, 1024);
}

IndexNodePoolManager::~IndexNodePoolManager()
{
    delete poolTypeOne;
    delete poolTypeTwo;
    delete poolTypeThree;
    delete poolTypeFour;
}

MemoryPool<IndexNodeTypeOne>& IndexNodePoolManager::getPoolTypeOne()
{
    return *poolTypeOne;
}

MemoryPool<IndexNodeTypeTwo>& IndexNodePoolManager::getPoolTypeTwo()
{
    return *poolTypeTwo;
}

MemoryPool<IndexNodeTypeThree>& IndexNodePoolManager::getPoolTypeThree()
{
    return *poolTypeThree;
}

MemoryPool<IndexNodeTypeFour>& IndexNodePoolManager::getPoolTypeFour()
{
    return *poolTypeFour;
}

void IndexNodePoolManager::clearAllPools()
{
    // 清空所有内存池，释放内存回系统，并重新初始化
    // 注意：调用此函数前必须确保所有索引缓存已清空，没有对象正在使用
    poolTypeOne->clearAll(true);   // true 表示清空后重新初始化
    poolTypeTwo->clearAll(true);
    poolTypeThree->clearAll(true);
    poolTypeFour->clearAll(true);
}
