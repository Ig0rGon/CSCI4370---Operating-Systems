all: wc wc_mul wc_b

wc: wc.c
	$(CC) wc.c -g -o wc

wc_mul: wc_mul.c
	$(CC) wc_mul.c -g -o wc_mul

wc_b: wc_b.c
	$(CC) wc_b.c -g -o wc_b

clean:
	rm -f wc wc_mul wc_b
