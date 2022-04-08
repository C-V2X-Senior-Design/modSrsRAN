#!/usr/bin/sh

cd ~/Code/modSrsRAN
rm -r probes
mkdir probes

# Set time length to collect data
if [ $# -eq 0 ]
	then
		TIME_LENGTH=5
else
	TIME_LENGTH=$1
fi

SFILE="timing_iq/timing_pusch.txt"
CFILE="timing_iq/timing_pucch.txt" 
RSFILE="timing_iq/timing_results.txt"

#Check/Add the Python executable to the shell PATH environment
#Check/Add ~/Code/srsRAN directory to the path 

echo "\n\n\n\nSTARTING\n\n\n\n"

### SET NAMESPACE ###
sudo ip netns add ue1

### START EPC ###
#may need priveleges and hang
sudo srsepc &
sleep 5


### START UE ###
#may need priveleges and hang
sudo srsue &
sleep 5

### START ENB ###
#may need privelges and hang
sudo srsenb & 
sleep 5



#Sleep time approximates how long to gather data for
sleep $TIME_LENGTH

# kills all srsenb processes, this works!!
pgrep srsenb | xargs sudo kill & 
# kill all srsue processes
pgrep srsue | xargs sudo kill &
# kill all srsepc processes
pgrep srsepc | xargs sudo kill & 
# kills all children
sleep 10
echo "\n\n\n\nKILLING PROCESSES\n\n\n\n"
kill -TERM -$$

#TODO: Need to figure out how to let Python program shut down without losing 
#any work in progress 

### Open the Python script and have it read from stdout buffer ###
# python3 timing_iq/timing.py TIME_LENGTH SFILE CFILE RSFILE