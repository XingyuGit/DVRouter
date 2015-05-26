CXX=g++
CXXFLAGS=-I. -Wall
DEPS= #header file 
LDFLAGS=-lboost_system

%.o: %.cpp $(DEPS)
	$(CXX) $(CXXFLAGS) -c $< -o $@

all: DVRouter TinyAODVRouter

DVRouter: DVRouter.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

TinyAODVRouter: TinyAODVRouter.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

clean:
	rm -f *.o DVRouter TinyAODVRouter
	