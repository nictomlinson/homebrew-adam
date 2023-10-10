CFLAGS=-Wall -Wextra -pedantic -std=c99 -g
BUILDIR=./bin
SOURCEDIR=./src

B=$(BUILDIR)/
O=$(B)obj/
S=$(SOURCEDIR)/

what:
	-@echo make \(all\|mcasm\)

all: mcasm mcCode

mcasm: $Bmcasm

mcCode: $Bmccode.bin

$Bmcasm:	$(S)mcasm.c
	$(CC) $(CFLAGS) -I$(S) -o $@ $(S)mcasm.c

$Bmccode.bin:	mcasm $(S)ports.ucode $(S)mcCode.ucode
	$(B)mcasm -o $@ $(S)ports.ucode $(S)mcCode.ucode
