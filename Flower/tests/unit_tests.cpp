#include <cstdio>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include <unistd.h>

#include "MemoryPool.h"
#include "Myfile.h"
#include "SetWithLock.h"
#include "interface.h"
#include "UniqueGenerator.h"
#include "common.h"

namespace {

class TestFailure : public std::runtime_error {
public:
	explicit TestFailure(const std::string& message) : std::runtime_error(message) {}
};

#define CHECK_TRUE(expr) \
	do { \
		if (!(expr)) { \
			throw TestFailure(std::string("check failed: ") + #expr); \
		} \
	} while (0)

#define CHECK_EQ(actual, expected) \
	do { \
		const auto actualValue = (actual); \
		const auto expectedValue = (expected); \
		if (!(actualValue == expectedValue)) { \
			throw TestFailure(std::string("check failed: ") + #actual + " == " + #expected); \
		} \
	} while (0)

struct TempFile {
	char path[sizeof("/tmp/flower-unit-test-XXXXXX")] = "/tmp/flower-unit-test-XXXXXX";
	int fd = -1;

	TempFile() {
		fd = mkstemp(path);
		if (fd < 0) {
			throw TestFailure("mkstemp failed");
		}
		close(fd);
		fd = -1;
	}

	~TempFile() {
		unlink(path);
	}
};

struct TrackedObject {
	static int constructorCount;
	static int destructorCount;

	int value;

	explicit TrackedObject(int value) : value(value) {
		++constructorCount;
	}

	~TrackedObject() {
		++destructorCount;
	}
};

int TrackedObject::constructorCount = 0;
int TrackedObject::destructorCount = 0;

void testUniqueGeneratorSequentialAllocation() {
	UniqueGenerator generator;
	CHECK_EQ(generator.acquireNumber(1), 1ULL);
	CHECK_EQ(generator.acquireNumber(3), 2ULL);
	CHECK_EQ(generator.acquireNumber(2), 5ULL);
}

void testUniqueGeneratorRecycleAndSplit() {
	UniqueGenerator generator;
	generator.setInitMaxUniqueNum(100);
	generator.recycleNumber(40, 5);

	CHECK_EQ(generator.acquireNumber(3), 40ULL);
	CHECK_EQ(generator.acquireNumber(2), 43ULL);
	CHECK_EQ(generator.acquireNumber(1), 100ULL);
}

void testUniqueGeneratorIgnoresOversizedRecycle() {
	UniqueGenerator generator;
	generator.setInitMaxUniqueNum(500);
	generator.recycleNumber(12, static_cast<unsigned char>(RECYCLE_BUCKET_COUNT + 1));

	CHECK_EQ(generator.acquireNumber(1), 500ULL);
}

void testMemoryPoolTracksFreeAndReusesSlots() {
	TrackedObject::constructorCount = 0;
	TrackedObject::destructorCount = 0;

	MemoryPool<TrackedObject> pool(2, 1);
	CHECK_EQ(pool.getTotalCount(), 2ULL);
	CHECK_EQ(pool.getFreeCount(), 2ULL);

	TrackedObject* first = pool.allocate(7);
	TrackedObject* second = pool.allocate(9);
	CHECK_EQ(first->value, 7);
	CHECK_EQ(second->value, 9);
	CHECK_EQ(pool.getFreeCount(), 0ULL);

	pool.deallocate(first);
	CHECK_EQ(pool.getFreeCount(), 1ULL);

	TrackedObject* reused = pool.allocate(11);
	CHECK_EQ(reused, first);
	CHECK_EQ(reused->value, 11);

	pool.deallocate(reused);
	pool.deallocate(second);
	CHECK_EQ(pool.getFreeCount(), 2ULL);
	CHECK_EQ(TrackedObject::constructorCount, 3);
	CHECK_EQ(TrackedObject::destructorCount, 3);
}

void testMemoryPoolGrowsAndClears() {
	MemoryPool<int> pool(1, 2);
	int* first = pool.allocate(1);
	int* second = pool.allocate(2);
	(void)first;
	(void)second;

	CHECK_EQ(pool.getTotalCount(), 3ULL);
	pool.clearAll(true);
	CHECK_EQ(pool.getTotalCount(), 1ULL);
	CHECK_EQ(pool.getFreeCount(), 1ULL);
}

void testMyfileReadWriteAndSync() {
	TempFile tempFile;
	Myfile file;
	CHECK_TRUE(file.init(tempFile.path, true));

	char payload[] = "flower";
	CHECK_TRUE(file.write(0, payload, sizeof(payload)));
	CHECK_TRUE(file.sync());

	char buffer[sizeof(payload)] = {};
	CHECK_TRUE(file.read(0, buffer, sizeof(buffer)));
	CHECK_EQ(std::string(buffer, sizeof(buffer)), std::string(payload, sizeof(payload)));
}

void testCommonHelpers() {
	char indexPath[4096] = {};
	char kvPath[4096] = {};

	CHECK_TRUE(getIndexPath("/tmp/data", indexPath));
	CHECK_TRUE(getKVFilePath("/tmp/data", kvPath));
	CHECK_EQ(std::string(indexPath), std::string("/tmp/data.idx"));
	CHECK_EQ(std::string(kvPath), std::string("/tmp/data.kvi"));

	CHECK_EQ(swiftBigLittleEnd(static_cast<unsigned char>(0x12)), static_cast<unsigned char>(0x12));
	CHECK_EQ(swiftBigLittleEnd(static_cast<unsigned short>(0x1234)), static_cast<unsigned short>(0x3412));
	CHECK_EQ(swiftBigLittleEnd(0x12345678U), 0x78563412U);
	CHECK_EQ(swiftBigLittleEnd(0x0123456789ABCDEFULL), 0xEFCDAB8967452301ULL);
}

void testAddFindPosHandlesPositiveAndNegativeSkip() {
	TempFile tempFile;
	Myfile file;
	CHECK_TRUE(file.init(tempFile.path, true));

	char content[] = "abcdef";
	CHECK_TRUE(file.write(0, content, sizeof(content) - 1));

	std::set<unsigned long long> positions;
	SetWithLock setWithLock(&positions);

	CHECK_TRUE(AddFindPos(&setWithLock, 3, 2, file, "abcdef", 6));
	CHECK_TRUE(AddFindPos(&setWithLock, 2, -2, file, "abcdef", 6));

	CHECK_EQ(positions.size(), 2ULL);
	CHECK_TRUE(positions.count(0) == 1);
	CHECK_TRUE(positions.count(5) == 1);
}

void testAddFindPosSkipsInsertOnPrefixMismatch() {
	TempFile tempFile;
	Myfile file;
	CHECK_TRUE(file.init(tempFile.path, true));

	char content[] = "zzcdef";
	CHECK_TRUE(file.write(0, content, sizeof(content) - 1));

	std::set<unsigned long long> positions;
	SetWithLock setWithLock(&positions);

	CHECK_TRUE(AddFindPos(&setWithLock, 2, -2, file, "abcdef", 6));
	CHECK_TRUE(positions.empty());
}

void testEqualBytesFastPathHandlesShortNeedles() {
	const unsigned char left7[] = {'f', 'l', 'o', 'w', 'e', 'r', '!'};
	const unsigned char same7[] = {'f', 'l', 'o', 'w', 'e', 'r', '!'};
	const unsigned char diff7[] = {'f', 'l', 'o', 'w', 'e', 'r', '?'};
	const unsigned char left3[] = {'a', 'b', 'c'};
	const unsigned char diff3[] = {'a', 'b', 'd'};

	CHECK_TRUE(EqualBytesFastPath(left7, same7, 7));
	CHECK_TRUE(!EqualBytesFastPath(left7, diff7, 7));
	CHECK_TRUE(EqualBytesFastPath(left3, reinterpret_cast<const unsigned char*>("abc"), 3));
	CHECK_TRUE(!EqualBytesFastPath(left3, diff3, 3));
}

void testSearchContextShortBranchSupportsOffsetsAndLineColumns() {
	TempFile tempFile;
	Myfile file;
	CHECK_TRUE(file.init(tempFile.path, true));

	const char content[] = "zero\nabc\nxyzabc\n";
	CHECK_TRUE(file.write(0, const_cast<char*>(content), sizeof(content) - 1));
	CHECK_TRUE(file.sync());

	char indexPath[4096] = {};
	char kvPath[4096] = {};
	CHECK_TRUE(getIndexPath(tempFile.path, indexPath));
	CHECK_TRUE(getKVFilePath(tempFile.path, kvPath));

	CHECK_TRUE(BuildDstIndex(tempFile.path, true));

	SearchContext context;
	CHECK_TRUE(context.init(tempFile.path, 2, true));

	std::set<unsigned long long> positions;
	CHECK_TRUE(context.search("abc", 3, &positions));
	CHECK_EQ(positions.size(), 2ULL);
	CHECK_TRUE(positions.count(5) == 1);
	CHECK_TRUE(positions.count(12) == 1);

	ResultMap result;
	CHECK_TRUE(context.search("abc", 3, &result));
	CHECK_EQ(result.size(), 2ULL);

	auto first = result.find(5);
	CHECK_TRUE(first != result.end());
	CHECK_EQ(first->second.GetLineNum(), 1ULL);
	CHECK_EQ(first->second.GetColumnNum(), 0ULL);
	CHECK_EQ(first->second.GetEndLineNum(), 1ULL);
	CHECK_EQ(first->second.GetEndColumnNum(), 2ULL);

	auto second = result.find(12);
	CHECK_TRUE(second != result.end());
	CHECK_EQ(second->second.GetLineNum(), 2ULL);
	CHECK_EQ(second->second.GetColumnNum(), 3ULL);
	CHECK_EQ(second->second.GetEndLineNum(), 2ULL);
	CHECK_EQ(second->second.GetEndColumnNum(), 5ULL);

	unlink(indexPath);
	unlink(kvPath);
}

}  // namespace

int main() {
	const std::vector<std::pair<const char*, void(*)()>> tests = {
		{"UniqueGeneratorSequentialAllocation", testUniqueGeneratorSequentialAllocation},
		{"UniqueGeneratorRecycleAndSplit", testUniqueGeneratorRecycleAndSplit},
		{"UniqueGeneratorIgnoresOversizedRecycle", testUniqueGeneratorIgnoresOversizedRecycle},
		{"MemoryPoolTracksFreeAndReusesSlots", testMemoryPoolTracksFreeAndReusesSlots},
		{"MemoryPoolGrowsAndClears", testMemoryPoolGrowsAndClears},
		{"MyfileReadWriteAndSync", testMyfileReadWriteAndSync},
		{"CommonHelpers", testCommonHelpers},
		{"AddFindPosHandlesPositiveAndNegativeSkip", testAddFindPosHandlesPositiveAndNegativeSkip},
		{"AddFindPosSkipsInsertOnPrefixMismatch", testAddFindPosSkipsInsertOnPrefixMismatch},
		{"EqualBytesFastPathHandlesShortNeedles", testEqualBytesFastPathHandlesShortNeedles},
		{"SearchContextShortBranchSupportsOffsetsAndLineColumns", testSearchContextShortBranchSupportsOffsetsAndLineColumns},
	};

	for (const auto& test : tests) {
		try {
			test.second();
			std::cout << "[PASS] " << test.first << '\n';
		} catch (const std::exception& ex) {
			std::cerr << "[FAIL] " << test.first << ": " << ex.what() << '\n';
			return 1;
		}
	}

	return 0;
}
