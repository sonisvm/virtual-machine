## Instructions to compile
>gcc -g -Wall -o vcpu_scheduler vcpu_scheduler.c -lvirt

OR

> Run the command `make`.

## Code Description
### Problem statement
Given a set of virtual machines having different workloads, load balance them across the available physical cpus.

### Assumptions
- There will be only 1 vcpu for a virtual machine.
- No VMs are shutdown/started while the scheduler is running.
- No pcpus are enabled/disabled while the scheduler is running.

### Load balancing policy
The policy used for load balancing is as below:
- If the difference in load/usage between the busiest pcpu and freest pcpu is greater than 10%
  - Find the busiest domain running on the busiest pcpu.
  - Pin that domain to the freest pcpu.
  - Pin all the domains except the busiest, to the pcpu on which they were running previously. 
- If the difference in load/usage between the busiest pcpu and freest pcpu is less than or equal to 10%
  - Pin all domains to whichever pcpu they were running on previously.

### Code description
#### Structures used
1. `domainStats` - contains following information
	1. `domain` - pointer to the domain, type - `virDomainPtr`.
	2. `time` - cumulative time used by the vcpu, in nanoseconds.
	3. `usage` - usage in % for the vcpu, for the current run of the scheduler.
	4. `cpuNum` - the pcpu on which the domain is running.

2. `pcpuStats` - contains the followng information
	1. `usage` - usage in % of the pcpu, for the current run of the scheduler.
  
#### Algorithm
1. Connect to hypervisor using `virConnectOpen()`.
2. Get the list of all active and running domains using `virConnectListAllDomains()`.
3. Initialize `prevDomainStats` as an empty structure. This structure will be used to store the statistics information from previous run.
4. Create an array of `domainStatsPtr` called `currDomainStats` to hold the statistics information of each domain.
5. Invoke `getCurrVcpuTime()` to get the current vcpu statistics.
	1. `getCurrVcpuTime` makes use of `virDomainGetCPUStats()` to get the statistics.
	2. The vcpu time is extracted from `cpu_time` parameter.
6. Invoke `virNodeGetInfo()` to get the number of pcpus in the system.
7. Create an array of `pcpuStatsPtr` called `currPcpuStats` to hold the statistics about each pcpu.
8. If `prevDomainStats` i.e. information about domain statistics calculated in previous run is not empty
	1. Invoke `calculateDomainUsage()` to calculate the vcpu usage for each domain.
		1.  Usage is calculated as (vcpu time in current run - vcpu time in previous run)/ (t * 10^9) where `t` is the time interval between which the scheduler runs.
	2. Invoke `calculatePcpuUsage()` function to calculate the pcpu usage for each pcpu
		1. Usage of pcpu P = sum of usages of all vcpus running on P.
	3. Invoke `pinVcpuToPcpu()` to pin each vcpu to the best pcpu.
		1. Find the maximum and minimum pcpu usage as well as the busiest and freest pcpu.
		2. If the difference between maximum and minimum pcpu usage is greater than 10%
			1. Find the busiest vcpu running on the busiest pcpu.
			2. Pin the busiest vcpu to the freest pcpu.
		3. Pin all the vcpus (except the busiest one if such a vcpu was found in previous step) to whichever pcpus they were running on.
9. Store `currDomainStats` in `PrevDomainStats` to use for the next run.
10. Sleep for the specified time interval `t` and then start again from step 4. 
