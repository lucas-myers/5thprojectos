CXX = g++
CXXFLAGS = -Wall -g

all: oss worker

oss: oss.cpp deadlockdetection.cpp deadlockdetection.h
	$(CXX) $(CXXFLAGS) -o oss oss.cpp deadlockdetection.cpp

worker: worker.cpp
	$(CXX) $(CXXFLAGS) -o worker worker.cpp

clean:
	rm -f oss worker *.o *.log