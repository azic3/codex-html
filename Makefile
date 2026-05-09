CXX ?= g++
CXXFLAGS ?= -std=c++11 -Wall -Wextra -pthread
LDFLAGS ?= -pthread
LDLIBS ?= -lmysqlclient -lcurl -lcrypt

TARGET := server
SOURCES := main.cpp config.cpp webserver.cpp http_conn.cpp threadpool.cpp CGmysql.cpp smtp_client.cpp password_hasher.cpp
OBJECTS := $(SOURCES:.cpp=.o)

.PHONY: all clean rebuild

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(LDFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)

rebuild: clean all
