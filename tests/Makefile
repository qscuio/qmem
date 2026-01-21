# Tests Makefile

CC := gcc
CFLAGS := -Wall -Wextra -std=c11 -I../include -I../src -g
LDFLAGS := -lpthread

TESTS := test_json test_services

all: $(TESTS)
	@echo "Running tests..."
	@for t in $(TESTS); do echo "  Running $$t..."; ./$$t || exit 1; done
	@echo "All tests passed!"

test_json: test_json.c ../build/common/json.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test_services: test_services.c ../build/common/*.o ../build/services/meminfo.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(TESTS)

.PHONY: all clean
