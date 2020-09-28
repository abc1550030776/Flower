#include "IndexNode.h"

IndexNode::IndexNode()
{
	start = 0;
	len = 0;
	preCmpLen = 0;
}

IndexNodeChild::IndexNodeChild()
{
	childType = 0;
	indexId = 0;
}