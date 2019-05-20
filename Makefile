mallook_CFLAGS += -Werror
mallook_CFLAGS += -Wall
mallook_CFLAGS += -Wextra
mallook_CFLAGS += -O2
mallook_CFLAGS += -g

libmallook_CFLAGS += $(mallook_CFLAGS)
libmallook_CFLAGS += -pthread
libmallook_CFLAGS += -fPIC

libmallook_CPPFLAGS += -DPIC

libmallook_LIBS += -ldl

.PHONY: all
all: libmallook.so mallook-test

mallook.o: mallook.c
	$(CC) $(libmallook_CFLAGS) $(CFLAGS) $(libmallook_CPPFLAGS) $(CPPFLAGS) -c -o $@ $^

libmallook.so: mallook.o
	$(CC) -shared $(libmallook_CFLAGS) $(CFLAGS) $(libmallook_LDFLAGS) $(LDFLAGS) -o $@ $^ $(libmallook_LIBS) $(LIBS)

mallook-test.o: mallook-test.c
	$(CC) $(mallook_CFLAGS) $(CFLAGS) $(mallook_CPPFLAGS) $(CPPFLAGS) -c -o $@ $^

mallook-test: mallook-test.o
	$(CC) $(mallook_CFLAGS) $(CFLAGS) $(mallook_LDFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS)

.PHONY: clean
clean:
	$(RM) mallook.o libmallook.so
	$(RM) mallook-test.o mallook-test
