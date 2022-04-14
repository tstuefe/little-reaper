all: make-orphans-continuously make-orphans make-orphan

make-orphan: make-orphan.c
	cc -o make-orphan make-orphan.c

make-orphans: make-orphans.c
	cc -o make-orphans make-orphans.c

make-orphans-continuously: make-orphans-continuously.c
	cc -o make-orphans-continuously make-orphans-continuously.c

clean:
	rm make-orphans-continuously make-orphans make-orphan 

