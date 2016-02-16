op-tls: op-tls.o test-parser.o
	cc op-tls.o test-parser.o ../spec.o -o op-tls

optest1: optest1.s test-parser.c
	cc optest1.s test-parser.c ../spec.c -o optest1

clean:
	rm -f optest1 op-tls
