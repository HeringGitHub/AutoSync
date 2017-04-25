all: autosync

LIBS = 

FLAGS = -g

OBJECT = main.o Log.o
autosync: $(OBJECT)
	$(CXX) -o $@ $^ $(LIBS) $(FLAGS)
	rm -rf *.o

clean:
	rm -rf autosync