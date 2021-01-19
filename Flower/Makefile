sourceFile:=$(wildcard *.cpp)

objFile:=$(patsubst %.cpp,%.o,$(sourceFile))

.PHONY:clean

flower:$(objFile)
	g++ -o $@ -ltcmalloc_minimal $^ -std=c++11 -pthread -Wall -W"conversion"; mv flower ..

%.o:%.cpp
	g++ -o $@ -c $^ -std=c++11 -Wall -W"conversion"

clean:
	rm -rf *.o ../flower
