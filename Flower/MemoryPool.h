#pragma once
#include <vector>
#include <mutex>
#include <cstddef>

// 通用内存池模板类
// 用于频繁分配和释放固定大小对象的场景
template<typename T>
class MemoryPool
{
public:
    // 构造函数
    // initialSize: 初始预分配的对象数量
    // growSize: 当内存池耗尽时每次增长的对象数量
    MemoryPool(size_t initialSize = 1024, size_t growSize = 512)
        : initialSize(initialSize), growSize(growSize)
    {
        allocateChunk(initialSize);
    }

    ~MemoryPool()
    {
        // 释放所有分配的内存块
        std::lock_guard<std::mutex> lock(mutex);
        for (auto chunk : chunks)
        {
            ::operator delete(chunk);
        }
    }

    // 从内存池分配一个对象
    // 使用placement new在预分配的内存上构造对象
    template<typename... Args>
    T* allocate(Args&&... args)
    {
        std::lock_guard<std::mutex> lock(mutex);
        
        // 如果空闲列表为空，分配新的内存块
        if (freeList.empty())
        {
            allocateChunk(growSize);
        }

        // 从空闲列表获取一个内存位置
        void* ptr = freeList.back();
        freeList.pop_back();

        // 在该位置使用placement new构造对象
        T* obj = new(ptr) T(std::forward<Args>(args)...);
        return obj;
    }

    // 释放对象回内存池
    void deallocate(T* obj)
    {
        if (obj == nullptr)
        {
            return;
        }

        // 显式调用析构函数
        obj->~T();

        // 将内存位置添加回空闲列表
        std::lock_guard<std::mutex> lock(mutex);
        freeList.push_back(obj);
    }

    // 获取统计信息
    size_t getFreeCount() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return freeList.size();
    }

    size_t getTotalCount() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return totalAllocated;
    }
    
    // 清空内存池，释放所有内存回系统
    // 注意：调用此函数前必须确保没有对象正在使用
    // reinit: 是否在清空后重新初始化内存池
    void clearAll(bool reinit = true)
    {
        std::lock_guard<std::mutex> lock(mutex);
        
        // 释放所有分配的内存块
        for (auto chunk : chunks)
        {
            ::operator delete(chunk);
        }
        
        // 清空容器
        chunks.clear();
        freeList.clear();
        totalAllocated = 0;
        
        // 如果需要重新初始化，分配初始大小的内存块
        if (reinit)
        {
            allocateChunk(initialSize);
        }
    }

private:
    // 禁止拷贝和赋值
    MemoryPool(const MemoryPool&) = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;

    // 分配一块新的内存块
    void allocateChunk(size_t count)
    {
        // 分配原始内存
        void* chunk = ::operator new(sizeof(T) * count);
        chunks.push_back(chunk);

        // 将内存块中的每个位置添加到空闲列表
        char* ptr = static_cast<char*>(chunk);
        for (size_t i = 0; i < count; ++i)
        {
            freeList.push_back(ptr);
            ptr += sizeof(T);
        }

        totalAllocated += count;
    }

    std::vector<void*> chunks;      // 所有分配的内存块
    std::vector<void*> freeList;    // 空闲对象列表
    size_t initialSize;             // 初始大小
    size_t growSize;                // 增长大小
    size_t totalAllocated = 0;      // 总共分配的对象数量
    mutable std::mutex mutex;       // 互斥锁，保证线程安全
};

// 前向声明IndexNode类型
class IndexNodeTypeOne;
class IndexNodeTypeTwo;
class IndexNodeTypeThree;
class IndexNodeTypeFour;

// 索引节点内存池管理器
// 为每种类型的IndexNode提供独立的内存池
// 注意：每个Index实例应该有自己的IndexNodePoolManager实例
class IndexNodePoolManager
{
public:
    // 构造函数和析构函数
    IndexNodePoolManager();
    ~IndexNodePoolManager();

    // 获取各个类型的内存池
    MemoryPool<IndexNodeTypeOne>& getPoolTypeOne();
    MemoryPool<IndexNodeTypeTwo>& getPoolTypeTwo();
    MemoryPool<IndexNodeTypeThree>& getPoolTypeThree();
    MemoryPool<IndexNodeTypeFour>& getPoolTypeFour();
    
    // 清空所有内存池，释放所有内存回系统
    // 警告：调用此函数前必须确保所有索引缓存已清空，没有对象正在使用
    void clearAllPools();

private:
    // 禁止拷贝和赋值
    IndexNodePoolManager(const IndexNodePoolManager&) = delete;
    IndexNodePoolManager& operator=(const IndexNodePoolManager&) = delete;

    MemoryPool<IndexNodeTypeOne>* poolTypeOne;
    MemoryPool<IndexNodeTypeTwo>* poolTypeTwo;
    MemoryPool<IndexNodeTypeThree>* poolTypeThree;
    MemoryPool<IndexNodeTypeFour>* poolTypeFour;
};
