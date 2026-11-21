CXX     ?= g++
CXXFLAGS = -std=c++17 -Wall -Wextra -I../include
SRC      = test_engine.cpp ../src/poudre_trail_engine.cpp
BIN      = test_engine

all: $(BIN)

$(BIN): $(SRC)
	$(CXX) $(CXXFLAGS) -o $@ $^

run: $(BIN)
	./$(BIN)

clean:
	rm -f $(BIN)
