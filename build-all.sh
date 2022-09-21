#!/bin/bash
for i in LPMSI LPMOESI LPMI LPMSI LPMESI LPMOSI LPMOESI PMESI_C2C PMSI PMESI PMSI_C2C PMESI_C2C PMOESI CARP
do
	rm ./build/X86_$i/gem5-*.opt
	for cores in 4 8 16 
	do
		schedule="{"
		for ((j=0; j<${cores}-1; j++))
		do
			schedule=${schedule}"$j,"
		done
		schedule=${schedule}"$j}"
		echo $schedule

		sed -i '29s/.*/const unsigned int numCores = '$cores';/' src/cpu/testers/rubytest/Trace.hh
		sed -i '54s/.*/const unsigned int AB_SLOT_ALLOCATION[] = '${schedule}';/' src/cpu/testers/rubytest/Trace.hh
		sed -i '42s/.*/const unsigned int AB_CORES[] = '${schedule}';/' src/cpu/testers/rubytest/Trace.hh
		scons ./build/X86_$i/gem5.opt -j8
		cp ./build/X86_$i/gem5.opt ./build/X86_$i/gem5-$cores.opt
	done
done
