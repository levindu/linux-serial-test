all:
	gcc -o linux-serial-test.$$(uname -m) ../linux-serial-test.c -static -s

pack:
	tar czf ../linux-serial-test.tgz .
	cp linux-serial-test.md ..
