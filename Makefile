CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2
TARGET   := modem_sim

SRCS     := main.cpp AtModemSimulator.cpp
OBJS     := $(SRCS:.cpp=.o)

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET) virtual-tty

run: $(TARGET)
	./$(TARGET)