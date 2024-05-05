TARGETS = smtp smtpec

all: $(TARGETS)

smtp: smtp.cc
	g++ $< -lpthread -lssl -lcrypto -lpthread -g -o $@

smtpec: smtpec.cc
	g++ $< -lpthread -lresolv -g -o $@

pack:
	m -f submit-hw2.zip
	zip -r submit-hw2.zip *.cc README Makefile

clean::
	rm -fv $(TARGETS) *~

realclean:: clean
	rm -fv submit-hw2.zip
