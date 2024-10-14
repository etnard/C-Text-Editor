kilo: kilo.c
	$(CC) kilo.c -o kilo -Wall -Wextra -pedantic -std=c99
	@# -Wall: most warnings
	@# -Wextra: extra warnings
	@# -pedantic: extra ISO C warnings
	@# -std=c90: ISO C99
