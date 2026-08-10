CXX ?= g++
CXXFLAGS ?= -std=c++11 -Wall -Wextra -pthread
LDFLAGS ?= -pthread
MYSQL_CONFIG ?= mysql_config
MYSQL_CFLAGS := $(shell $(MYSQL_CONFIG) --cflags 2>/dev/null)
MYSQL_LIBS := $(shell $(MYSQL_CONFIG) --libs 2>/dev/null)

ifeq ($(strip $(MYSQL_LIBS)),)
MYSQL_LIBS := -lmysqlclient
endif

CXXFLAGS += $(MYSQL_CFLAGS)
LDLIBS ?= -lcurl -lcrypt
LDLIBS += $(MYSQL_LIBS)

TARGET := build/server
SOURCES := \
	src/main.cpp \
	src/config/config.cpp \
	src/server/webserver.cpp \
	src/http/http_conn.cpp \
	src/threadpool/threadpool.cpp \
	src/db/CGmysql.cpp \
	src/cache/redis_client.cpp \
	src/mail/smtp_client.cpp \
	src/logging/app_logger.cpp \
	src/security/password_hasher.cpp
OBJECTS := $(patsubst %.cpp,build/%.o,$(SOURCES))
INCLUDES := \
	-Isrc/config \
	-Isrc/server \
	-Isrc/http \
	-Isrc/threadpool \
	-Isrc/db \
	-Isrc/cache \
	-Isrc/mail \
	-Isrc/logging \
	-Isrc/security

.PHONY: all clean rebuild

all: $(TARGET)

$(TARGET): $(OBJECTS)
	@mkdir -p $(dir $@)
	$(CXX) $(LDFLAGS) -o $@ $^ $(LDLIBS)

build/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)

rebuild: clean all
