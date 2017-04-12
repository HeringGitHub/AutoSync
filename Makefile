all: AutoSync

LIBS = -lssh2

FLAGS = -g

AutoSync: main.cpp
	$(CXX) -o $@ main.cpp $(LIBS) $(FLAGS)

clean:
	rm -rf AutoSync