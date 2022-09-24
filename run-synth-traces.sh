#!/bin/bash
for benchmark in {1..9};
do	
	echo "Benchmark ${benchmark}"
	# Create directories for each benchmark
	mkdir synth-trace-results
	mkdir synth-trace-results/synth-$1-$3-benchmark-$benchmark-cores-$4-25k

	# Copy the master traces
	cp synth-benchmark-traces-25k/synth${benchmark}-trace/* synth-trace-results/synth-$1-$3-benchmark-$benchmark-cores-$4-25k/

	# Create a memory trace for each core. Assumes an 8-core system (0-7)
	for i in {0..7};
	do
		cp synth-benchmark-traces-25k/synth${benchmark}-trace/mem.trc synth-trace-results/synth-$1-$3-benchmark-$benchmark-cores-$4-25k/trace$i.trc	
		if [ $1 == "CARP" ]; then
			# Assumes core 7 executes E-level task. E-level tasks only do reads to shared data
			if [ $i = 7 ]; then
							sed -i 's/WR/RD/g' synth-$1-$3-benchmark-${benchmark}-25k/trace$i.trc
			fi
		fi
	done

	# Run the benchmark
	./build/X86_$1/gem5.opt \
					-d synth-trace-results/synth-$1-$3-benchmark-$benchmark-cores-$4-25k/ \
					--debug-flags=$2 \
					./configs/example/ruby_random_test.py \
					--ruby-clock=2GHz \
					--ruby \
					--cpu-clock=2GHz \
					--topology=Crossbar \
					--mem-type=SimpleMemory \
					-n $4 \
					--mem-size=4194304kB \
        	--maxloads=300000 \
        	--wakeup_freq=1 \
        	--trace_path synth-trace-results/synth-$1-$3-benchmark-$benchmark-cores-$4-25k &> synth-trace-results/synth-$1-$3-benchmark-$benchmark-cores-$4-25k/log.out   
done

# Usage:
# ./run-synth-traces.sh LPMSI ProtocolTrace example 8
# Runs LPMSI protocol with ProtocolTrace debug flag with 8 cores. Output is in synth-LPMSI-example-benchmark-*
