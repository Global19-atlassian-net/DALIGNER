THISDIR:=$(abspath $(dir $(realpath $(lastword ${MAKEFILE_LIST}))))
#LIBDIRS?=${THISDIR}/../DAZZ_DB
CFLAGS+= -O3 -Wall -Wextra -fno-strict-aliasing -Wno-unused-result -DWORD_SIZE=16
#CPPFLAGS+= -I${THISDIR}/../DAZZ_DB -I${PREFIX}/include
#CPPFLAGS+= -MMD -MP
LDLIBS+= -lm -lpthread
LDFLAGS+= $(patsubst %,-L%,${LIBDIRS})
MOST = daligner HPC.daligner LAsort LAmerge LAsplit LAcat LAshow LAdump LAcheck LAindex
ALL:=${MOST} daligner_p LA4Falcon LA4Ice DB2Falcon
vpath %.c ${THISDIR}
#vpath %.a ${THISDIR}/../DAZZ_DB

%: %.c

all: ${ALL}
daligner: lsd.sort.o filter.o
daligner_p: lsd.sort.o filter_p.o
LA4Falcon: DBX.o
${ALL}: libdazzdb.a

libdazzdb.a: DB.o QV.o align.o
	${AR} rv $@ $^

install:
	rsync -av ${ALL} ${PREFIX}/bin
symlink:
	ln -sf $(addprefix ${CURDIR}/,${ALL}) ${PREFIX}/bin
clean:
	rm -f ${ALL}
	rm -f ${DEPS}
	rm -fr *.dSYM *.o *.d *.a

.PHONY: clean all

SRCS:=$(notdir $(wildcard ${THISDIR}/*.c))
#DEPS:=$(patsubst %.c,%.d,${SRCS})
#-include ${DEPS}
