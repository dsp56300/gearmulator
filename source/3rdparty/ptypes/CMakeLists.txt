cmake_minimum_required(VERSION 3.10)

project(ptypes)

set(SOURCES
	pmd5.cxx
	pvariant.cxx
	pipbase.cxx
	piobase.cxx
	ptime.cxx
	patomic.cxx
	pputf.cxx
	pinstm.cxx
	pnpipe.cxx
	pstrmanip.cxx
	pstring.cxx
	pipstm.cxx
	pmsgq.cxx
	ppodlist.cxx
	pipmsg.cxx
	pnpserver.cxx
	pstrlist.cxx
	pstrconv.cxx
	poutstm.cxx
	pipsvbase.cxx
	prwlock.cxx
	pthread.cxx
	pipmsgsv.cxx
	pstrtoi.cxx
	pinfilter.cxx
	pcset.cxx
	pobjlist.cxx
	punit.cxx
	pstdio.cxx
	pfdxstm.cxx
	ptimedsem.cxx
	poutfile.cxx
	pipstmsv.cxx
	pcomponent.cxx
	pinfile.cxx
	pstrcase.cxx
	poutmem.cxx
	ptrigger.cxx
	pcsetdbg.cxx
	pinmem.cxx
	ptextmap.cxx
	pmem.cxx
	poutfilter.cxx
	pstrutils.cxx
	pfatal.cxx
	psemaphore.cxx
	pintee.cxx
	pasync.cxx
	ppipe.cxx
	pmtxtable.cxx
	pexcept.cxx
	punknown.cxx
	pversion.cxx
)

add_library(ptypes STATIC)

target_sources(ptypes PRIVATE ${SOURCES})

target_include_directories(ptypes PUBLIC ../)
set_property(TARGET ptypes PROPERTY FOLDER "Networking")
