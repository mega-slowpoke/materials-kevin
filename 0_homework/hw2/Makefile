CFLAGS = -Wall -Werror -g
CC = gcc $(CFLAGS)
SHELL = /bin/bash
CWD = $(shell pwd | sed 's/.*\///g')
AN = proj1

.PHONY: all test-setup test clean clean-tests zip

all: gradebook_main

gradebook.o: gradebook.h gradebook.c
	$(CC) -c gradebook.c

gradebook_main: gradebook.o gradebook_main.c
	$(CC) -o $@ $^

test-setup:
	@chmod u+x testius
	@rm -f MATH1572.txt MATH1573.bin csci1901.bin csci_2021.bin csci_2021.txt \
		phys1301.txt arth1001.bin econ1001.txt

ifdef testnum
test: gradebook_main test-setup
	./testius test_cases/tests.json -v -n "$(testnum)"
else
test: gradebook_main test-setup
	./testius test_cases/tests.json
endif

clean:
	rm -f *.o gradebook_main

clean-tests:
	rm -rf test_results
	rm -f MATH1572.txt MATH1573.bin csci1901.bin csci_2021.bin csci_2021.txt \
		phys1301.txt arth1001.bin econ1001.txt

zip: clean clean-tests
	rm -f $(AN)-code.zip
	cd .. && zip "$(CWD)/$(AN)-code.zip" -r "$(CWD)" -x "$(CWD)/test_cases/*" "$(CWD)/testius" "$(CWD)/arth1001.txt" "$(CWD)/econ1001.bin"
	@echo Zip created in $(AN)-code.zip
	@if (( $$(stat -c '%s' $(AN)-code.zip) > 10*(2**20) )); then echo "WARNING: $(AN)-code.zip seems REALLY big, check there are no abnormally large test files"; du -h $(AN)-code.zip; fi
	@if (( $$(unzip -t $(AN)-code.zip | wc -l) > 256 )); then echo "WARNING: $(AN)-code.zip has 256 or more files in it which may cause submission problems"; fi
