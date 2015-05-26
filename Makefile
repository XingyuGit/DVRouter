CXX=g++
CXXFLAGS=-I. -Wall
DEPS= #header file 
LDFLAGS=-lboost_system

%.o: %.cpp $(DEPS)
	$(CXX) $(CXXFLAGS) -c $< -o $@

all: DVRouter

DVRouter: DVRouter.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

clean:
	rm -f *.o DVRouter
	