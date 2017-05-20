CPP = g++
AR = ar
INCLUDE = -I./include/
OPTIONS = -std=c++11 -Wall -Werror -Wextra -pedantic -g

SOURCES = $(shell cd src; find . -name "*.cpp" -type f)
OBJECTS = $(addprefix build/,$(SOURCES:.cpp=.o))

all: out/dpa_sys.a

out/dpa_sys.a: $(OBJECTS)
	@mkdir -p "$(shell dirname "$@")"
	rm -f "$@"
	$(AR) scr "$@" $^

build/%.o: src/%.cpp
	@mkdir -p "$(shell dirname "$@")"
	$(CPP) $(OPTIONS) $(INCLUDE) -c $< -o $@

clean:
	rm -f $(OBJECTS) out/dpa_sys.a
