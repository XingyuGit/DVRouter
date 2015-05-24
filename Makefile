CXX=g++
CXXFLAGS=-I. -Wall
DEPS= #header file 
LDFLAGS=-lboost_system

%.o: %.cpp $(DEPS)
	$(CXX) $(CXXFLAGS) -c $< -o $@

all: my-router

my-router: my-router.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

clean:
	rm -f *.o my-router
	