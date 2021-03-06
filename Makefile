CXX=g++
CXXFLAGS=-I. -Wall -std=c++11
DEPS= #header file 
LDFLAGS=-lboost_system

%.o: %.cpp $(DEPS)
	$(CXX) $(CXXFLAGS) -c $< -o $@

all: DVRouter TinyAODVRouter

debug: CXXFLAGS += -g
debug: DVRouter TinyAODVRouter

DVRouter: DVRouter.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

TinyAODVRouter: TinyAODVRouter.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

clean:
	rm -f *.o DVRouter TinyAODVRouter
	