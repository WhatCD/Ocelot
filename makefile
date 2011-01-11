CXX=g++
CXXFLAGS=-march=native -O2 -fomit-frame-pointer -fno-ident -fvisibility-inlines-hidden -fvisibility=hidden -Wall -std=c++0x -iquote -Wl,O1 -Wl,--as-needed -I/usr/include/mysql -I/usr/include/mysql++ -I/usr/local/include/boost
LDFLAGS=-L/usr/local/lib
LIBS=-lmysqlpp -lboost_system -lboost_thread -lev
OCELOT=ocelot
OBJS=$(patsubst %.cpp,%.o,$(wildcard *.cpp))
all: $(OCELOT)
$(OCELOT): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS) $(LIBS)
%.o: %.cpp %.h
	$(CXX) -c $(CXXFLAGS) -o $@ $<
clean:
	rm -f $(OCELOT) $(OBJS)
