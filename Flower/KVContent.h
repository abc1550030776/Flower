#include "IndexFile.h"

class KVContent
{
public:
	bool init(const char* fileName, Index* index);
	bool get(unsigned long long key, unsigned long long& lowerBound, unsigned long long& upperBound, unsigned long long& value);
private:
	IndexFile indexFile;
};
