kilo: kilo.c
	$(CC) kilo.c -o kilo -Wall -Wextra -pedantic -std=c23

.PHONY: clean

clean: 
	rm -f kilo

run: kilo
	./kilo