# Makefile for the Leon architecture using GLISS V2

# configuration
ARCH = microblaze
GLISS_PREFIX=../gliss2
WITH_DISASM=1	# comment it to prevent disassembler building
#WITH_SIM=1		# comment it to prevent simulator building

MEMORY=vfast_mem
LOADER=old_elf
SYSCALL=syscall-linux


# files
GOALS=
ifdef WITH_DISASM
GOALS+=$(ARCH)-disasm
endif
ifdef WITH_SIM
GOALS+=$(ARCH)-sim
endif

SUBDIRS=src sim disasm
CLEAN=$(ARCH).nml $(ARCH).irg
DISTCLEAN=include src disasm sim

GFLAGS=\
	-m mem:$(MEMORY) \
	-m loader:$(LOADER) \
	-m code:code \
	-m env:void_env \
	-a disasm.c \
	-a used_regs.c

NMP =\
	$(ARCH).nmp


# targets
all: lib $(GOALS)

$(ARCH).nml: $(NMP)
	$(GLISS_PREFIX)/gep/gliss-nmp2nml.pl $< $@

$(ARCH).irg: $(ARCH).nml
	$(GLISS_PREFIX)/irg/mkirg $< $@

src include: $(ARCH).irg
	$(GLISS_PREFIX)/gep/gep $(GFLAGS) $< -S

lib: src include/$(ARCH)/config.h src/disasm.c src/used_regs.c
	(cd src; $(MAKE))

$(ARCH)-disasm: lib
	cd disasm; $(MAKE)

$(ARCH)-sim: lib
	cd sim; $(MAKE)

include/$(ARCH)/config.h: config.tpl
	test -d include/$(ARCH) || mkdir -p include/$(ARCH)
	cp config.tpl include/$(ARCH)/config.h

src/disasm.c: $(ARCH).irg
	$(GLISS_PREFIX)/gep/gliss-disasm $< -o $@ -c

src/used_regs.c: $(ARCH).irg
	$(GLISS_PREFIX)/gep/gliss-used-regs $< 

distclean: clean
	for d in $(SUBDIRS); do test -d $$d && (cd $$d; make distclean || exit 1); done
	rm -rf $(DISTCLEAN)

clean: only-clean
	for d in $(SUBDIRS); do test -d $$d && (cd $$d; make clean || exit 1); done

only-clean:
	rm -rf $(CLEAN)