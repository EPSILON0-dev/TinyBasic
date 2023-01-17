.PHONY: build
build:
	gcc -o tinybasic main.c -O1 -g

.PHONY: clean
clean:
	-rm tinybasic