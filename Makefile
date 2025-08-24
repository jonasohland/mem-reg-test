CC      = gcc
CFLAGS  = 
LDFLAGS = -lfabric 

memregtest: memregtest.c
	$(CC) -o mem-reg-test $(CFLAGS) memregtest.c $(LDFLAGS)
