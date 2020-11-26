sourceFile:=$(wildcard *.cpp)

objFile:=$(patsubst %.cpp,%.o,$(sourceFile))

.PHONY:clean

flower:$(objFile)
	g++ -o $@ $^ -std=c++11 -pthread -Wall -W"conversion"

%.o:%.cpp
	g++ -o $@ -c $^ -std=c++11 -Wall -W"conversion"

clean:
	rm -rf *.o flower
