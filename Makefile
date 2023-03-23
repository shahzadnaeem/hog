hog: hog.c
	cc -o hog hog.c

dhog: hog.c
	cc -o dhog -DDEBUG hog.c

clean:
	rm -f hog dhog
